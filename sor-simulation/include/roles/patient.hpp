#pragma once

/**
 * @brief Represents a single patient (or child+guardian) going through the SOR pipeline.
 */
class Patient {
public:
    Patient() = default;

    /**
     * @brief Execute patient journey (registration -> triage -> specialist).
     * @return 0 on completion or orderly shutdown.
     */
    int run();
};
