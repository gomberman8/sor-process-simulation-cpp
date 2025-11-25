#pragma once

#include <string>

/**
 * @brief One registration window consuming from REGISTRATION_QUEUE.
 */
class Registration {
public:
    Registration() = default;

    /**
     * @brief Process incoming patients and forward to triage.
     * @param keyPath path used for ftok keys (shared with director).
     * @return 0 on normal exit.
     */
    int run(const std::string& keyPath);
};
