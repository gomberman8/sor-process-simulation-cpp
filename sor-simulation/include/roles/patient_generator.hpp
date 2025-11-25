#pragma once

/**
 * @brief Generates patient processes periodically according to configuration.
 */
class PatientGenerator {
public:
    PatientGenerator() = default;

    /**
     * @brief Main loop for spawning patients.
     * @return 0 on normal stop, non-zero on error.
     */
    int run();
};
