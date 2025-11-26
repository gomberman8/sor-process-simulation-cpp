#pragma once

#include <string>
#include "model/config.hpp"

/**
 * @brief Generates patient processes periodically according to configuration.
 */
class PatientGenerator {
public:
    PatientGenerator() = default;

    /**
     * @brief Main loop for spawning patients.
     * @param keyPath path used for ftok keys (shared with director).
     * @param cfg configuration (time scale, totals, seed).
     * @return 0 on normal stop, non-zero on error.
     */
    int run(const std::string& keyPath, const Config& cfg);
};
