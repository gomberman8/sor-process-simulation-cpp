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
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <array>

namespace {
constexpr int kDefaultTimeScaleMsPerSimMinute = 20;

struct IpcIds {
    int logQueue{-1};
    int regQueue{-1};
    int triageQueue{-1};
    std::array<int, kSpecialistCount> specialistsQueue{};
    int shmId{-1};
    int semWaitingRoom{-1};
    int semSharedState{-1};
};

std::atomic<bool> stopRequested(false);
std::atomic<bool> sigusr2Requested(false);
std::atomic<bool> sigintRequested(false);

void handleSigint(int) {
    stopRequested.store(true);
    sigintRequested.store(true);
}

void handleSigusr2(int) {
    sigusr2Requested.store(true);
    stopRequested.store(true);
}

long long monotonicMs() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        return 0;
    }
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

int simMinutesFrom(long long startMs, int scaleMsPerMinute) {
    if (startMs == 0 || scaleMsPerMinute <= 0) return 0;
    long long now = monotonicMs();
    long long delta = now - startMs;
    if (delta < 0) delta = 0;
    return static_cast<int>(delta / scaleMsPerMinute);
}

int realMinutesFrom(long long startMs) {
    if (startMs == 0) return 0;
    long long now = monotonicMs();
    long long delta = now - startMs;
    if (delta < 0) delta = 0;
    return static_cast<int>(delta / 60000); // 60s * 1000ms
}

