#pragma once

#include <string>

/**
 * @brief TUI-like visualizer that tails the simulation log and renders patient flow.
 * @param logPath path to the log file produced by the logger process.
 * @param renderIntervalMs refresh interval in milliseconds (higher = slower).
 * @return exit code (0 on normal exit, non-zero on errors).
 */
int runVisualizer(const std::string& logPath, int renderIntervalMs = 200);
