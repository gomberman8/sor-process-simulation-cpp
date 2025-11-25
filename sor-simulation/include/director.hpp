#pragma once

/**
 * @brief Central orchestrator: sets up IPC, spawns roles, handles shutdown.
 */
class Director {
public:
    Director() = default;

    /**
     * @brief Entry point for the director process.
     * @return 0 on clean shutdown, non-zero on failure.
     */
    int run();
};
