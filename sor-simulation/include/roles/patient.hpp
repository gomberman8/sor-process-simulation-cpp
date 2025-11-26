#pragma once

#include <string>

/**
 * @brief Represents a single patient (or child+guardian) going through the SOR pipeline.
 */
class Patient {
public:
    Patient() = default;

    /**
     * @brief Execute patient journey (registration -> triage -> specialist).
     * @param keyPath path used for ftok keys.
     * @param patientId logical id.
     * @param age age in years.
     * @param isVip VIP flag.
     * @param hasGuardian child with guardian flag.
     * @param personsCount 1 or 2.
     * @return 0 on completion or orderly shutdown.
     */
    int run(const std::string& keyPath, int patientId, int age, bool isVip, bool hasGuardian, int personsCount);
};
