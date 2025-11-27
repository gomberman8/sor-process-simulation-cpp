#include "director.hpp"

#include "ipc/message_queue.hpp"
#include "ipc/semaphore.hpp"
#include "ipc/shared_memory.hpp"
#include "logging/logger.hpp"
#include "model/config.hpp"
#include "model/events.hpp"
#include "model/shared_state.hpp"
#include "model/types.hpp"
#include "roles/triage.hpp"
#include "roles/specialist.hpp"
#include "util/error.hpp"
#include "util/random.hpp"

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
#include <signal.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>

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

std::atomic<bool> stopRequested(false);

void handleSigint(int) {
    stopRequested.store(true);
}

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

    // Increase per-queue capacity to avoid blocking when traffic spikes.
    auto tuneQueue = [](int qid) {
        struct msqid_ds ds {};
        if (msgctl(qid, IPC_STAT, &ds) == -1) return;
        ds.msg_qbytes = 262144; // 256 KB if permitted by system limits
        msgctl(qid, IPC_SET, &ds);
    };
    tuneQueue(logQ.id());
    tuneQueue(regQ.id());
    tuneQueue(triQ.id());
    tuneQueue(specQ.id());

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
    Semaphore stateSemGuard;
    struct msqid_ds regStats{};

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

    // Handle Ctrl+C to request early stop.
    struct sigaction saInt {};
    saInt.sa_handler = handleSigint;
    sigemptyset(&saInt.sa_mask);
    saInt.sa_flags = 0;
    sigaction(SIGINT, &saInt, nullptr);

    if (ok && shared) {
        shared->currentInWaitingRoom = 0;
        shared->waitingRoomCapacity = config.N_waitingRoom;
        shared->queueRegistrationLen = 0;
        shared->reg2Active = 0;
        shared->totalPatients = 0;
        shared->triageRed = shared->triageYellow = shared->triageGreen = shared->triageSentHome = 0;
        shared->outcomeHome = shared->outcomeWard = shared->outcomeOther = 0;
        shared->directorPid = getpid();
        shared->registration1Pid = shared->registration2Pid = shared->triagePid = 0;
    }
    if (ok) {
        key_t stateKey = ftok(selfPath.c_str(), 'M');
        if (stateKey == -1 || !stateSemGuard.open(stateKey)) {
            ok = false;
        }
    }

    if (ok) {
        logEvent(ids.logQueue, Role::Director, 0, "Director: IPC initialized, logger spawned: " + logPath);
        logEvent(ids.logQueue, Role::Director, 0,
                 "Simulation config N=" + std::to_string(config.N_waitingRoom) +
                 " K=" + std::to_string(config.K_registrationThreshold) +
                 " simMinutes=" + std::to_string(config.simulationDurationMinutes) +
                 " totalPatients=" + std::to_string(config.totalPatientsTarget) +
                 " msPerMinute=" + std::to_string(config.timeScaleMsPerSimMinute));
    }

    pid_t reg1Pid = -1;
    pid_t reg2Pid = -1;
    if (ok) {
        reg1Pid = fork();
        if (reg1Pid == -1) {
            logErrno("fork for registration failed");
            ok = false;
        } else if (reg1Pid == 0) {
            std::vector<char*> args;
            args.push_back(const_cast<char*>(selfPath.c_str()));
            args.push_back(const_cast<char*>("registration"));
            args.push_back(const_cast<char*>(selfPath.c_str())); // key path same as executable
            args.push_back(nullptr);
            execv(selfPath.c_str(), args.data());
            logErrno("execv for registration failed");
            _exit(1);
        } else {
            if (shared) {
                shared->registration1Pid = reg1Pid;
            }
            logEvent(ids.logQueue, Role::Director, 0, "Registration1 spawned");
        }
    }
    pid_t triagePid = -1;
    if (ok) {
        triagePid = fork();
        if (triagePid == -1) {
            logErrno("fork for triage failed");
            ok = false;
        } else if (triagePid == 0) {
            std::vector<char*> args;
            args.push_back(const_cast<char*>(selfPath.c_str()));
            args.push_back(const_cast<char*>("triage"));
            args.push_back(const_cast<char*>(selfPath.c_str())); // key path
            args.push_back(nullptr);
            execv(selfPath.c_str(), args.data());
            logErrno("execv for triage failed");
            _exit(1);
        } else {
            if (shared) {
                shared->triagePid = triagePid;
            }
            logEvent(ids.logQueue, Role::Director, 0, "Triage spawned");
        }
    }

    pid_t generatorPid = -1;
    if (ok) {
        generatorPid = fork();
        if (generatorPid == -1) {
            logErrno("fork for patient generator failed");
            ok = false;
        } else if (generatorPid == 0) {
            // pass config values as args
            std::vector<std::string> argVals = {
                std::to_string(config.N_waitingRoom),
                std::to_string(config.K_registrationThreshold),
                std::to_string(config.simulationDurationMinutes),
                std::to_string(config.totalPatientsTarget),
                std::to_string(config.timeScaleMsPerSimMinute),
                std::to_string(config.randomSeed)
            };
            std::vector<char*> args;
            args.push_back(const_cast<char*>(selfPath.c_str()));
            args.push_back(const_cast<char*>("patient_generator"));
            args.push_back(const_cast<char*>(selfPath.c_str())); // key path
            for (auto& s : argVals) {
                args.push_back(const_cast<char*>(s.c_str()));
            }
            args.push_back(nullptr);
            execv(selfPath.c_str(), args.data());
            logErrno("execv for patient generator failed");
            _exit(1);
        } else {
            logEvent(ids.logQueue, Role::Director, 0, "Patient generator spawned");
        }
    }
    std::vector<pid_t> specialistPids;
    if (ok) {
        int specCount = 6;
        for (int i = 0; i < specCount; ++i) {
            pid_t pid = fork();
            if (pid == -1) {
                logErrno("fork for specialist failed");
                ok = false;
                break;
            } else if (pid == 0) {
                std::string typeStr = std::to_string(i);
                std::vector<char*> args;
                args.push_back(const_cast<char*>(selfPath.c_str()));
                args.push_back(const_cast<char*>("specialist"));
                args.push_back(const_cast<char*>(selfPath.c_str())); // key path
                args.push_back(const_cast<char*>(typeStr.c_str()));
                args.push_back(nullptr);
                execv(selfPath.c_str(), args.data());
                logErrno("execv for specialist failed");
                _exit(1);
            } else {
                specialistPids.push_back(pid);
                logEvent(ids.logQueue, Role::Director, 0, "Specialist spawned type " + std::to_string(i));
            }
        }
    }

    auto waitWithTimeout = [&](pid_t pid, const std::string& name) {
        if (pid <= 0) return;
        int status = 0;
        auto start = std::chrono::steady_clock::now();
        while (true) {
            pid_t res = waitpid(pid, &status, WNOHANG);
            if (res == pid) {
                return;
            }
            if (res == -1) {
                logErrno("waitpid for " + name + " failed");
                return;
            }
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= 5) {
                kill(pid, SIGKILL);
                logEvent(ids.logQueue, Role::Director, 0, "Force killed " + name);
                waitpid(pid, nullptr, 0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };

    // Run until user interruption (Ctrl+C). SOR works "24/7" until SIGUSR2 requested.
    const int chunkMs = 100;
    RandomGenerator directorRng(static_cast<unsigned int>(std::time(nullptr)));
    int sigusr1CooldownMs = 1000; // attempt SIGUSR1 roughly every second if specialists exist
    int elapsedSinceUsr1 = 0;
    while (!stopRequested.load()) {
        usleep(static_cast<useconds_t>(chunkMs * 1000));
        elapsedSinceUsr1 += chunkMs;
        // Dynamically manage Registration2 based on waiting-room load.
        if (shared) {
            int qlen = 0;
            // Prefer real queue depth from msgctl; fallback to shared counter.
            if (ids.regQueue != -1 && msgctl(ids.regQueue, IPC_STAT, &regStats) == 0) {
                qlen = static_cast<int>(regStats.msg_qnum);
            }
            stateSemGuard.wait();
            int sharedLen = shared->queueRegistrationLen;
            int reg2Flag = shared->reg2Active;
            int waitingRoomLoad = shared->currentInWaitingRoom;
            stateSemGuard.post();
            if (sharedLen > qlen) qlen = sharedLen;
            int openThreshold = std::max(config.K_registrationThreshold, config.N_waitingRoom / 2);
            int closeThreshold = config.N_waitingRoom / 3;
            if (!reg2Flag && waitingRoomLoad >= openThreshold) {
                pid_t pid = fork();
                if (pid == 0) {
                    std::vector<char*> args;
                    args.push_back(const_cast<char*>(selfPath.c_str()));
                    args.push_back(const_cast<char*>("registration2"));
                    args.push_back(const_cast<char*>(selfPath.c_str()));
                    args.push_back(nullptr);
                    execv(selfPath.c_str(), args.data());
                    logErrno("execv for registration2 failed");
                    _exit(1);
                } else if (pid > 0) {
                    reg2Pid = pid;
                    stateSemGuard.wait();
                    shared->reg2Active = 1;
                    shared->registration2Pid = pid;
                    stateSemGuard.post();
                    logEvent(ids.logQueue, Role::Director, 0,
                             "Registration2 spawned (waitingRoom=" + std::to_string(waitingRoomLoad) +
                             "/" + std::to_string(config.N_waitingRoom) +
                             " regQ=" + std::to_string(qlen) + ")");
                }
            } else if (reg2Flag && waitingRoomLoad < closeThreshold) {
                if (reg2Pid > 0) {
                    kill(reg2Pid, SIGUSR2);
                    logEvent(ids.logQueue, Role::Director, 0,
                             "Registration2 closing (waitingRoom=" + std::to_string(waitingRoomLoad) +
                             "/" + std::to_string(config.N_waitingRoom) + ")");
                    waitWithTimeout(reg2Pid, "registration2");
                    reg2Pid = -1;
                }
                stateSemGuard.wait();
                shared->reg2Active = 0;
                shared->registration2Pid = 0;
                stateSemGuard.post();
            }
        }
        if (!specialistPids.empty() && elapsedSinceUsr1 >= sigusr1CooldownMs) {
            elapsedSinceUsr1 = 0;
            int roll = directorRng.uniformInt(0, 99);
            if (roll < 5) { // ~5% chance each second
                pid_t target = specialistPids[directorRng.uniformInt(0, static_cast<int>(specialistPids.size()) - 1)];
                if (target > 0) {
                    kill(target, SIGUSR1);
                    logEvent(ids.logQueue, Role::Director, 0, "Director sent SIGUSR1 to specialist pid=" + std::to_string(target));
                }
            }
        }
    }

    logEvent(ids.logQueue, Role::Director, 0, "Director received stop request, broadcasting SIGUSR2");
    // Coordinated shutdown: send SIGUSR2 individually (process group removed for portability).
    logEvent(ids.logQueue, Role::Director, 0, "Director initiating shutdown (SIGUSR2 to children)");
    if (reg1Pid > 0) kill(reg1Pid, SIGUSR2);
    if (reg2Pid > 0) kill(reg2Pid, SIGUSR2);
    if (triagePid > 0) kill(triagePid, SIGUSR2);
    for (pid_t pid : specialistPids) {
        if (pid > 0) kill(pid, SIGUSR2);
    }
    if (generatorPid > 0) kill(generatorPid, SIGUSR2);

    // send termination marker for logger
    if (ok) {
        logEvent(ids.logQueue, Role::Director, 0, "END");
    }

    waitWithTimeout(reg1Pid, "registration");
    waitWithTimeout(reg2Pid, "registration2");
    waitWithTimeout(triagePid, "triage");
    for (pid_t pid : specialistPids) {
        waitWithTimeout(pid, "specialist");
    }
    waitWithTimeout(generatorPid, "patient_generator");
    waitWithTimeout(loggerPid, "logger");

    int status = 0;

    destroyIpc(ids, shared);

    if (!ok) {
        return 1;
    }
    if (loggerPid > 0 && WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return ok ? 0 : 1;
}