/** @brief Set up logger/registration/triage/specialist queues, clearing stale ones and tuning capacity. */
bool createQueues(const std::string& keyPath, IpcIds& ids) {
    MessageQueue logQ;
    MessageQueue regQ;
    MessageQueue triQ;
    std::array<MessageQueue, kSpecialistCount> specQs;

    key_t logKey = ftok(keyPath.c_str(), 'L');
    key_t regKey = ftok(keyPath.c_str(), 'R');
    key_t triKey = ftok(keyPath.c_str(), 'T');
    std::array<key_t, kSpecialistCount> specKeys;
    for (int i = 0; i < kSpecialistCount; ++i) {
        specKeys[i] = ftok(keyPath.c_str(), 'A' + i);
    }
    if (logKey == -1 || regKey == -1 || triKey == -1) {
        logErrno("ftok failed");
        return false;
    }
    for (int i = 0; i < kSpecialistCount; ++i) {
        if (specKeys[i] == -1) {
            logErrno("ftok failed for specialist queue");
            return false;
        }
    }

    // Best effort: remove stale queues from previous crashed runs.
    auto removeIfExists = [](key_t key) {
        int qid = msgget(key, 0);
        if (qid != -1) {
            msgctl(qid, IPC_RMID, nullptr);
        }
    };
    removeIfExists(logKey);
    removeIfExists(regKey);
    removeIfExists(triKey);
    for (int i = 0; i < kSpecialistCount; ++i) {
        removeIfExists(specKeys[i]);
    }

    if (!logQ.create(logKey, 0600) || !regQ.create(regKey, 0600) ||
        !triQ.create(triKey, 0600)) {
        return false;
    }
    for (int i = 0; i < kSpecialistCount; ++i) {
        if (!specQs[i].create(specKeys[i], 0600)) {
            return false;
        }
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
    for (int i = 0; i < kSpecialistCount; ++i) {
        tuneQueue(specQs[i].id());
    }

    ids.logQueue = logQ.id();
    ids.regQueue = regQ.id();
    ids.triageQueue = triQ.id();
    for (int i = 0; i < kSpecialistCount; ++i) {
        ids.specialistsQueue[i] = specQs[i].id();
    }
    return true;
}

/** @brief Create waiting-room and shared-state semaphores; remove stale sets first. */
bool createSemaphores(const std::string& keyPath, const Config& cfg, IpcIds& ids) {
    key_t waitKey = ftok(keyPath.c_str(), 'W');
    key_t stateKey = ftok(keyPath.c_str(), 'M');
    if (waitKey == -1 || stateKey == -1) {
        logErrno("ftok for semaphores failed");
        return false;
    }

    // Best effort cleanup of stale semaphores from previous crashed runs.
    auto removeIfExists = [](key_t key) {
        int sid = semget(key, 1, 0);
        if (sid != -1) {
            semctl(sid, 0, IPC_RMID);
        }
    };
    removeIfExists(waitKey);
    removeIfExists(stateKey);

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

/** @brief Allocate and attach shared memory for SharedState, wiping any leftovers. */
bool createSharedState(const std::string& keyPath, IpcIds& ids, SharedState*& stateOut) {
    SharedMemory shm;
    key_t shmKey = ftok(keyPath.c_str(), 'H');
    if (shmKey == -1) {
        logErrno("ftok for shm failed");
        return false;
    }
    // Remove stale shared memory if present (previous crash).
    int staleId = shmget(shmKey, 0, 0);
    if (staleId != -1) {
        shmctl(staleId, IPC_RMID, nullptr);
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

std::string formatDuration(long long seconds) {
    long long days = seconds / 86400;
    seconds %= 86400;
    long long hours = seconds / 3600;
    seconds %= 3600;
    long long minutes = seconds / 60;
    seconds %= 60;
    std::ostringstream oss;
    oss << days << "d " << hours << "h " << minutes << "m " << seconds << "s";
    return oss.str();
}

struct SpecialistNames {
    static const char* name(int idx) {
        switch (static_cast<SpecialistType>(idx)) {
            case SpecialistType::Cardiologist: return "Cardiologist";
            case SpecialistType::Neurologist: return "Neurologist";
            case SpecialistType::Ophthalmologist: return "Ophthalmologist";
            case SpecialistType::Laryngologist: return "Laryngologist";
            case SpecialistType::Surgeon: return "Surgeon";
            case SpecialistType::Paediatrician: return "Paediatrician";
            default: return "Unknown";
        }
    }
};

/**
 * @brief Forks and execs a child process with provided argv, logging fork/exec errors.
 * Parent receives the child's pid; child only returns on exec failure (_exit(1)).
 */
pid_t forkExec(const std::string& exePath, const std::vector<std::string>& argv,
               const std::string& forkErrMsg, const std::string& execErrMsg) {
    pid_t pid = fork();
    if (pid == -1) {
        logErrno(forkErrMsg);
        return -1;
    }
    if (pid == 0) {
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& s : argv) {
            args.push_back(const_cast<char*>(s.c_str()));
        }
        args.push_back(nullptr);
        execv(exePath.c_str(), args.data());
        logErrno(execErrMsg);
        _exit(1);
    }
    return pid;
}

struct SummaryPayload {
    int totalPatients{0};
    int waitingRoomCapacity{0};
    int queueRegistrationLen{0};
    int triageRed{0};
    int triageYellow{0};
    int triageGreen{0};
    int triageSentHome{0};
    int outcomeHome{0};
    int outcomeWard{0};
    int outcomeOther{0};
    int timeScaleMsPerSimMinute{0};
    int simulationDurationMinutes{0};
    long long simulatedSeconds{0};
    pid_t directorPid{0};
    pid_t registration1Pid{0};
    pid_t registration2Pid{0};
    pid_t triagePid{0};
    std::vector<pid_t> reg2History;
    std::array<pid_t, kSpecialistCount> specialistPids{};
};

SummaryPayload buildPayload(const SharedState* state, long long simulatedSeconds,
                            const std::vector<pid_t>& reg2History,
                            const std::array<pid_t, kSpecialistCount>& specialistPids) {
    SummaryPayload payload;
    payload.totalPatients = state->totalPatients;
    payload.waitingRoomCapacity = state->waitingRoomCapacity;
    payload.queueRegistrationLen = state->queueRegistrationLen;
    payload.triageRed = state->triageRed;
    payload.triageYellow = state->triageYellow;
    payload.triageGreen = state->triageGreen;
    payload.triageSentHome = state->triageSentHome;
    payload.outcomeHome = state->outcomeHome;
    payload.outcomeWard = state->outcomeWard;
    payload.outcomeOther = state->outcomeOther;
    payload.timeScaleMsPerSimMinute = state->timeScaleMsPerSimMinute;
    payload.simulationDurationMinutes = state->simulationDurationMinutes;
    payload.simulatedSeconds = simulatedSeconds;
    payload.directorPid = state->directorPid;
    payload.registration1Pid = state->registration1Pid;
    payload.registration2Pid = state->registration2Pid;
    payload.triagePid = state->triagePid;
    payload.reg2History = reg2History;
    payload.specialistPids = specialistPids;
    return payload;
}

std::string joinHistory(const std::vector<pid_t>& values) {
    std::ostringstream oss;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << values[i];
    }
    return oss.str();
}

bool writeSummaryText(const SummaryPayload& payload, std::ofstream& out) {
    out << "SOR Simulation Summary\n";
    out << "======================\n";
    out << "Total patients processed: " << payload.totalPatients << "\n";
    out << "Waiting room capacity: " << payload.waitingRoomCapacity << "\n";
    out << "Registered queue length at shutdown: " << payload.queueRegistrationLen << "\n";
    out << "Triage outcomes:\n";
    out << "  Red:    " << payload.triageRed << "\n";
    out << "  Yellow: " << payload.triageYellow << "\n";
    out << "  Green:  " << payload.triageGreen << "\n";
    out << "  Sent home from triage: " << payload.triageSentHome << "\n";
    out << "Final dispositions:\n";
    out << "  Home:       " << payload.outcomeHome << "\n";
    out << "  Ward:       " << payload.outcomeWard << "\n";
    out << "  Other:      " << payload.outcomeOther << "\n";
    out << "Time scale (ms per minute): " << payload.timeScaleMsPerSimMinute << "\n";
    out << "Simulation duration (config minutes): " << payload.simulationDurationMinutes << "\n";
    out << "Simulated elapsed time: " << formatDuration(payload.simulatedSeconds) << "\n";
    out << "Process IDs:\n";
    out << "  Director:      " << payload.directorPid << "\n";
    out << "  Registration1: " << payload.registration1Pid << "\n";
    out << "  Triage:        " << payload.triagePid << "\n";
    out << "  Specialists:\n";
    for (int i = 0; i < kSpecialistCount; ++i) {
        out << "    " << SpecialistNames::name(i) << ": ";
        if (payload.specialistPids[i] != 0) {
            out << payload.specialistPids[i] << "\n";
        } else {
            out << "not spawned\n";
        }
    }
    out << "Registration2 history: ";
    if (payload.reg2History.empty()) {
        out << "Not spawned during the simulation\n";
    } else {
        out << joinHistory(payload.reg2History) << "\n";
    }
    return true;
}

bool writeSummary(const SummaryPayload& payload, const std::string& path) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        logErrno("summary file open failed");
        return false;
    }
    return writeSummaryText(payload, out);
}

