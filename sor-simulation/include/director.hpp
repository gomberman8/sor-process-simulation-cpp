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
     * @return 0 on clean shutdown, non-zero on failure.
     */
    int run(const std::string& selfPath, const Config& config);
};
