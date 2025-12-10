#include "roles/triage.hpp"

#include "ipc/message_queue.hpp"
#include "ipc/semaphore.hpp"
#include "ipc/shared_memory.hpp"
#include "logging/logger.hpp"
#include "model/events.hpp"
#include "model/shared_state.hpp"
#include "model/types.hpp"
#include "util/error.hpp"
#include "util/random.hpp"

#include <array>
#include <atomic>
#include <csignal>
#include <cstring>
#include <string>
#include <ctime>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>


namespace {
std::atomic<bool> stopFlag(false);
std::atomic<bool> sigusr2Seen(false);

void handleSigusr2(int) {
    stopFlag.store(true);
    sigusr2Seen.store(true);
}

/** @brief Uniformly pick a specialist type. */
SpecialistType pickSpecialist(RandomGenerator& rng) {
    int r = rng.uniformInt(0, 5);
    switch (r) {
        case 0: return SpecialistType::Cardiologist;
        case 1: return SpecialistType::Neurologist;
        case 2: return SpecialistType::Ophthalmologist;
        case 3: return SpecialistType::Laryngologist;
        case 4: return SpecialistType::Surgeon;
        case 5: return SpecialistType::Paediatrician;
        default: return SpecialistType::None;
    }
}

/** @brief Pick triage color with weighted probabilities. */
TriageColor pickColor(RandomGenerator& rng) {
    int r = rng.uniformInt(0, 99);
    if (r < 10) return TriageColor::Red;
    if (r < 45) return TriageColor::Yellow;
    return TriageColor::Green;
}

/** @brief Priority ordering for colors (lower is higher priority). */
int colorPriority(TriageColor c) {
    switch (c) {
        case TriageColor::Red: return 1;    // highest priority
        case TriageColor::Yellow: return 2; // medium
        case TriageColor::Green: return 3;  // lowest
        default: return 3;
    }
}

/** @brief Monotonic clock in milliseconds (best effort). */
long long monotonicMs() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) return 0;
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

/** @brief Simulation minutes derived from shared state start/time scale. */
int currentSimMinutes(const SharedState* state) {
    if (!state || state->timeScaleMsPerSimMinute <= 0) return 0;
    long long now = monotonicMs();
    long long delta = now - state->simStartMonotonicMs;
    if (delta < 0) delta = 0;
    return static_cast<int>(delta / state->timeScaleMsPerSimMinute);
}
} // namespace

