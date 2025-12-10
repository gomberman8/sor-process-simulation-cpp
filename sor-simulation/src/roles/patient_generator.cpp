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

#include <array>
#include <atomic>
#include <csignal>
#include <string>
#include <vector>
#include <ctime>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>

namespace {
std::atomic<bool> stopFlag(false);
std::atomic<bool> sigusr2Seen(false);
constexpr int kDefaultTimeScaleMsPerSimMinute = 20;

void handleSigusr2(int) {
    stopFlag.store(true);
    sigusr2Seen.store(true);
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

/** @brief Real minutes elapsed since sim start (wall-clock). */
int currentRealMinutes(const SharedState* state) {
    if (!state) return 0;
    long long now = monotonicMs();
    long long delta = now - state->simStartMonotonicMs;
    if (delta < 0) delta = 0;
    return static_cast<int>(delta / 60000);
}
} // namespace

// Patient generator loop (see header for details).
int PatientGenerator::run(const std::string& keyPath, const Config& cfg) {
    // Ignore SIGINT so director controls shutdown via SIGUSR2.
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

    RandomGenerator rng(cfg.randomSeed);
    int spawned = 0;
    // Log current mode for clarity during long runs.
    int logId = -1;
    key_t logKey = ftok(keyPath.c_str(), 'L');
    MessageQueue logQueue;
    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
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

    int regQueueId = -1;
    int triQueueId = -1;
    if (regKey != -1) {
        regQueueId = msgget(regKey, 0);
    }
    if (triKey != -1) {
        triQueueId = msgget(triKey, 0);
    }
    std::array<int, kSpecialistCount> specQueueIds;
    specQueueIds.fill(-1);
    setLogMetricsContext({statePtr, regQueueId, triQueueId, specQueueIds,
                          -1, stateSem.id()});
    int simTime = currentSimMinutes(statePtr);
    if (logId != -1) {
        logEvent(logId, Role::PatientGenerator, simTime,
                 "PatientGenerator running (until SIGUSR2)");
    }
    std::vector<pid_t> children;
    bool childLimitLogged = false;
    // Reap finished children to avoid zombies and free process slots.
    auto reapChildren = [&](std::vector<pid_t>& list) {
        for (auto it = list.begin(); it != list.end();) {
            pid_t cpid = *it;
            if (cpid > 0) {
                pid_t res = waitpid(cpid, nullptr, WNOHANG);
                if (res == cpid) {
                    it = list.erase(it);
                    continue;
                }
            }
            ++it;
        }
    };
    // Scale intervals with sim speed; clamp to at least 1 ms for positive inputs.
    auto scaleInterval = [&](int baseMs) {
        if (baseMs <= 0) return cfg.timeScaleMsPerSimMinute;
        long long scaled = static_cast<long long>(baseMs) * cfg.timeScaleMsPerSimMinute /
                           kDefaultTimeScaleMsPerSimMinute;
        if (scaled <= 0) scaled = 1;
        return static_cast<int>(scaled);
    };
    int genMinMs = scaleInterval(cfg.patientGenMinMs);
    int genMaxMs = scaleInterval(cfg.patientGenMaxMs);
    if (genMaxMs < genMinMs) genMaxMs = genMinMs;

    while (!stopFlag.load()) {
        // Stop when real duration elapsed (if shared state present).
        if (statePtr && statePtr->simulationDurationMinutes > 0 &&
            currentRealMinutes(statePtr) >= statePtr->simulationDurationMinutes) {
            stopFlag.store(true);
            break;
        }

        // Backpressure: avoid exceeding system process limits and waiting-room capacity.
        const size_t maxChildren = 2000; // cap concurrent patient processes to avoid fork errors
        while (!stopFlag.load() && children.size() >= maxChildren) {
            // Reap some children to free slots
            reapChildren(children);
            if (children.size() >= maxChildren) {
                usleep(50 * 1000); // brief pause before retrying
                if (!childLimitLogged && logId != -1) {
                    simTime = currentSimMinutes(statePtr);
                    logEvent(logId, Role::PatientGenerator, simTime,
                             "PatientGenerator waiting for children slots (count=" + std::to_string(children.size()) + ")");
                }
                childLimitLogged = true;
            }
        }
        if (stopFlag.load()) break;
        if (childLimitLogged && children.size() < maxChildren) {
            childLimitLogged = false;
        }

        // If waiting room is full, slow down generation.
        int age = rng.uniformInt(1, 90);
        bool hasGuardian = age < 18;
        int personsCount = hasGuardian ? 2 : 1;
        bool isVip = rng.uniformInt(0, 99) < 10; // ~10% VIP

        pid_t pid = fork();
        if (pid == -1) {
            // Fork can fail if zombie children accumulate or system is at process limit.
            if (logId != -1) {
                simTime = currentSimMinutes(statePtr);
                logEvent(logId, Role::PatientGenerator, simTime, "PatientGenerator fork failed, backing off");
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
        int sleepMs = rng.uniformInt(genMinMs, genMaxMs);
        usleep(static_cast<useconds_t>(sleepMs * 1000));

        // Reap finished children to avoid zombies and fork failures during long runs.
        reapChildren(children);
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
        simTime = currentSimMinutes(statePtr);
        logEvent(logId, Role::PatientGenerator, simTime,
                 sigusr2Seen.load() ? "PatientGenerator stopping (SIGUSR2)" : "PatientGenerator stopping");
    }
    return 0;
}
