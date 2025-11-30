#include "roles/patient.hpp"

#include "ipc/message_queue.hpp"
#include "ipc/semaphore.hpp"
#include "ipc/shared_memory.hpp"
#include "logging/logger.hpp"
#include "model/events.hpp"
#include "model/shared_state.hpp"
#include "model/types.hpp"
#include "util/error.hpp"

#include <atomic>
#include <csignal>
#include <pthread.h>
#include <cstring>
#include <string>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ctime>
#include <unistd.h>

namespace {
std::atomic<bool> stopFlag(false);

void handleSigusr2(int) {
    stopFlag.store(true);
}

long long monotonicMs() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) return 0;
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

int currentSimMinutes(const SharedState* state) {
    if (!state || state->timeScaleMsPerSimMinute <= 0) return 0;
    long long now = monotonicMs();
    long long delta = now - state->simStartMonotonicMs;
    if (delta < 0) delta = 0;
    return static_cast<int>(delta / state->timeScaleMsPerSimMinute);
}

struct ChildArgs {
    int logQueueId;
    int patientId;
    std::atomic<bool>* stopFlagPtr;
    const SharedState* shared;
};

void* childThreadMain(void* arg) {
    ChildArgs* args = static_cast<ChildArgs*>(arg);
    // Child shares waiting room with guardian; just log presence and wait for stop.
    logEvent(args->logQueueId, Role::Patient, currentSimMinutes(args->shared),
             "Child thread active for patient id=" + std::to_string(args->patientId));
    while (!args->stopFlagPtr->load()) {
        usleep(50 * 1000); // short sleep to avoid busy loop
    }
    logEvent(args->logQueueId, Role::Patient, currentSimMinutes(args->shared),
             "Child thread exiting for patient id=" + std::to_string(args->patientId));
    return nullptr;
}
} // namespace

int Patient::run(const std::string& keyPath, int patientId, int age, bool isVip, bool hasGuardian, int personsCount) {
    // Ignore SIGINT so only SIGUSR2 controls shutdown.
    struct sigaction saIgnore {};
    saIgnore.sa_handler = SIG_IGN;
    sigemptyset(&saIgnore.sa_mask);
    saIgnore.sa_flags = 0;
    sigaction(SIGINT, &saIgnore, nullptr);

    struct sigaction sa {};
    sa.sa_handler = handleSigusr2;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);

    MessageQueue regQueue;
    MessageQueue logQueue;
    Semaphore waitSem;
    Semaphore stateSem;
    SharedMemory shm;

    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
    key_t specKey = ftok(keyPath.c_str(), 'S');
    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t stateKey = ftok(keyPath.c_str(), 'M');
    key_t shmKey = ftok(keyPath.c_str(), 'H');

    if (regKey == -1 || triKey == -1 || specKey == -1 || logKey == -1 ||
        waitKey == -1 || stateKey == -1 || shmKey == -1) {
        logErrno("Patient ftok failed");
        return 1;
    }
    if (!regQueue.open(regKey) || !logQueue.open(logKey)) {
        return 1;
    }
    if (!waitSem.open(waitKey) || !stateSem.open(stateKey)) {
        return 1;
    }
    if (!shm.open(shmKey)) {
        return 1;
    }
    auto* statePtr = static_cast<SharedState*>(shm.attach());
    if (!statePtr) {
        return 1;
    }

    int triageQueueId = -1;
    int specialistsQueueId = -1;
    if (triKey != -1) {
        triageQueueId = msgget(triKey, 0);
    }
    if (specKey != -1) {
        specialistsQueueId = msgget(specKey, 0);
    }
    setLogMetricsContext({statePtr, regQueue.id(), triageQueueId, specialistsQueueId,
                          waitSem.id(), stateSem.id()});

    // Spawn a lightweight thread to model the child presence (if any).
    pthread_t childThread{};
    ChildArgs* childArgs = nullptr;
    std::atomic<bool> childStop(false);
    bool childStarted = false;
    if (hasGuardian && personsCount == 2) {
        childArgs = new ChildArgs{logQueue.id(), patientId, &childStop, statePtr};
        if (pthread_create(&childThread, nullptr, childThreadMain, childArgs) == 0) {
            childStarted = true;
        } else {
            // If thread fails, continue as single process but log the failure.
            logErrno("Failed to start child thread");
            delete childArgs;
            childArgs = nullptr;
        }
    }

    // Log that patient is queued outside waiting for a slot.
    int simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), Role::Patient, simTime,
             "Patient waiting to enter waiting room id=" + std::to_string(patientId) +
             " persons=" + std::to_string(personsCount));

    // Acquire waiting room slots
    for (int i = 0; i < personsCount; ++i) {
        if (!waitSem.wait()) {
            shm.detach(statePtr);
            return 1;
        }
    }

    // Update shared state: inside count and queue len
    stateSem.wait();
    statePtr->currentInWaitingRoom += personsCount;
    statePtr->queueRegistrationLen += 1;
    statePtr->totalPatients += 1;
    stateSem.post();

    simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), Role::Patient, simTime,
             "Patient arrived id=" + std::to_string(patientId) +
             " age=" + std::to_string(age) +
             " vip=" + std::string(isVip ? "1" : "0") +
             " persons=" + std::to_string(personsCount) +
             " guardian=" + std::string(hasGuardian ? "1" : "0"));

    EventMessage ev{};
    long baseType = static_cast<long>(EventType::PatientArrived);
    // VIPs use lower mtype to be dequeued first with negative msgtyp in msgrcv.
    ev.mtype = isVip ? baseType : baseType + 1;
    ev.patientId = patientId;
    ev.age = age;
    ev.isVip = isVip ? 1 : 0;
    ev.personsCount = personsCount;
    std::strncpy(ev.extra, hasGuardian ? "guardian" : "solo", sizeof(ev.extra) - 1);

    // Non-blocking send with retry to avoid deadlock if registration queue temporarily full.
    size_t payloadSize = sizeof(EventMessage) - sizeof(long);
    while (true) {
        if (msgsnd(regQueue.id(), &ev, payloadSize, IPC_NOWAIT) == 0) {
            break;
        }
        if (errno == EAGAIN) {
            usleep(1000);
            continue;
        }
        logErrno("Patient send to registration failed");
        break;
    }

    // Patient process ends; waiting room slots will be released once registration forwards the patient.
    simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), Role::Patient, simTime, "Patient registered id=" + std::to_string(patientId));

    // Stop child thread if it was started.
    if (childStarted) {
        childStop.store(true);
        pthread_join(childThread, nullptr);
        delete childArgs;
    }
    shm.detach(statePtr);
    return 0;
}
