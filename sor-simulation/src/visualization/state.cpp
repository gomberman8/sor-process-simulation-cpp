#include "visualization/state.hpp"

#include <algorithm>

PatientView& ensurePatient(VisualizationState& state, int patientId) {
    auto it = state.patients.find(patientId);
    if (it == state.patients.end()) {
        PatientView pv;
        pv.id = patientId;
        auto res = state.patients.emplace(patientId, pv);
        return res.first->second;
    }
    return it->second;
}

namespace {
void trackRegistrationLifecycle(const LogEntry& entry, VisualizationState& state) {
    if (entry.role == "reg1") {
        if (entry.text.find("started") != std::string::npos) state.reg1Active = true;
        if (entry.text.find("shutting down") != std::string::npos) state.reg1Active = false;
    }
    if (entry.role == "reg2") {
        if (entry.text.find("Registration2 started") != std::string::npos) state.reg2Active = true;
        if (entry.text.find("Registration2 shutting down") != std::string::npos) state.reg2Active = false;
    }
    if (entry.role == "triage") {
        if (entry.text.find("Triage started") != std::string::npos) state.triageActive = true;
        if (entry.text.find("Triage shutting down") != std::string::npos) state.triageActive = false;
    }
}
} // namespace

void applyPatientUpdate(const LogEntry& entry, VisualizationState& state) {
    int patientId = -1;
    if (!extractInt(entry.text, "id=", patientId)) {
        return;
    }
    PatientView& pv = ensurePatient(state, patientId);
    pv.pid = entry.pid;
    if (entry.role == "patient" && entry.pid > 0 && pv.patientPid == 0) {
        pv.patientPid = entry.pid;
    }
    pv.lastSimTime = entry.simTime;
    if (entry.simTime > state.latestSimTime) {
        state.latestSimTime = entry.simTime;
    }

    auto setStage = [&](Stage newStage) {
        if (pv.stage == newStage) return;
        pv.stage = newStage;
        switch (newStage) {
            case Stage::WaitingRoom:
                pv.waitOrder = ++state.waitSeq;
                break;
            case Stage::RegistrationQueue:
                pv.regOrder = ++state.regSeq;
                break;
            case Stage::TriageQueue:
                pv.triageOrder = ++state.triageSeq;
                break;
            default:
                break;
        }
    };

    if (entry.text.find("waiting to enter waiting room") != std::string::npos) {
        extractInt(entry.text, "persons=", pv.persons);
        setStage(Stage::OutsideQueue);
        return;
    }

    if (entry.text.find("Patient arrived") != std::string::npos) {
        extractInt(entry.text, "persons=", pv.persons);
        int vipVal = 0;
        if (extractInt(entry.text, "vip=", vipVal)) {
            pv.isVip = vipVal != 0;
        }
        int guardianVal = 0;
        if (extractInt(entry.text, "guardian=", guardianVal)) {
            pv.hasGuardian = guardianVal != 0;
        }
        setStage(Stage::WaitingRoom);
        pv.color = TriageColor::None;
        return;
    }

    if (entry.text.find("Patient registered") != std::string::npos) {
        setStage(Stage::RegistrationQueue);
        return;
    }

    if (entry.role.rfind("reg", 0) == 0 &&
        entry.text.find("Forwarded patient") != std::string::npos) {
        setStage(Stage::TriageQueue);
        extractInt(entry.text, "persons=", pv.persons);
        int vipVal = 0;
        if (extractInt(entry.text, "vip=", vipVal)) {
            pv.isVip = vipVal != 0;
        }
        return;
    }

    if (entry.role == "triage" &&
        entry.text.find("Forwarded patient") != std::string::npos) {
        pv.stage = Stage::SpecialistQueue;
        int colorVal = 3;
        if (extractInt(entry.text, "color=", colorVal)) {
            pv.color = colorFromInt(colorVal);
        }
        int specVal = -1;
        if (extractInt(entry.text, "specialist=", specVal)) {
            pv.specialist = specialistFromInt(specVal);
            switch (pv.color) {
                case TriageColor::Red: state.triageRed++; break;
                case TriageColor::Yellow: state.triageYellow++; break;
                case TriageColor::Green: state.triageGreen++; break;
                default: break;
            }
        }
        extractInt(entry.text, "persons=", pv.persons);
        return;
    }

    if (entry.text.find("Patient sent home from triage") != std::string::npos) {
        setStage(Stage::SentHome);
        pv.specialist = SpecialistType::None;
        pv.color = TriageColor::None;
        state.triageSentHome++;
        return;
    }

    if (entry.role == "specialist" &&
        entry.text.find("Received patient") != std::string::npos) {
        setStage(Stage::SpecialistActive);
        int colorVal = 3;
        if (extractInt(entry.text, "color=", colorVal)) {
            pv.color = colorFromInt(colorVal);
        }
        int specVal = -1;
        if (extractInt(entry.text, "specIdx=", specVal)) {
            pv.specialist = specialistFromInt(specVal);
        }
        extractInt(entry.text, "persons=", pv.persons);
        return;
    }

    if (entry.role == "specialist" &&
        entry.text.find("Handled patient") != std::string::npos) {
        setStage(Stage::Done);
        int colorVal = 3;
        if (extractInt(entry.text, "color=", colorVal)) {
            pv.color = colorFromInt(colorVal);
        }
        int specVal = -1;
        if (extractInt(entry.text, "specIdx=", specVal)) {
            pv.specialist = specialistFromInt(specVal);
        }
        extractInt(entry.text, "persons=", pv.persons);
        size_t pos = entry.text.find("outcome=");
        if (pos != std::string::npos) {
            pv.outcome = entry.text.substr(pos + 8);
            auto space = pv.outcome.find(' ');
            if (space != std::string::npos) {
                pv.outcome = pv.outcome.substr(0, space);
            }
        }
        SpecialistType specType = pv.specialist;
        if (specType != SpecialistType::None) {
            int idx = static_cast<int>(specType);
            state.specialistHandled[idx]++;
            if (pv.outcome == "home") {
                state.specialistHome[idx]++;
                state.outcomeHome++;
            } else if (pv.outcome == "ward") {
                state.specialistWard[idx]++;
                state.outcomeWard++;
            } else {
                state.specialistOther[idx]++;
                state.outcomeOther++;
            }
        }
    }
}

