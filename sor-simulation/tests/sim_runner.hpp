#pragma once

#include "director.hpp"
#include "model/config.hpp"

#include <string>

/** @brief Outcome of a short-lived simulator run (exit code + log/summary paths). */
struct SimResult {
    int exitCode{0};
    std::string logPath;
    std::string summaryPath;
};

/** @brief Run the simulator for a fixed wall-clock duration, then stop with SIGUSR2. */
SimResult runSimulation(const std::string& sorSimPath, const Config& cfg, int runtimeMs);
