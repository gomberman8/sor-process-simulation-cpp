#include "roles/specialist.hpp"

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
std::atomic<bool> pausedFlag(false);
std::atomic<bool> sigusr2Seen(false);

void handleSigusr2(int) { stopFlag.store(true); }
void handleSigusr1(int) { pausedFlag.store(true); }

std::string specToString(SpecialistType t) {
    switch (t) {
        case SpecialistType::Cardiologist: return "Cardiologist";
        case SpecialistType::Neurologist: return "Neurologist";
        case SpecialistType::Ophthalmologist: return "Ophthalmologist";
        case SpecialistType::Laryngologist: return "Laryngologist";
        case SpecialistType::Surgeon: return "Surgeon";
        case SpecialistType::Paediatrician: return "Paediatrician";
        default: return "Unknown";
    }
}

Role roleForType(SpecialistType t) {
    switch (t) {
        case SpecialistType::Cardiologist: return Role::SpecialistCardio;
        case SpecialistType::Neurologist: return Role::SpecialistNeuro;
        case SpecialistType::Ophthalmologist: return Role::SpecialistOphthalmo;
        case SpecialistType::Laryngologist: return Role::SpecialistLaryng;
        case SpecialistType::Surgeon: return Role::SpecialistSurgeon;
        case SpecialistType::Paediatrician: return Role::SpecialistPaediatric;
        default: return Role::SpecialistCardio;
    }
}

long maxMsgTypeForSpec(SpecialistType t) {
    // base + specialist*10 + maxPriority(3)
    return static_cast<long>(EventType::PatientToSpecialist) + static_cast<int>(t) * 10 + 3;
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

int Specialist::run(const std::string& keyPath, SpecialistType type) {
    // Ignore SIGINT so only SIGUSR2/SIGUSR1 manage lifecycle.
    struct sigaction saIgnore {};
    saIgnore.sa_handler = SIG_IGN;
    sigemptyset(&saIgnore.sa_mask);
    saIgnore.sa_flags = 0;
    sigaction(SIGINT, &saIgnore, nullptr);

    struct sigaction sa1 {};
    sa1.sa_handler = handleSigusr1;
    sigemptyset(&sa1.sa_mask);
    sa1.sa_flags = 0;
    sigaction(SIGUSR1, &sa1, nullptr);

    struct sigaction sa2 {};
    sa2.sa_handler = handleSigusr2;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGUSR2, &sa2, nullptr);

    MessageQueue specQueue;
    MessageQueue logQueue;
    Semaphore stateSem;
    Semaphore waitSem;
    SharedMemory shm;

    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
    key_t specKey = ftok(keyPath.c_str(), 'S');
    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t semStateKey = ftok(keyPath.c_str(), 'M');
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t shmKey = ftok(keyPath.c_str(), 'H');

    if (regKey == -1 || triKey == -1 || specKey == -1 || logKey == -1 ||
        semStateKey == -1 || waitKey == -1 || shmKey == -1) {
        logErrno("Specialist ftok failed");
        return 1;
    }
    if (!specQueue.open(specKey) || !logQueue.open(logKey)) {
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

    int registrationQueueId = -1;
    int triageQueueId = -1;
    if (regKey != -1) {
        registrationQueueId = msgget(regKey, 0);
    }
    if (triKey != -1) {
        triageQueueId = msgget(triKey, 0);
    }
    setLogMetricsContext({statePtr, registrationQueueId, triageQueueId, specQueue.id(),
                          waitSem.id(), stateSem.id()});

    Role asRole = roleForType(type);
    int simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), asRole, simTime, "Specialist " + specToString(type) + " started");
    RandomGenerator rng;

    while (!stopFlag.load()) {
        if (pausedFlag.load()) {
            int pauseMs = rng.uniformInt(100, 500);
            usleep(static_cast<useconds_t>(pauseMs * 1000));
            pausedFlag.store(false);
            simTime = currentSimMinutes(statePtr);
            logEvent(logQueue.id(), asRole, simTime, "SIGUSR1: temporary leave finished");
        }

        EventMessage ev{};
        long maxType = maxMsgTypeForSpec(type);
        // Negative msgtyp picks the lowest mtype <= |msgtyp|, giving priority to red/yellow over green.
        ssize_t res = msgrcv(specQueue.id(), &ev, sizeof(EventMessage) - sizeof(long),
                             -maxType, 0);
        if (res == -1) {
            if ((errno == EINTR && stopFlag.load()) || errno == EIDRM || errno == EINVAL) {
                break;
            }
            continue;
        }

        simTime = currentSimMinutes(statePtr);
        logEvent(logQueue.id(), asRole, simTime,
                 "Received patient id=" + std::to_string(ev.patientId) +
                 " color=" + std::to_string(ev.triageColor) +
                 " persons=" + std::to_string(ev.personsCount));

        // Simulate exam; slower to allow queues to build (registration2 logic to kick in later).
        int examMs = rng.uniformInt(10, 40);
        usleep(static_cast<useconds_t>(examMs * 1000));

        int outcomeRand = rng.uniformInt(0, 999);
        stateSem.wait();
        if (outcomeRand < 850) {
            statePtr->outcomeHome += 1;
        } else if (outcomeRand < 995) {
            statePtr->outcomeWard += 1;
        } else {
            statePtr->outcomeOther += 1;
        }
        stateSem.post();

        std::string outcomeText;
        if (outcomeRand < 850) outcomeText = "home";
        else if (outcomeRand < 995) outcomeText = "ward";
        else outcomeText = "otherFacility";

        simTime = currentSimMinutes(statePtr);
        logEvent(logQueue.id(), asRole, simTime,
                 "Handled patient id=" + std::to_string(ev.patientId) +
                 " outcome=" + outcomeText +
                 " persons=" + std::to_string(ev.personsCount) +
                 " color=" + std::to_string(ev.triageColor) +
                 " specIdx=" + std::to_string(ev.specialistIdx));
    }

    simTime = currentSimMinutes(statePtr);
    if (sigusr2Seen.load()) {
        logEvent(logQueue.id(), asRole, simTime, "Specialist shutting down (SIGUSR2)");
    } else {
        logEvent(logQueue.id(), asRole, simTime, "Specialist shutting down");
    }
    shm.detach(statePtr);
    return 0;
}
