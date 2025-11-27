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
} // namespace

int Specialist::run(const std::string& keyPath, SpecialistType type) {
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

    key_t specKey = ftok(keyPath.c_str(), 'S');
    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t semStateKey = ftok(keyPath.c_str(), 'M');
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t shmKey = ftok(keyPath.c_str(), 'H');

    if (specKey == -1 || logKey == -1 || semStateKey == -1 || waitKey == -1 || shmKey == -1) {
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

    Role asRole = roleForType(type);
    logEvent(logQueue.id(), asRole, 0, "Specialist " + specToString(type) + " started");
    RandomGenerator rng;

    while (!stopFlag.load()) {
        if (pausedFlag.load()) {
            int pauseMs = rng.uniformInt(100, 500);
            usleep(static_cast<useconds_t>(pauseMs * 1000));
            pausedFlag.store(false);
            logEvent(logQueue.id(), asRole, 0, "SIGUSR1: temporary leave finished");
        }

        EventMessage ev{};
        long baseType = static_cast<long>(EventType::PatientToSpecialist);
        long myType = baseType + static_cast<int>(type);
        ssize_t res = msgrcv(specQueue.id(), &ev, sizeof(EventMessage) - sizeof(long),
                             myType, 0);
        if (res == -1) {
            if ((errno == EINTR && stopFlag.load()) || errno == EIDRM || errno == EINVAL) {
                break;
            }
            continue;
        }

        logEvent(logQueue.id(), asRole, 0,
                 "Received patient id=" + std::to_string(ev.patientId) +
                 " color=" + std::to_string(ev.triageColor) +
                 " persons=" + std::to_string(ev.personsCount));

        // Simulate exam; slower to allow queues to build (registration2 logic to kick in later).
        int examMs = rng.uniformInt(50, 200);
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
        // Patient leaves waiting room regardless of outcome.
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

        std::string outcomeText;
        if (outcomeRand < 850) outcomeText = "home";
        else if (outcomeRand < 995) outcomeText = "ward";
        else outcomeText = "otherFacility";

        logEvent(logQueue.id(), asRole, 0,
                 "Handled patient id=" + std::to_string(ev.patientId) +
                 " outcome=" + outcomeText +
                 " persons=" + std::to_string(ev.personsCount) +
                 " color=" + std::to_string(ev.triageColor) +
                 " specIdx=" + std::to_string(ev.specialistIdx) +
                 " waitingRoom=" + std::to_string(inside) + "/" + std::to_string(capacity));
    }

    if (sigusr2Seen.load()) {
        logEvent(logQueue.id(), asRole, 0, "Specialist shutting down (SIGUSR2)");
    } else {
        logEvent(logQueue.id(), asRole, 0, "Specialist shutting down");
    }
    shm.detach(statePtr);
    return 0;
}
