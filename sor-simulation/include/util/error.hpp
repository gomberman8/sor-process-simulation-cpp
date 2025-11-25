#pragma once

#include <string>

/**
 * @brief Error handling helpers for system-call failures.
 */
void die(const std::string& message);

/**
 * @brief Log a message along with errno details (implementation TBD).
 */
void logErrno(const std::string& message);
