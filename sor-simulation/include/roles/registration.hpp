#pragma once

/**
 * @brief One registration window consuming from REGISTRATION_QUEUE.
 */
class Registration {
public:
    Registration() = default;

    /**
     * @brief Process incoming patients and forward to triage.
     * @return 0 on normal exit.
     */
    int run();
};
