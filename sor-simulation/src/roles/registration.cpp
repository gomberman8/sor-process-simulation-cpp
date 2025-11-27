#include "roles/registration.hpp"

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
#include <cstring>
#include <string>
#include <ctime>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

namespace {
std::atomic<bool> stopFlag(false);
std::atomic<bool> sigusr2Seen(false);

void handleSigusr2(int) {
    stopFlag.store(true);
    sigusr2Seen.store(true);
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

int Registration::run(const std::string& keyPath, bool isSecond) {
    // Ignore SIGINT so only SIGUSR2 triggers shutdown.
    struct sigaction saIgnore {};
    saIgnore.sa_handler = SIG_IGN;
    sigemptyset(&saIgnore.sa_mask);
    saIgnore.sa_flags = 0;
    sigaction(SIGINT, &saIgnore, nullptr);

    // Install SIGUSR2 handler for shutdown.
    struct sigaction sa {};
    sa.sa_handler = handleSigusr2;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);

    // Open existing IPC objects using same ftok keys as Director.
    MessageQueue regQueue;
    MessageQueue triageQueue;
    MessageQueue logQueue;
    Semaphore stateSem;
    SharedMemory shm;

    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t semStateKey = ftok(keyPath.c_str(), 'M');
    key_t shmKey = ftok(keyPath.c_str(), 'H');

    if (regKey == -1 || triKey == -1 || logKey == -1 || semStateKey == -1 || shmKey == -1) {
        logErrno("Registration ftok failed");
        return 1;
    }
    if (!regQueue.open(regKey) || !triageQueue.open(triKey) || !logQueue.open(logKey)) {
        return 1;
    }
    if (!stateSem.open(semStateKey)) {
        return 1;
    }
    if (!shm.open(shmKey)) {
        return 1;
    }
    auto* statePtr = static_cast<SharedState*>(shm.attach());
    if (!statePtr) {
        return 1;
    }

    Role myRole = isSecond ? Role::Registration2 : Role::Registration1;
    // Log includes PID via logger; message text focuses on patient ids/flags.
    int simTime = currentSimMinutes(statePtr);
    logEvent(logQueue.id(), myRole, simTime, isSecond ? "Registration2 started" : "Registration started");

    while (!stopFlag.load()) {
        EventMessage ev{};
        long baseType = static_cast<long>(EventType::PatientArrived);
        // Use negative msgtyp to prioritize lower mtype (VIP first).
        ssize_t res = msgrcv(regQueue.id(), &ev, sizeof(EventMessage) - sizeof(long),
                             -(baseType + 1), 0); // accept both VIP (1) and normal (2)
        if (res == -1) {
            if ((errno == EINTR && stopFlag.load()) || errno == EIDRM || errno == EINVAL) {
                break;
            }
            logErrno("Registration msgrcv failed");
            continue;
        }

        // Update shared queue length (simple decrement) under semaphore.
        stateSem.wait();
        if (statePtr->queueRegistrationLen > 0) {
            statePtr->queueRegistrationLen -= 1;
        }
        stateSem.post();

        // Simulate service time to allow queue buildup (and potential reg2 activation).
        usleep(100 * 1000); // 100ms per registration

        // Forward to triage queue.
        ev.mtype = static_cast<long>(EventType::PatientRegistered);
        // Non-blocking send with retry to avoid stalling when triage queue is full.
        size_t payloadSize = sizeof(EventMessage) - sizeof(long);
        bool sent = false;
        while (!sent) {
            if (msgsnd(triageQueue.id(), &ev, payloadSize, IPC_NOWAIT) == 0) {
                sent = true;
                break;
            }
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            logErrno("Registration send to triage failed");
            break;
        }
        if (sent) {
            simTime = currentSimMinutes(statePtr);
            logEvent(logQueue.id(), myRole, simTime,
                     "Forwarded patient id=" + std::to_string(ev.patientId) +
                     " vip=" + std::to_string(ev.isVip) +
                     " persons=" + std::to_string(ev.personsCount));
        }
    }

    simTime = currentSimMinutes(statePtr);
    if (sigusr2Seen.load()) {
        logEvent(logQueue.id(), myRole, simTime, isSecond ? "Registration2 shutting down (SIGUSR2)" : "Registration shutting down (SIGUSR2)");
    } else {
        logEvent(logQueue.id(), myRole, simTime, isSecond ? "Registration2 shutting down" : "Registration shutting down");
    }
    shm.detach(statePtr);
    return 0;
}
