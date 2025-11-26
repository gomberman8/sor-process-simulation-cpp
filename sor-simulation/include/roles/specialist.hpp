#pragma once

#include <string>
#include "model/types.hpp"

/**
 * @brief Specialist doctor handling one specialty and reacting to director signals.
 */
class Specialist {
public:
    Specialist() = default;

    /**
     * @brief Process patients from SPECIALISTS_QUEUE; handle SIGUSR1/SIGUSR2.
     * @return 0 on normal exit.
     */
    int run(const std::string& keyPath, SpecialistType type);
};