void destroyIpc(const IpcIds& ids, SharedState* attachedState) {
    if (attachedState) {
        shmdt(attachedState);
    }
    if (ids.logQueue != -1) {
        if (msgctl(ids.logQueue, IPC_RMID, nullptr) == -1) {
            logErrno("cleanup log queue failed");
        }
    }
    if (ids.regQueue != -1) {
        if (msgctl(ids.regQueue, IPC_RMID, nullptr) == -1) {
            logErrno("cleanup reg queue failed");
        }
    }
    if (ids.triageQueue != -1) {
        if (msgctl(ids.triageQueue, IPC_RMID, nullptr) == -1) {
            logErrno("cleanup triage queue failed");
        }
    }
    for (int qid : ids.specialistsQueue) {
        if (qid != -1) {
            if (msgctl(qid, IPC_RMID, nullptr) == -1) {
                logErrno("cleanup specialists queue failed");
            }
        }
    }
    if (ids.shmId != -1) {
        if (shmctl(ids.shmId, IPC_RMID, nullptr) == -1) {
            logErrno("cleanup shm failed");
        }
    }
    if (ids.semWaitingRoom != -1) {
        if (semctl(ids.semWaitingRoom, 0, IPC_RMID) == -1) {
            logErrno("cleanup waiting room semaphore failed");
        }
    }
    if (ids.semSharedState != -1) {
        if (semctl(ids.semSharedState, 0, IPC_RMID) == -1) {
            logErrno("cleanup shared state semaphore failed");
        }
    }
}
} // namespace

