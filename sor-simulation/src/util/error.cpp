#include "util/error.hpp"

#include <cstdlib>
#include <iostream>
#include <cerrno>
#include <cstring>

void die(const std::string& message) {
    std::cerr << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void logErrno(const std::string& message) {
    int err = errno;
    std::cerr << message;
    if (err != 0) {
        std::cerr << " (errno=" << err << " \"" << std::strerror(err) << "\")";
    }
    std::cerr << std::endl;
}
