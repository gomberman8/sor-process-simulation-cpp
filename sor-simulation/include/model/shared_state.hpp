#pragma once

#include <cstdint>

struct SharedState {
    int currentInWaitingRoom;   // persons inside (including children+guardians)
    int queueRegistrationLen;   // registration queue length
    int reg2Active;             // 0/1

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
