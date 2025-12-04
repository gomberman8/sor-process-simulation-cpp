#pragma once

#include <string>

#include "model/config.hpp"

/**
 * @brief Central orchestrator: sets up IPC, spawns roles, handles shutdown.
 */
class Director {
public:
    Director() = default;

    /**
     * @brief Entry point for the director process.
     * @param selfPath path to current executable (used for exec of roles).
     * @param config validated configuration values.
     * @param logPathOverride optional log path; if null, a timestamped path is used.
     * @return 0 on clean shutdown, non-zero on failure.
     */
    int run(const std::string& selfPath, const Config& config, const std::string* logPathOverride = nullptr);

    /**
     * @brief Path to the most recently written summary file (text variant).
     * @return empty if no summary was produced during the last run.
     */
    const std::string& lastSummaryPath() const { return lastSummaryPath_; }
    /**
     * @brief Path to the log file used in the last run.
     */
    const std::string& lastLogPath() const { return lastLogPath_; }

private:
    std::string lastSummaryPath_;
    std::string lastLogPath_;
};
