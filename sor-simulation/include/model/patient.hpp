#pragma once

#include "types.hpp"
#include <string>

struct PatientInfo {
    int         id;             // internal ID
    int         age;
    bool        isVip;
    bool        hasGuardian;    // true if child with adult
    int         personsCount;   // 1 or 2 (guardian + child)
    TriageColor triageColor;
    SpecialistType targetSpecialist;
    std::string symptoms;
};
