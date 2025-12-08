#pragma once

#include <cstdint>

struct Config {
    int N_waitingRoom;
    int K_registrationThreshold;
    int timeScaleMsPerSimMinute;
    int simulationDurationMinutes;
    unsigned int randomSeed;
    int visualizerRenderIntervalMs;
    int registrationServiceMs;
    int triageServiceMs;
    int specialistExamMinMs;
    int specialistExamMaxMs;
};
