#pragma once

#include <cstdint>

enum class TriageColor {
    Red,
    Yellow,
    Green,
    None
};

enum class SpecialistType {
    Cardiologist,
    Neurologist,
    Ophthalmologist,
    Laryngologist,
    Surgeon,
    Paediatrician,
    None
};

enum class EventType : long {
    PatientArrived = 1,
    PatientRegistered = 2,
    PatientSentHomeFromTriage = 3,
    PatientToSpecialist = 4,
    PatientDone = 5,
    LogMessage = 10
};

enum class Role {
    Director,
    PatientGenerator,
    Patient,
    Registration1,
    Registration2,
    Triage,
    SpecialistCardio,
    SpecialistNeuro,
    SpecialistOphthalmo,
    SpecialistLaryng,
    SpecialistSurgeon,
    SpecialistPaediatric,
    Logger
};
