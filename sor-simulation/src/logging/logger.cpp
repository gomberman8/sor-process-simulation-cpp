#include "logging/logger.hpp"

#include "util/error.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <string>

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
