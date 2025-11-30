#include "logging/logger.hpp"

#include "util/error.hpp"

#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/msg.h>
#include <sys/sem.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

#include "model/events.hpp"
#include "model/shared_state.hpp"
#include "model/types.hpp"

Logger::Logger() : fd(-1) {}

Logger::Logger(const std::string& path) : fd(-1) {
    openFile(path);
}

bool Logger::openFile(const std::string& path) {
    if (fd != -1) {
        closeFile();
    }
    fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        logErrno("open log file failed");
        return false;
    }
    return true;
}

void Logger::logLine(const std::string& line) {
    if (fd == -1) {
        logErrno("logLine called with closed fd");
        return;
    }
    std::string withNewline = line;
    withNewline.push_back('\n');
    ssize_t written = ::write(fd, withNewline.data(), withNewline.size());
    if (written == -1) {
        logErrno("write failed");
    }
}

void Logger::closeFile() {
    if (fd != -1) {
        ::close(fd);
        fd = -1;
    }
}

namespace {
struct MetricsSnapshot {
    int waitingInside{0};
    int waitingCapacity{0};
    int registrationQueueLen{0};
    int triageQueueLen{0};
    int specialistsQueueLen{0};
    int waitSemaphoreValue{0};
    int stateSemaphoreValue{0};
};

LogMetricsContext g_logMetricsContext{};
bool g_metricsContextSet = false;

int queueLength(int qid) {
    if (qid < 0) return 0;
    struct msqid_ds stats{};
    if (msgctl(qid, IPC_STAT, &stats) == -1) {
        return 0;
    }
    return static_cast<int>(stats.msg_qnum);
}

int semaphoreValue(int semId) {
    if (semId < 0) return 0;
    int val = semctl(semId, 0, GETVAL);
    if (val == -1) {
        return 0;
    }
    return val;
}

MetricsSnapshot collectMetrics() {
    MetricsSnapshot metrics{};
    if (!g_metricsContextSet) {
        return metrics;
    }
    if (g_logMetricsContext.sharedState) {
        metrics.waitingInside = g_logMetricsContext.sharedState->currentInWaitingRoom;
        metrics.waitingCapacity = g_logMetricsContext.sharedState->waitingRoomCapacity;
    }
    metrics.registrationQueueLen = queueLength(g_logMetricsContext.registrationQueueId);
    metrics.triageQueueLen = queueLength(g_logMetricsContext.triageQueueId);
    metrics.specialistsQueueLen = queueLength(g_logMetricsContext.specialistsQueueId);
    metrics.waitSemaphoreValue = semaphoreValue(g_logMetricsContext.waitSemaphoreId);
    metrics.stateSemaphoreValue = semaphoreValue(g_logMetricsContext.stateSemaphoreId);
    return metrics;
}

std::string roleLabel(int roleInt) {
    auto role = static_cast<Role>(roleInt);
    switch (role) {
        case Role::Director: return "director";
        case Role::PatientGenerator: return "patient_gen";
        case Role::Patient: return "patient";
        case Role::Registration1: return "reg1";
        case Role::Registration2: return "reg2";
        case Role::Triage: return "triage";
        case Role::SpecialistCardio:
        case Role::SpecialistNeuro:
        case Role::SpecialistOphthalmo:
        case Role::SpecialistLaryng:
        case Role::SpecialistSurgeon:
        case Role::SpecialistPaediatric:
            return "specialist";
        case Role::Logger: return "logger";
        default: return "unknown";
    }
}
} // namespace

int runLogger(int queueId, const std::string& path) {
    // Ignore SIGINT so logger survives Ctrl+C until it receives END.
    struct sigaction saIgnore {};
    saIgnore.sa_handler = SIG_IGN;
    sigemptyset(&saIgnore.sa_mask);
    saIgnore.sa_flags = 0;
    sigaction(SIGINT, &saIgnore, nullptr);

    Logger logger(path);
    if (queueId == -1) {
        logErrno("runLogger invalid queue id");
        return 1;
    }

    bool ok = true;
    while (true) {
        LogMessage msg{};
        ssize_t res = msgrcv(queueId, &msg, sizeof(LogMessage) - sizeof(long),
                             static_cast<long>(EventType::LogMessage), 0);
        if (res == -1) {
            if (errno == EINTR) {
                continue;
            }
            logErrno("Logger msgrcv failed");
            ok = false;
            break;
        }

        if (std::strncmp(msg.text, "END", 3) == 0) {
            break;
        }

        // Semicolon-separated line for easy parsing/CSV import:
        // simTime;pid;wR;rQ;tQ;sQ;wSem;sSem;who;text
        std::string line = std::to_string(msg.simTime) + ";"
                         + std::to_string(msg.pid) + ";"
                         + msg.text;
        logger.logLine(line);
    }

    logger.closeFile();
    return ok ? 0 : 1;
}

void setLogMetricsContext(const LogMetricsContext& context) {
    g_logMetricsContext = context;
    g_metricsContextSet = true;
}

bool logEvent(int queueId, Role role, int simTime, const std::string& text) {
    if (queueId == -1) {
        logErrno("logEvent invalid queue id");
        return false;
    }
    LogMessage msg{};
    msg.mtype = static_cast<long>(EventType::LogMessage);
    msg.role = static_cast<int>(role);
    msg.simTime = simTime;
    msg.pid = getpid();
    std::string finalText = text;
    if (g_metricsContextSet) {
        MetricsSnapshot metrics = collectMetrics();
        finalText = "wR=" + std::to_string(metrics.waitingInside) + "/" + std::to_string(metrics.waitingCapacity) + ";"
                  + "rQ=" + std::to_string(metrics.registrationQueueLen) + ";"
                  + "tQ=" + std::to_string(metrics.triageQueueLen) + ";"
                  + "sQ=" + std::to_string(metrics.specialistsQueueLen) + ";"
                  + "wSem=" + std::to_string(metrics.waitSemaphoreValue) + ";"
                  + "sSem=" + std::to_string(metrics.stateSemaphoreValue) + ";"
                  + roleLabel(static_cast<int>(role)) + ";"
                  + text;
    }
    std::strncpy(msg.text, finalText.c_str(), sizeof(msg.text) - 1);
    size_t payloadSize = sizeof(LogMessage) - sizeof(long);
    // Use IPC_NOWAIT to avoid blocking the simulation when the log queue is full.
    // On EAGAIN we simply drop the log entry to keep processing moving.
    if (msgsnd(queueId, &msg, payloadSize, IPC_NOWAIT) == -1) {
        if (errno == EAGAIN) {
            return false;
        }
        if (errno != EIDRM && errno != EINVAL) {
            logErrno("logEvent msgsnd failed");
        }
        return false;
    }
    return true;
}
