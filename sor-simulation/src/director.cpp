#include "director.hpp"

#include "ipc/message_queue.hpp"
#include "ipc/semaphore.hpp"
#include "ipc/shared_memory.hpp"
#include "logging/logger.hpp"
#include "model/config.hpp"
#include "model/events.hpp"
#include "model/shared_state.hpp"
#include "model/types.hpp"
#include "util/error.hpp"

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
struct IpcIds {
    int logQueue{-1};
    int regQueue{-1};
    int triageQueue{-1};
    int specialistsQueue{-1};
    int shmId{-1};
    int semWaitingRoom{-1};
    int semSharedState{-1};
};

bool createQueues(const std::string& keyPath, IpcIds& ids) {
    MessageQueue logQ;
    MessageQueue regQ;
    MessageQueue triQ;
    MessageQueue specQ;

    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
    key_t specKey = ftok(keyPath.c_str(), 'S');
    if (logKey == -1 || regKey == -1 || triKey == -1 || specKey == -1) {
        logErrno("ftok failed");
        return false;
    }

    if (!logQ.create(logKey, 0600) || !regQ.create(regKey, 0600) ||
        !triQ.create(triKey, 0600) || !specQ.create(specKey, 0600)) {
        return false;
    }

    ids.logQueue = logQ.id();
    ids.regQueue = regQ.id();
    ids.triageQueue = triQ.id();
    ids.specialistsQueue = specQ.id();
    return true;
}

bool createSemaphores(const std::string& keyPath, const Config& cfg, IpcIds& ids) {
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t stateKey = ftok(keyPath.c_str(), 'M');
    if (waitKey == -1 || stateKey == -1) {
        logErrno("ftok for semaphores failed");
        return false;
    }

    Semaphore waitSem;
    Semaphore stateSem;
    if (!waitSem.create(waitKey, cfg.N_waitingRoom, 0600)) {
        return false;
    }
    ids.semWaitingRoom = waitSem.id();
    if (!stateSem.create(stateKey, 1, 0600)) {
        if (ids.semWaitingRoom != -1) {
            semctl(ids.semWaitingRoom, 0, IPC_RMID);
            ids.semWaitingRoom = -1;
        }
        return false;
    }
    ids.semSharedState = stateSem.id();
    return true;
}

bool createSharedState(const std::string& keyPath, IpcIds& ids, SharedState*& stateOut) {
    SharedMemory shm;
    key_t shmKey = ftok(keyPath.c_str(), 'H');
    if (shmKey == -1) {
        logErrno("ftok for shm failed");
        return false;
    }
    if (!shm.create(shmKey, sizeof(SharedState), 0600)) {
        return false;
    }
    void* addr = shm.attach();
    if (!addr) {
        shmctl(shm.id(), IPC_RMID, nullptr);
        return false;
    }
    auto* shared = static_cast<SharedState*>(addr);
    std::memset(shared, 0, sizeof(SharedState));
    ids.shmId = shm.id();
    stateOut = shared;
    return true;
}

void destroyIpc(const IpcIds& ids, SharedState* attachedState) {
    if (attachedState) {
        shmdt(attachedState);
    }
    if (ids.logQueue != -1) {
        msgctl(ids.logQueue, IPC_RMID, nullptr);
    }
    if (ids.regQueue != -1) {
        msgctl(ids.regQueue, IPC_RMID, nullptr);
    }
    if (ids.triageQueue != -1) {
        msgctl(ids.triageQueue, IPC_RMID, nullptr);
    }
    if (ids.specialistsQueue != -1) {
        msgctl(ids.specialistsQueue, IPC_RMID, nullptr);
    }
    if (ids.shmId != -1) {
        shmctl(ids.shmId, IPC_RMID, nullptr);
    }
    if (ids.semWaitingRoom != -1) {
        semctl(ids.semWaitingRoom, 0, IPC_RMID);
    }
    if (ids.semSharedState != -1) {
        semctl(ids.semSharedState, 0, IPC_RMID);
    }
}
} // namespace

int Director::run(const std::string& selfPath, const Config& config) {
    IpcIds ids;
    SharedState* shared = nullptr;
    bool ok = true;

    if (!createQueues(selfPath, ids)) {
        ok = false;
    }
    if (ok && !createSemaphores(selfPath, config, ids)) {
        ok = false;
    }
    if (ok && !createSharedState(selfPath, ids, shared)) {
        ok = false;
    }

    std::string logPath = "sor_run_" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".log";

    pid_t loggerPid = -1;
    if (ok) {
        loggerPid = fork();
        if (loggerPid == -1) {
            logErrno("fork for logger failed");
            ok = false;
        } else if (loggerPid == 0) {
            std::string queueIdStr = std::to_string(ids.logQueue);
            std::vector<char*> args;
            args.push_back(const_cast<char*>(selfPath.c_str()));
            args.push_back(const_cast<char*>("logger"));
            args.push_back(const_cast<char*>(queueIdStr.c_str()));
            args.push_back(const_cast<char*>(logPath.c_str()));
            args.push_back(nullptr);
            execv(selfPath.c_str(), args.data());
            logErrno("execv for logger failed");
            _exit(1);
        }
    }

    if (ok && shared) {
        shared->currentInWaitingRoom = 0;
        shared->queueRegistrationLen = 0;
        shared->reg2Active = 0;
        shared->totalPatients = 0;
        shared->triageRed = shared->triageYellow = shared->triageGreen = shared->triageSentHome = 0;
        shared->outcomeHome = shared->outcomeWard = shared->outcomeOther = 0;
        shared->directorPid = getpid();
        shared->registration1Pid = shared->registration2Pid = shared->triagePid = 0;
    }

    if (ok) {
        logEvent(ids.logQueue, Role::Director, 0, "Director: IPC initialized, logger spawned: " + logPath);
    }

    // TODO: spawn roles via exec with IPC ids once implemented.

    // send termination marker for logger
    if (ok) {
        logEvent(ids.logQueue, Role::Director, 0, "END");
    }

    int status = 0;
    if (loggerPid > 0) {
        if (waitpid(loggerPid, &status, 0) == -1) {
            logErrno("waitpid for logger failed");
            ok = false;
        }
    }

    destroyIpc(ids, shared);

    if (!ok) {
        return 1;
    }
    if (loggerPid > 0 && WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return ok ? 0 : 1;
}
