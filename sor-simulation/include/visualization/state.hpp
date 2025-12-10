#pragma once

#include "model/types.hpp"
#include "visualization/log_parser.hpp"

#include <array>
#include <deque>
#include <map>
#include <string>
#include <vector>

enum class Stage {
    OutsideQueue,
    WaitingRoom,
    RegistrationQueue,
    TriageQueue,
    SpecialistQueue,
    SpecialistActive,
    Done,
    SentHome
};

struct PatientView {
    int id{0};
    int pid{0};
    int patientPid{0};
    int persons{1};
    bool hasGuardian{false};
    bool isVip{false};
    TriageColor color{TriageColor::None};
    SpecialistType specialist{SpecialistType::None};
    Stage stage{Stage::OutsideQueue};
    bool registrationInProgress{false};
    std::string registrationWindow;
    std::string outcome;
    int lastSimTime{0};
    int waitOrder{-1};
    int regOrder{-1};
    int triageOrder{-1};
};

struct VisualizationState {
    std::map<int, PatientView> patients;
    int waitingCurrent{0};
    int waitingCapacity{0};
    int waitSem{0};
    int regQueue{0};
    int triageQueue{0};
    int specialistsQueue{0};
    bool reg1Active{false};
    bool reg2Active{false};
    bool triageActive{false};
    std::deque<std::string> lastActions;
    int waitSeq{0};
    int regSeq{0};
    int triageSeq{0};
    std::array<int, kSpecialistCount> specialistPids{};
    std::array<bool, kSpecialistCount> specialistOnLeave{};
    std::array<int, kSpecialistCount> specialistHandled{};
    std::array<int, kSpecialistCount> specialistHome{};
    std::array<int, kSpecialistCount> specialistWard{};
    std::array<int, kSpecialistCount> specialistOther{};
    int triageRed{0};
    int triageYellow{0};
    int triageGreen{0};
    int triageSentHome{0};
    int outcomeHome{0};
    int outcomeWard{0};
    int outcomeOther{0};
    int latestSimTime{0};
};

/** @brief Ensure a PatientView exists for id, returning a reference. */
PatientView& ensurePatient(VisualizationState& state, int patientId);

/** @brief Apply patient-specific updates derived from a log entry. */
void applyPatientUpdate(const LogEntry& entry, VisualizationState& state);

/** @brief Apply a log entry to mutate the visualization state. */
void applyLogEntry(const LogEntry& entry, VisualizationState& state);

/** @brief Collect pointers to patients filtered by stage. */
std::vector<const PatientView*> collectPatientsByStage(const VisualizationState& state, Stage stage);