// Triage loop entry (see header for details).
int Triage::run(const std::string& keyPath) {
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

    MessageQueue triageQueue;
    std::array<MessageQueue, kSpecialistCount> specQueues;
    MessageQueue logQueue;
    Semaphore stateSem;
    Semaphore waitSem;
    SharedMemory shm;

    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
    std::array<key_t, kSpecialistCount> specKeys;
    for (int i = 0; i < kSpecialistCount; ++i) {
        specKeys[i] = ftok(keyPath.c_str(), 'A' + i);
    }
    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t semStateKey = ftok(keyPath.c_str(), 'M');
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t shmKey = ftok(keyPath.c_str(), 'H');

    if (regKey == -1 || triKey == -1 || logKey == -1 ||
        semStateKey == -1 || waitKey == -1 || shmKey == -1) {
        logErrno("Triage ftok failed");
        return 1;
    }
    for (int i = 0; i < kSpecialistCount; ++i) {
        if (specKeys[i] == -1) {
            logErrno("Triage ftok failed");
            return 1;
        }
    }
    if (!triageQueue.open(triKey) || !logQueue.open(logKey)) {
        return 1;
    }
    std::array<int, kSpecialistCount> specQueueIds;
    specQueueIds.fill(-1);
    for (int i = 0; i < kSpecialistCount; ++i) {
        if (!specQueues[i].open(specKeys[i])) {
            logErrno("Triage spec queue open failed");
            return 1;
        }
        specQueueIds[i] = specQueues[i].id();
    }
    if (!stateSem.open(semStateKey) || !waitSem.open(waitKey)) {
        return 1;
    }
    if (!shm.open(shmKey)) {
        return 1;
    }
    auto* statePtr = static_cast<SharedState*>(shm.attach());
    if (!statePtr) {
        return 1;
    }
    int triageServiceMs = statePtr->triageServiceMs;
    if (triageServiceMs < 0) triageServiceMs = 0;

    int registrationQueueId = -1;
    if (regKey != -1) {
        registrationQueueId = msgget(regKey, 0);
    }
    setLogMetricsContext({statePtr, registrationQueueId, triageQueue.id(), specQueueIds,
                          waitSem.id(), stateSem.id()});

    int simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), Role::Triage, simTime, "Triage started");
    RandomGenerator rng;

    while (!stopFlag.load()) {
        EventMessage ev{};
        long baseType = static_cast<long>(EventType::PatientRegistered);
        // Negative msgtyp prioritizes lower mtype, so VIP (baseType) is dequeued before normal (baseType+1).
        ssize_t res = msgrcv(triageQueue.id(), &ev, sizeof(EventMessage) - sizeof(long),
                             -(baseType + 1), 0);
        if (res == -1) {
            if ((errno == EINTR && stopFlag.load()) || errno == EIDRM || errno == EINVAL) {
                break;
            }
            logErrno("Triage msgrcv failed");
            continue;
        }

        if (triageServiceMs > 0) {
            usleep(static_cast<useconds_t>(triageServiceMs * 1000));
        }

        // 5% send home directly
        int rHome = rng.uniformInt(0, 99);
        stateSem.wait();
        if (rHome < 5) {
            statePtr->triageSentHome += 1;
            stateSem.post();
            simTime = currentSimMinutes(statePtr);
            logEvent(logQueue.id(), Role::Triage, simTime,
                     "Patient sent home from triage id=" + std::to_string(ev.patientId));
            continue;
        }

        TriageColor color = pickColor(rng);
        switch (color) {
            case TriageColor::Red: statePtr->triageRed += 1; break;
            case TriageColor::Yellow: statePtr->triageYellow += 1; break;
            case TriageColor::Green: statePtr->triageGreen += 1; break;
            default: break;
        }
        SpecialistType spec = pickSpecialist(rng);
        stateSem.post();

        long routedType = static_cast<long>(EventType::PatientToSpecialist) +
                          static_cast<int>(spec) * 10 + colorPriority(color);
        ev.mtype = routedType;
        ev.specialistIdx = static_cast<int>(spec);
        ev.triageColor = static_cast<int>(color);

        // Non-blocking send with retry to avoid stalling if specialist queue is momentarily full.
        size_t payloadSize = sizeof(EventMessage) - sizeof(long);
        bool sent = false;
        while (!sent) {
            MessageQueue& targetQueue = specQueues[static_cast<int>(spec)];
            if (msgsnd(targetQueue.id(), &ev, payloadSize, IPC_NOWAIT) == 0) {
                sent = true;
                break;
            }
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            logErrno("Triage send to specialist failed");
            break;
        }
        if (sent) {
            simTime = currentSimMinutes(statePtr);
            logEvent(logQueue.id(), Role::Triage, simTime,
                     "Forwarded patient id=" + std::to_string(ev.patientId) +
                     " to specialist=" + std::to_string(ev.specialistIdx) +
                     " color=" + std::to_string(ev.triageColor));
        }
    }

    simTime = currentSimMinutes(statePtr);
    if (sigusr2Seen.load()) {
        logEvent(logQueue.id(), Role::Triage, simTime, "Triage shutting down (SIGUSR2)");
    } else {
        logEvent(logQueue.id(), Role::Triage, simTime, "Triage shutting down");
    }
    shm.detach(statePtr);
    return 0;
}
