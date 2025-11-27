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

TriageColor pickColor(RandomGenerator& rng) {
    int r = rng.uniformInt(0, 99);
    if (r < 10) return TriageColor::Red;
    if (r < 45) return TriageColor::Yellow;
    return TriageColor::Green;
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
} // namespace

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
    MessageQueue specQueue;
    MessageQueue logQueue;
    Semaphore stateSem;
    Semaphore waitSem;
    SharedMemory shm;

    key_t triKey = ftok(keyPath.c_str(), 'T');
    key_t specKey = ftok(keyPath.c_str(), 'S');
    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t semStateKey = ftok(keyPath.c_str(), 'M');
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t shmKey = ftok(keyPath.c_str(), 'H');

    if (triKey == -1 || specKey == -1 || logKey == -1 || semStateKey == -1 || waitKey == -1 || shmKey == -1) {
        logErrno("Triage ftok failed");
        return 1;
    }
    if (!triageQueue.open(triKey) || !specQueue.open(specKey) || !logQueue.open(logKey)) {
        return 1;
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

    int simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), Role::Triage, simTime, "Triage started");
    RandomGenerator rng;

    while (!stopFlag.load()) {
        EventMessage ev{};
        ssize_t res = msgrcv(triageQueue.id(), &ev, sizeof(EventMessage) - sizeof(long),
                             0, 0);
        if (res == -1) {
            if ((errno == EINTR && stopFlag.load()) || errno == EIDRM || errno == EINVAL) {
                break;
            }
            logErrno("Triage msgrcv failed");
            continue;
        }

        // 5% send home directly
        int rHome = rng.uniformInt(0, 99);
        stateSem.wait();
        if (rHome < 5) {
            statePtr->triageSentHome += 1;
            // free waiting room slots for this patient
            if (statePtr->currentInWaitingRoom >= ev.personsCount) {
                statePtr->currentInWaitingRoom -= ev.personsCount;
            } else {
                statePtr->currentInWaitingRoom = 0;
            }
            int inside = statePtr->currentInWaitingRoom;
            int capacity = statePtr->waitingRoomCapacity;
            stateSem.post();
            for (int i = 0; i < ev.personsCount; ++i) {
                waitSem.post();
            }
            simTime = currentSimMinutes(statePtr);
            logEvent(logQueue.id(), Role::Triage, simTime,
                     "Patient sent home from triage id=" + std::to_string(ev.patientId) +
                     " waitingRoom=" + std::to_string(inside) + "/" + std::to_string(capacity));
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

        ev.mtype = static_cast<long>(EventType::PatientToSpecialist);
        ev.specialistIdx = static_cast<int>(spec);
        ev.triageColor = static_cast<int>(color);

        long routedType = ev.mtype + ev.specialistIdx; // per-specialist mtype
        // Non-blocking send with retry to avoid stalling if specialist queue is momentarily full.
        size_t payloadSize = sizeof(EventMessage) - sizeof(long);
        bool sent = false;
        while (!sent) {
            if (msgsnd(specQueue.id(), &ev, payloadSize, IPC_NOWAIT) == 0) {
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
