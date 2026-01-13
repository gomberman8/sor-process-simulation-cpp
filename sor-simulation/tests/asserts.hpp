#pragma once

#include <iostream>
#include <cerrno>
#include <string>
#include <unistd.h>

// Minimal assertion helpers for standalone test binaries (print to stderr and return failure).

inline void writeStdout(const std::string& msg) {
    const char* data = msg.data();
    size_t remaining = msg.size();
    while (remaining > 0) {
        ssize_t written = ::write(STDOUT_FILENO, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        data += static_cast<size_t>(written);
        remaining -= static_cast<size_t>(written);
    }
}

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        auto _lhs = (a); \
        auto _rhs = (b); \
        if (!(_lhs == _rhs)) { \
            std::cerr << "[FAIL] " << (msg) << " expected=" << _rhs << " actual=" << _lhs \
                      << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return 1; \
        } \
    } while (0)
