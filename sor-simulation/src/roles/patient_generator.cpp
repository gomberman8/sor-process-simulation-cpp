#include "roles/patient_generator.hpp"

#include "logging/logger.hpp"
#include "ipc/message_queue.hpp"
#include "ipc/shared_memory.hpp"
#include "ipc/semaphore.hpp"
#include "model/config.hpp"
#include "model/types.hpp"
#include "model/shared_state.hpp"
#include "roles/patient.hpp"
#include "util/error.hpp"
#include "util/random.hpp"

#include <atomic>
#include <csignal>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ipc.h>

namespace {
std::atomic<bool> stopFlag(false);
std::atomic<bool> sigusr2Seen(false);

void handleSigusr2(int) {
    stopFlag.store(true);
    sigusr2Seen.store(true);
}
} // namespace

int PatientGenerator::run(const std::string& keyPath, const Config& cfg) {
    struct sigaction sa {};
    sa.sa_handler = handleSigusr2;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);

    RandomGenerator rng(cfg.randomSeed);
    int spawned = 0;
    const bool infiniteFlow = (cfg.totalPatientsTarget <= 0);
    // Log current mode for clarity during long runs.
    int logId = -1;
    key_t logKey = ftok(keyPath.c_str(), 'L');
    MessageQueue logQueue;
    if (logKey != -1 && logQueue.open(logKey)) {
        logId = logQueue.id();
    }

    // Access shared state to read waiting room occupancy for backpressure.
    SharedMemory shm;
    Semaphore stateSem;
    SharedState* statePtr = nullptr;
    key_t shmKey = ftok(keyPath.c_str(), 'H');
    key_t semKey = ftok(keyPath.c_str(), 'M');
    if (shmKey != -1 && semKey != -1 && shm.open(shmKey) && stateSem.open(semKey)) {
        statePtr = static_cast<SharedState*>(shm.attach());
    }
    if (logId != -1) {
        logEvent(logId, Role::PatientGenerator, 0,
                 infiniteFlow ? "PatientGenerator running in infinite mode (until SIGUSR2)"
                              : "PatientGenerator target=" + std::to_string(cfg.totalPatientsTarget));
    }
    std::vector<pid_t> children;

    while (!stopFlag.load() && (infiniteFlow || spawned < cfg.totalPatientsTarget)) {
        // Backpressure: avoid exceeding system process limits and waiting-room capacity.
        const size_t maxChildren = 200; // cap concurrent patient processes to avoid fork errors
        while (!stopFlag.load() && children.size() >= maxChildren) {
            // Reap some children to free slots
            for (auto it = children.begin(); it != children.end();) {
                pid_t cpid = *it;
                if (cpid > 0) {
                    pid_t res = waitpid(cpid, nullptr, WNOHANG);
                    if (res == cpid) {
                        it = children.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            if (children.size() >= maxChildren) {
                usleep(50 * 1000); // brief pause before retrying
            }
        }
        if (stopFlag.load()) break;

        // If waiting room is full, slow down generation.
        if (statePtr) {
            stateSem.wait();
            int inside = statePtr->currentInWaitingRoom;
            int capacity = statePtr->waitingRoomCapacity;
            stateSem.post();
            if (inside >= capacity) {
                usleep(20 * 1000);
                continue;
            }
        }

        int age = rng.uniformInt(1, 90);
        bool hasGuardian = age < 18;
        int personsCount = hasGuardian ? 2 : 1;
        bool isVip = rng.uniformInt(0, 99) < 10; // ~10% VIP

        pid_t pid = fork();
        if (pid == -1) {
            // Fork can fail if zombie children accumulate or system is at process limit.
            if (logId != -1) {
                logEvent(logId, Role::PatientGenerator, 0, "PatientGenerator fork failed, backing off");
            } else {
                logErrno("PatientGenerator fork failed");
            }
            usleep(100 * 1000); // brief backoff
            continue;
        } else if (pid == 0) {
            // exec self in patient mode: argv: [exe, "patient", keyPath, id, age, isVip, hasGuardian, personsCount]
            std::string idStr = std::to_string(spawned + 1);
            std::string ageStr = std::to_string(age);
            std::string vipStr = isVip ? "1" : "0";
            std::string guardianStr = hasGuardian ? "1" : "0";
            std::string personsStr = std::to_string(personsCount);

            std::vector<char*> args;
            args.push_back(const_cast<char*>(keyPath.c_str())); // executable path
            args.push_back(const_cast<char*>("patient"));
            args.push_back(const_cast<char*>(keyPath.c_str()));
            args.push_back(const_cast<char*>(idStr.c_str()));
            args.push_back(const_cast<char*>(ageStr.c_str()));
            args.push_back(const_cast<char*>(vipStr.c_str()));
            args.push_back(const_cast<char*>(guardianStr.c_str()));
            args.push_back(const_cast<char*>(personsStr.c_str()));
            args.push_back(nullptr);
            execv(keyPath.c_str(), args.data());
            logErrno("execv patient failed");
            _exit(1);
        }

        children.push_back(pid);
        spawned++;
        int sleepMs = cfg.timeScaleMsPerSimMinute;
        usleep(static_cast<useconds_t>(sleepMs * 1000));

        // Reap finished children to avoid zombies and fork failures during long runs.
        for (auto it = children.begin(); it != children.end();) {
            pid_t cpid = *it;
            if (cpid > 0) {
                pid_t res = waitpid(cpid, nullptr, WNOHANG);
                if (res == cpid) {
                    it = children.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    // On shutdown or completion, signal remaining children and wait.
    for (pid_t c : children) {
        if (c > 0) {
            kill(c, SIGUSR2);
        }
    }
    for (pid_t c : children) {
        if (c > 0) {
            waitpid(c, nullptr, 0);
        }
    }
    if (logId != -1) {
        logEvent(logId, Role::PatientGenerator, 0,
                 sigusr2Seen.load() ? "PatientGenerator stopping (SIGUSR2)" : "PatientGenerator stopping");
    }
    return 0;
}
