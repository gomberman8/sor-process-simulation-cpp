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
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

namespace {
std::atomic<bool> stopFlag(false);

void handleSigusr2(int) {
    stopFlag.store(true);
}
} // namespace

int Registration::run(const std::string& keyPath) {
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

    logEvent(logQueue.id(), Role::Registration1, 0, "Registration started");

    while (!stopFlag.load()) {
        EventMessage ev{};
        ssize_t res = msgrcv(regQueue.id(), &ev, sizeof(EventMessage) - sizeof(long),
                             0, 0);
        if (res == -1) {
            if (errno == EINTR && stopFlag.load()) {
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

        // Forward to triage queue.
        ev.mtype = static_cast<long>(EventType::PatientRegistered);
        if (!triageQueue.send(&ev, sizeof(ev), ev.mtype)) {
            logErrno("Registration send to triage failed");
        } else {
            logEvent(logQueue.id(), Role::Registration1, 0, "Forwarded patient to triage");
        }
    }

    logEvent(logQueue.id(), Role::Registration1, 0, "Registration shutting down");
    shm.detach(statePtr);
    return 0;
}
