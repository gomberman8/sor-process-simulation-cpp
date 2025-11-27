#pragma once

#include <cstdint>

struct SharedState {
    int currentInWaitingRoom;   // persons inside (including children+guardians)
    int waitingRoomCapacity;    // total capacity N
    int queueRegistrationLen;   // registration queue length
    int reg2Active;             // 0/1
    int timeScaleMsPerSimMinute;    // wall-clock ms per simulated minute
    int simulationDurationMinutes;  // total planned duration
    long long simStartMonotonicMs;  // CLOCK_MONOTONIC at start (ms)

    int totalPatients;
    int triageRed;
    int triageYellow;
    int triageGreen;
    int triageSentHome;

    int outcomeHome;
    int outcomeWard;
    int outcomeOther;

    int directorPid;
    int registration1Pid;
    int registration2Pid;
    int triagePid;
    // arrays for specialists etc. can be added later
};