void applyLogEntry(const LogEntry& entry, VisualizationState& state) {
    if (entry.hasMetrics) {
        state.waitingCurrent = entry.waitingCurrent;
        if (entry.waitingCapacity > 0) {
            state.waitingCapacity = entry.waitingCapacity;
        }
        state.regQueue = entry.regQueue;
        state.triageQueue = entry.triageQueue;
        state.specialistsQueue = entry.specialistsQueue;
    }

    if (entry.role == "specialist" && entry.text.find("started") != std::string::npos) {
        SpecialistType t = specialistFromLabel(entry.text);
        if (t != SpecialistType::None) {
            state.specialistPids[static_cast<int>(t)] = entry.pid;
        }
    }

    std::string action = "[" + std::to_string(entry.simTime) + "] " + entry.role + ": " + entry.text;
    state.lastActions.push_back(action);
    const size_t maxActions = 14;
    while (state.lastActions.size() > maxActions) {
        state.lastActions.pop_front();
    }

    trackRegistrationLifecycle(entry, state);
    applyPatientUpdate(entry, state);
}

std::vector<const PatientView*> collectPatientsByStage(const VisualizationState& state, Stage stage) {
    std::vector<const PatientView*> list;
    for (const auto& kv : state.patients) {
        if (kv.second.stage == stage) {
            list.push_back(&kv.second);
        }
    }
    return list;
}
