#include "util/error.hpp"

#include <cstdlib>
#include <iostream>
#include <cerrno>
#include <cstdio>

void die(const std::string& message) {
    std::cerr << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void logErrno(const std::string& message) {
    if (!message.empty()) {
        perror(message.c_str());
    } else {
        perror(nullptr);
    }
}
