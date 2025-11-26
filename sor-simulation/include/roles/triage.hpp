#pragma once

#include <string>

/**
 * @brief Triage role assigning severity and destinations.
 */
class Triage {
public:
    Triage() = default;

    /**
     * @brief Consume from TRIAGE_QUEUE, assign colors, route to specialists/home.
     * @return 0 on normal exit.
     */
    int run(const std::string& keyPath);
};