// Director entry point (see header for details).
int Director::run(const std::string& selfPath, const Config& config, const std::string* logPathOverride) {
    IpcIds ids;
    ids.specialistsQueue.fill(-1);
    SharedState* shared = nullptr;
    bool ok = true;
    Semaphore stateSemGuard;
    struct msqid_ds regStats{};
    lastSummaryPath_.clear();

    if (!createQueues(selfPath, ids)) {
        ok = false;
    }
    if (ok && !createSemaphores(selfPath, config, ids)) {
        ok = false;
    }
    if (ok && !createSharedState(selfPath, ids, shared)) {
        ok = false;
    }

    std::string logPath = logPathOverride ? *logPathOverride
                                          : "sor_run_" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".log";
    lastLogPath_ = logPath;

    pid_t loggerPid = -1;
    if (ok) {
        std::string queueIdStr = std::to_string(ids.logQueue);
        std::vector<std::string> args{selfPath, "logger", queueIdStr, logPath};
        loggerPid = forkExec(selfPath, args, "fork for logger failed", "execv for logger failed");
        if (loggerPid == -1) ok = false;
    }

    // Handle Ctrl+C to request early stop.
    struct sigaction saInt {};
    saInt.sa_handler = handleSigint;
    sigemptyset(&saInt.sa_mask);
    saInt.sa_flags = 0;
    sigaction(SIGINT, &saInt, nullptr);
    struct sigaction saUsr2 {};
    saUsr2.sa_handler = handleSigusr2;
    sigemptyset(&saUsr2.sa_mask);
    saUsr2.sa_flags = 0;
    sigaction(SIGUSR2, &saUsr2, nullptr);

    long long simStartMs = monotonicMs();
    auto simNow = [&]() { return simMinutesFrom(simStartMs, config.timeScaleMsPerSimMinute); };
    // Scales a base duration with the configured time scale; preserves zero/negative as zero.
    auto scaleAllowZero = [&](int baseMs) {
        if (baseMs <= 0) return 0;
        long long scaled = static_cast<long long>(baseMs) * config.timeScaleMsPerSimMinute /
                           kDefaultTimeScaleMsPerSimMinute;
        if (scaled <= 0) scaled = 1;
        return static_cast<int>(scaled);
    };
    // Scales a base duration and clamps to at least 1 ms to avoid zero-time steps.
    auto scaleAtLeastOne = [&](int baseMs) {
        int v = scaleAllowZero(baseMs);
        return v <= 0 ? 1 : v;
    };
    int scaledRegMs = scaleAllowZero(config.registrationServiceMs);
    int scaledTriageMs = scaleAllowZero(config.triageServiceMs);
    int scaledSpecMin = scaleAtLeastOne(config.specialistExamMinMs);
    int scaledSpecMax = scaleAtLeastOne(config.specialistExamMaxMs);
    if (scaledSpecMax < scaledSpecMin) scaledSpecMax = scaledSpecMin;
    int scaledLeaveMin = scaleAtLeastOne(config.specialistLeaveMinMs);
    int scaledLeaveMax = scaleAtLeastOne(config.specialistLeaveMaxMs);
    if (scaledLeaveMax < scaledLeaveMin) scaledLeaveMax = scaledLeaveMin;
    bool reconcileWaitSemEnabled = config.reconcileWaitSem != 0;
    if (const char* env = std::getenv("SORSIM_RECONCILE_WAITSEM")) {
        reconcileWaitSemEnabled = std::string(env) == "1";
    }

    if (ok && shared) {
        shared->currentInWaitingRoom = 0;
        shared->waitingRoomCapacity = config.N_waitingRoom;
        shared->queueRegistrationLen = 0;
        shared->reg2Active = 0;
        shared->timeScaleMsPerSimMinute = config.timeScaleMsPerSimMinute;
        shared->simulationDurationMinutes = config.simulationDurationMinutes;
        shared->simStartMonotonicMs = simStartMs;
        shared->totalPatients = 0;
        shared->triageRed = shared->triageYellow = shared->triageGreen = shared->triageSentHome = 0;
        shared->registrationServiceMs = scaledRegMs;
        shared->triageServiceMs = scaledTriageMs;
        shared->specialistExamMinMs = scaledSpecMin;
        shared->specialistExamMaxMs = scaledSpecMax;
        shared->specialistLeaveMinMs = scaledLeaveMin;
        shared->specialistLeaveMaxMs = scaledLeaveMax;
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

    if (ok && shared) {
        setLogMetricsContext({shared, ids.regQueue, ids.triageQueue, ids.specialistsQueue,
                              ids.semWaitingRoom, ids.semSharedState});
    }

    pid_t reg1Pid = -1;
    pid_t reg2Pid = -1;
    pid_t triagePid = -1;
    pid_t generatorPid = -1;

    if (ok) {
        int simTime = simNow();
        logEvent(ids.logQueue, Role::Director, simTime, "Director: IPC initialized, logger spawned: " + logPath);
        logEvent(ids.logQueue, Role::Director, simTime,
                 "Simulation config N=" + std::to_string(config.N_waitingRoom) +
                 " K=" + std::to_string(config.K_registrationThreshold) +
                 " simMinutes=" + std::to_string(config.simulationDurationMinutes) +
                 " msPerMinute=" + std::to_string(config.timeScaleMsPerSimMinute) +
                 " regMs=" + std::to_string(scaledRegMs) +
                 " triageMs=" + std::to_string(scaledTriageMs) +
                 " specMinMax=" + std::to_string(scaledSpecMin) +
                 "/" + std::to_string(scaledSpecMax) +
                 " leaveMinMax=" + std::to_string(scaledLeaveMin) +
                 "/" + std::to_string(scaledLeaveMax) +
                 " reconcileWaitSem=" + std::to_string(reconcileWaitSemEnabled ? 1 : 0));
        logEvent(ids.logQueue, Role::Director, simTime,
                 "Director PIDs: reg1=" + std::to_string(reg1Pid) +
                 " reg2=" + std::to_string(reg2Pid) +
                 " triage=" + std::to_string(triagePid) +
                 " gen=" + std::to_string(generatorPid));
    }

    if (ok) {
        std::vector<std::string> args{selfPath, "registration", selfPath};
        reg1Pid = forkExec(selfPath, args, "fork for registration failed", "execv for registration failed");
        if (reg1Pid == -1) {
            ok = false;
        } else {
            if (shared) {
                shared->registration1Pid = reg1Pid;
            }
            logEvent(ids.logQueue, Role::Director, simNow(), "Registration1 spawned");
        }
    }
    if (ok) {
        std::vector<std::string> args{selfPath, "triage", selfPath};
        triagePid = forkExec(selfPath, args, "fork for triage failed", "execv for triage failed");
        if (triagePid == -1) {
            ok = false;
        } else {
            if (shared) {
                shared->triagePid = triagePid;
            }
            logEvent(ids.logQueue, Role::Director, simNow(), "Triage spawned");
        }
    }

    if (ok) {
        std::vector<std::string> argVals = {
            std::to_string(config.N_waitingRoom),
            std::to_string(config.K_registrationThreshold),
            std::to_string(config.simulationDurationMinutes),
            std::to_string(config.timeScaleMsPerSimMinute),
            std::to_string(config.randomSeed),
            std::to_string(config.patientGenMinMs),
            std::to_string(config.patientGenMaxMs)
        };
        std::vector<std::string> args{selfPath, "patient_generator", selfPath};
        args.insert(args.end(), argVals.begin(), argVals.end());
        generatorPid = forkExec(selfPath, args, "fork for patient generator failed", "execv for patient generator failed");
        if (generatorPid == -1) {
            ok = false;
        } else {
            logEvent(ids.logQueue, Role::Director, simNow(), "Patient generator spawned");
        }
    }
    std::vector<pid_t> specialistPids;
    std::array<pid_t, kSpecialistCount> specialistPidMap{};
    std::vector<pid_t> reg2History;
    if (ok) {
        for (int i = 0; i < kSpecialistCount; ++i) {
            std::string typeStr = std::to_string(i);
            std::vector<std::string> args{selfPath, "specialist", selfPath, typeStr};
            pid_t pid = forkExec(selfPath, args, "fork for specialist failed", "execv for specialist failed");
            if (pid == -1) {
                ok = false;
                break;
            }
            specialistPids.push_back(pid);
            specialistPidMap[i] = pid;
            logEvent(ids.logQueue, Role::Director, simNow(), "Specialist spawned type " + std::to_string(i));
        }
    }

    // Wait for child exit with timeout; fall back to SIGKILL + waitpid to avoid zombies.
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
                logEvent(ids.logQueue, Role::Director, simNow(), "Force killed " + name);
                waitpid(pid, nullptr, 0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    };

    // Run until user interruption (Ctrl+C) or configured duration elapses.
    const int chunkMs = 100;
    RandomGenerator directorRng(static_cast<unsigned int>(std::time(nullptr)));
    int sigusr1CooldownMs = 1000; // attempt SIGUSR1 roughly every second if specialists exist
    int elapsedSinceUsr1 = 0;
    long long lastMonitorLogMs = monotonicMs();
    while (!stopRequested.load()) {
        usleep(static_cast<useconds_t>(chunkMs * 1000));
        int simTime = simNow();
        // Stop based on real wall-clock minutes, not simulated minutes; keeps duration tied to actual time.
        if (config.simulationDurationMinutes > 0 &&
            realMinutesFrom(simStartMs) >= config.simulationDurationMinutes) {
            stopRequested.store(true);
            logEvent(ids.logQueue, Role::Director, simTime,
                     "Simulation duration reached (" + std::to_string(config.simulationDurationMinutes) + " min)");
            break;
        }
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
            // Hysteresis: open when queue >= K, close when queue falls below N/3.
            int openThreshold = config.K_registrationThreshold;
            int closeThreshold = config.N_waitingRoom / 3;
            if (!reg2Flag && qlen >= openThreshold) {
                std::vector<std::string> args{selfPath, "registration2", selfPath};
                pid_t pid = forkExec(selfPath, args, "fork for registration2 failed", "execv for registration2 failed");
                if (pid > 0) {
                    reg2Pid = pid;
                    reg2History.push_back(pid);
                    stateSemGuard.wait();
                    shared->reg2Active = 1;
                    shared->registration2Pid = pid;
                    stateSemGuard.post();
                    logEvent(ids.logQueue, Role::Director, simTime,
                             "Registration2 spawned (regQ=" + std::to_string(qlen) +
                             " waitingRoom=" + std::to_string(waitingRoomLoad) +
                             "/" + std::to_string(config.N_waitingRoom) + ")");
                }
            } else if (reg2Flag && qlen < closeThreshold) {
                if (reg2Pid > 0) {
                    kill(reg2Pid, SIGUSR2);
                    logEvent(ids.logQueue, Role::Director, simTime,
                             "Registration2 closing (regQ=" + std::to_string(qlen) +
                             " waitingRoom=" + std::to_string(waitingRoomLoad) +
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
        // Periodic monitor log with ERROR prefix to spot stalls/died processes.
        long long nowMs = monotonicMs();
        if (nowMs - lastMonitorLogMs >= 5000 && ids.semWaitingRoom != -1) {
            lastMonitorLogMs = nowMs;
            int wsemVal = semctl(ids.semWaitingRoom, 0, GETVAL);
            if (wsemVal < 0) {
                logErrno("ERROR MONITOR semctl GETVAL failed for waiting room");
                wsemVal = -1;
            }
            int regQLen = 0;
            if (ids.regQueue != -1 && msgctl(ids.regQueue, IPC_STAT, &regStats) == 0) {
                regQLen = static_cast<int>(regStats.msg_qnum);
            }
            int triQLen = 0;
            if (ids.triageQueue != -1) {
                struct msqid_ds triStats {};
                if (msgctl(ids.triageQueue, IPC_STAT, &triStats) == 0) {
                    triQLen = static_cast<int>(triStats.msg_qnum);
                }
            }
            int inside = 0;
            stateSemGuard.wait();
            if (shared) {
                inside = shared->currentInWaitingRoom;
            }
            stateSemGuard.post();
            int expectedFree = (shared ? shared->waitingRoomCapacity : 0) - inside;
            int missing = expectedFree - wsemVal;
            bool reg1Alive = reg1Pid > 0 && kill(reg1Pid, 0) == 0;
            bool reg2Alive = reg2Pid > 0 && kill(reg2Pid, 0) == 0;
            bool triAlive = triagePid > 0 && kill(triagePid, 0) == 0;
            int semPid = -1;
            int waiters = -1;
            int zeroWaiters = -1;
            struct semid_ds semInfo {};
            if (ids.semWaitingRoom != -1) {
                semPid = semctl(ids.semWaitingRoom, 0, GETPID);
                waiters = semctl(ids.semWaitingRoom, 0, GETNCNT);
                zeroWaiters = semctl(ids.semWaitingRoom, 0, GETZCNT);
                // Best-effort stats read; logging tolerates IPC_STAT failure.
                semctl(ids.semWaitingRoom, 0, IPC_STAT, &semInfo);
            }

            // Optional reconcile: detects rare SysV semaphore token drift (free seats mismatch semaphore value)
            // and resets the semaphore to the expected count to keep the simulation moving while we investigate.
            if (reconcileWaitSemEnabled && missing > 0 && ids.semWaitingRoom != -1) {
                int setRes = semctl(ids.semWaitingRoom, 0, SETVAL, expectedFree);
                logEvent(ids.logQueue, Role::Director, simTime,
                         "ERROR MON RECONCILE set waitSem from " + std::to_string(wsemVal) +
                         " to " + std::to_string(expectedFree) +
                         " missing=" + std::to_string(missing) +
                         " pid=" + std::to_string(semPid) +
                         " n=" + std::to_string(waiters) +
                         " z=" + std::to_string(zeroWaiters) +
                         " setRes=" + std::to_string(setRes));
                // refresh observed value after reconcile for logging below
                wsemVal = semctl(ids.semWaitingRoom, 0, GETVAL);
            }
            logEvent(ids.logQueue, Role::Director, simTime,
                     "ERROR MON w=" + std::to_string(wsemVal) +
                     " id=" + std::to_string(ids.semWaitingRoom) +
                     " miss=" + std::to_string(missing) +
                     " pid=" + std::to_string(semPid) +
                     " n=" + std::to_string(waiters) +
                     " z=" + std::to_string(zeroWaiters) +
                     " ot=" + std::to_string(static_cast<long long>(semInfo.sem_otime)) +
                     " r1=" + std::to_string(reg1Alive ? 1 : 0) +
                     " r2=" + std::to_string(reg2Alive ? 1 : 0) +
                     " t=" + std::to_string(triAlive ? 1 : 0));
            // No automatic reconcile here; we want to catch the first drift to find root cause.
        }
        if (!specialistPids.empty() && elapsedSinceUsr1 >= sigusr1CooldownMs) {
            elapsedSinceUsr1 = 0;
            int roll = directorRng.uniformInt(0, 99);
            if (roll < 5) { // ~5% chance each second
                pid_t target = specialistPids[directorRng.uniformInt(0, static_cast<int>(specialistPids.size()) - 1)];
                if (target > 0) {
                    kill(target, SIGUSR1);
                    logEvent(ids.logQueue, Role::Director, simTime, "Director sent SIGUSR1 to specialist pid=" + std::to_string(target));
                }
            }
        }
    }

    int stopSimTime = simNow();
    if (sigusr2Requested.load()) {
        logEvent(ids.logQueue, Role::Director, stopSimTime, "Director received SIGUSR2, broadcasting shutdown");
    } else if (sigintRequested.load()) {
        logEvent(ids.logQueue, Role::Director, stopSimTime, "Director received SIGINT (Ctrl+C), broadcasting SIGUSR2");
        std::cout << "Director received SIGINT (Ctrl+C), broadcasting SIGUSR2" << std::endl;
    } else {
        logEvent(ids.logQueue, Role::Director, stopSimTime, "Director received stop request, broadcasting SIGUSR2");
    }
    // Coordinated shutdown: send SIGUSR2 individually (process group removed for portability).
    logEvent(ids.logQueue, Role::Director, stopSimTime, "Director initiating shutdown (SIGUSR2 to children)");
    if (reg1Pid > 0) kill(reg1Pid, SIGUSR2);
    if (reg2Pid > 0) kill(reg2Pid, SIGUSR2);
    if (triagePid > 0) kill(triagePid, SIGUSR2);
    for (pid_t pid : specialistPids) {
        if (pid > 0) kill(pid, SIGUSR2);
    }
    if (generatorPid > 0) kill(generatorPid, SIGUSR2);

    waitWithTimeout(reg1Pid, "registration");
    waitWithTimeout(reg2Pid, "registration2");
    waitWithTimeout(triagePid, "triage");
    for (pid_t pid : specialistPids) {
        waitWithTimeout(pid, "specialist");
    }
    waitWithTimeout(generatorPid, "patient_generator");

    // write final summary before logger shuts down
    if (shared && ids.logQueue != -1) {
        std::string summaryPath = "sor_summary_" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".txt";
        long long nowMs = monotonicMs();
        long long simulatedSeconds = 0;
        if (shared->timeScaleMsPerSimMinute > 0) {
            long long deltaMs = nowMs - shared->simStartMonotonicMs;
            if (deltaMs < 0) deltaMs = 0;
            long long simulatedMinutes = deltaMs / shared->timeScaleMsPerSimMinute;
            long long remainderMs = deltaMs % shared->timeScaleMsPerSimMinute;
            simulatedSeconds = simulatedMinutes * 60 + (remainderMs * 60) / shared->timeScaleMsPerSimMinute;
        }
        SummaryPayload payload = buildPayload(shared, simulatedSeconds, reg2History, specialistPidMap);
        if (writeSummary(payload, summaryPath)) {
            logEvent(ids.logQueue, Role::Director, stopSimTime, "Summary saved: " + summaryPath);
            lastSummaryPath_ = summaryPath;
        }
    }
    // send termination marker for logger after children have had a chance to log shutdown
    if (ok) {
        logEvent(ids.logQueue, Role::Director, stopSimTime, "END");
    }
    waitWithTimeout(loggerPid, "logger");

    destroyIpc(ids, shared);

    return ok ? 0 : 1;
}
