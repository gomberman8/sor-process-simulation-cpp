#include "logging/logger.hpp"

#include "util/error.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <sys/msg.h>
#include <cstring>
#include <iostream>

#include "model/events.hpp"
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
std::string roleToString(int roleInt) {
    auto role = static_cast<Role>(roleInt);
    switch (role) {
        case Role::Director: return "DIRECTOR";
        case Role::PatientGenerator: return "PATIENT_GEN";
        case Role::Patient: return "PATIENT";
        case Role::Registration1: return "REG1";
        case Role::Registration2: return "REG2";
        case Role::Triage: return "TRIAGE";
        case Role::SpecialistCardio: return "SPEC_CARDIO";
        case Role::SpecialistNeuro: return "SPEC_NEURO";
        case Role::SpecialistOphthalmo: return "SPEC_OPHTH";
        case Role::SpecialistLaryng: return "SPEC_LARYNG";
        case Role::SpecialistSurgeon: return "SPEC_SURGEON";
        case Role::SpecialistPaediatric: return "SPEC_PAED";
        case Role::Logger: return "LOGGER";
        default: return "UNKNOWN";
    }
}
} // namespace

int runLogger(int queueId, const std::string& path) {
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
        // simTime;pid;role;text
        std::string line = std::to_string(msg.simTime) + ";"
                         + std::to_string(msg.pid) + ";"
                         + roleToString(msg.role) + ";"
                         + msg.text;
        logger.logLine(line);
    }

    logger.closeFile();
    return ok ? 0 : 1;
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
    std::strncpy(msg.text, text.c_str(), sizeof(msg.text) - 1);
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
