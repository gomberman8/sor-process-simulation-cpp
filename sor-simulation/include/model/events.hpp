#pragma once

#include "types.hpp"

struct EventMessage {
    long mtype;           // static_cast<long>(EventType)
    int  patientId;
    int  specialistIdx;   // index or enum cast (SpecialistType)
    int  triageColor;     // cast from TriageColor
    int  isVip;
    int  age;
    int  personsCount;
    char extra[64];
};

// Log messages destined for LOG_QUEUE
struct LogMessage {
    long mtype;           // e.g. 1 for all log events
    int  role;            // cast from Role enum
    int  simTime;         // simulated time (minutes)
    int  pid;             // process PID
    char text[128];       // log line text (without timestamp prefix)
};
