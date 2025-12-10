#include "visualization/renderer.hpp"

#include "visualization/render_utils.hpp"
#include "visualization/log_parser.hpp"

#include <array>
#include <algorithm>
#include <iostream>
#include <sstream>


// Render waiting room/triage/entrance overview with live stats (see header).
void renderTopSection(const VisualizationState& state) {
    const int totalWidth = 118;
    const int colWaiting = 60;
    const int colTriage = 24;
    const int colEntrance = 30;
    std::string border(totalWidth, '=');
    std::cout << border << "\n";

    // One-line stats above everything
    std::stringstream statsLine;
    statsLine << "Elapsed " << state.latestSimTime << "m | "
              << "Triage R/Y/G " << state.triageRed << "/" << state.triageYellow << "/" << state.triageGreen
              << " home " << state.triageSentHome << " | "
              << "Disp H/W/O " << state.outcomeHome << "/" << state.outcomeWard << "/" << state.outcomeOther;
    std::string statsPadded = padded(statsLine.str(), totalWidth - 2);
    std::cout << "|" << statsPadded << "|\n";
    std::cout << border << "\n";

    std::string reg2Status = state.reg2Active ? "REG2 ON" : "REG2 off";

    std::stringstream headWait;
    std::stringstream headReg;
    // counts based on current staged patients to avoid stale metrics
    auto waitingList = collectPatientsByStage(state, Stage::WaitingRoom);
    auto regList = collectPatientsByStage(state, Stage::RegistrationQueue);
    auto triageList = collectPatientsByStage(state, Stage::TriageQueue);
    size_t triageCount = triageList.size();

    auto personCount = [](const PatientView* p) {
        return p->persons > 0 ? p->persons : 1;
    };
    int personsWaiting = 0;
    for (auto* p : waitingList) personsWaiting += personCount(p);
    for (auto* p : regList) personsWaiting += personCount(p);
    int patientsWaiting = static_cast<int>(waitingList.size() + regList.size());

    int capacityPersons = state.waitingCapacity > 0 ? state.waitingCapacity : 0;
    int usedPersons = personsWaiting;
    if (capacityPersons > 0 && state.waitSem >= 0) {
        usedPersons = capacityPersons - state.waitSem;
    } else if (state.waitingCurrent > 0) {
        usedPersons = state.waitingCurrent;
    }

    headWait << "WAITING ROOM ";
    if (capacityPersons > 0) {
        headWait << usedPersons << "/" << capacityPersons << " persons";
    } else {
        headWait << patientsWaiting;
    }
    headWait << " (patients " << patientsWaiting << ")";
    headReg << "TRIAGE QUEUE tQ=" << triageCount;
    auto entranceList = collectPatientsByStage(state, Stage::OutsideQueue);
    std::stringstream headEnt;
    headEnt << "ENTRANCE outQ=" << entranceList.size() << " " << reg2Status;

    std::cout << "|" << padded(headWait.str(), colWaiting)
              << "|" << padded(headReg.str(), colTriage)
              << "|" << padded(headEnt.str(), colEntrance) << "|\n";

    std::vector<const PatientView*> waitingCombined;
    waitingCombined.insert(waitingCombined.end(), waitingList.begin(), waitingList.end());
    waitingCombined.insert(waitingCombined.end(), regList.begin(), regList.end());

    std::sort(waitingCombined.begin(), waitingCombined.end(), [](const PatientView* a, const PatientView* b) {
        int stagePriA = (a->stage == Stage::RegistrationQueue) ? 0 : 1;
        int stagePriB = (b->stage == Stage::RegistrationQueue) ? 0 : 1;
        if (stagePriA != stagePriB) return stagePriA < stagePriB;
        int orderA = (a->stage == Stage::RegistrationQueue) ? a->regOrder : a->waitOrder;
        int orderB = (b->stage == Stage::RegistrationQueue) ? b->regOrder : b->waitOrder;
        if (orderA < 0) orderA = a->lastSimTime;
        if (orderB < 0) orderB = b->lastSimTime;
        return orderA < orderB;
    });

    // Trim by person capacity if known to avoid displaying more than can fit.
    if (capacityPersons > 0) {
        std::vector<const PatientView*> trimmed;
        int remaining = capacityPersons;
        for (auto* p : waitingCombined) {
            int need = personCount(p);
            if (need <= remaining) {
                trimmed.push_back(p);
                remaining -= need;
            } else {
                break;
            }
        }
        waitingCombined.swap(trimmed);
    }

    std::vector<const PatientView*> triageOrdered = triageList;
    std::sort(triageOrdered.begin(), triageOrdered.end(), [](const PatientView* a, const PatientView* b) {
        int oa = a->triageOrder < 0 ? a->lastSimTime : a->triageOrder;
        int ob = b->triageOrder < 0 ? b->lastSimTime : b->triageOrder;
        return oa < ob;
    });

    std::vector<std::string> waitingTokens;
    for (auto* p : waitingCombined) {
        Stage renderStage = p->registrationInProgress ? Stage::RegistrationQueue : Stage::WaitingRoom;
        waitingTokens.push_back(formatPatientLabel(*p, renderStage));
    }

    std::vector<std::string> triageTokens;
    for (auto* p : triageOrdered) triageTokens.push_back(formatPatientLabel(*p, Stage::TriageQueue));

    std::vector<std::string> entranceTokens;
    for (auto* p : entranceList) entranceTokens.push_back(formatPatientLabel(*p, Stage::OutsideQueue));

    auto waitingLines = wrapTokens(waitingTokens, static_cast<size_t>(colWaiting - 2));
    auto regLines = wrapTokens(triageTokens, static_cast<size_t>(colTriage - 2));
    auto entranceLines = wrapTokens(entranceTokens, static_cast<size_t>(colEntrance - 2));

    size_t minRows = static_cast<size_t>((state.waitingCapacity + 3) / 4); // assume roughly 4 items per line
    size_t waitingHeight = waitingLines.size();
    if (waitingHeight < minRows) waitingHeight = minRows;

    // Cap entrance height to waiting-height; show overflow with ellipsis.
    size_t entranceCap = waitingHeight > 0 ? waitingHeight : entranceLines.size();
    if (entranceCap > 0 && entranceLines.size() > entranceCap) {
        entranceLines.resize(entranceCap);
        entranceLines.back() = "...";
    }

    size_t rows = std::max({waitingHeight, regLines.size(), entranceLines.size()});
    if (rows < minRows) rows = minRows;
    for (size_t i = 0; i < rows; ++i) {
        std::string w = i < waitingLines.size() ? waitingLines[i] : "";
        std::string r = i < regLines.size() ? regLines[i] : "";
        std::string e = i < entranceLines.size() ? entranceLines[i] : "";
        std::cout << "|" << padded(w, colWaiting)
                  << "|" << padded(r, colTriage)
                  << "|" << padded(e, colEntrance) << "|\n";
    }
}

// Render the trailing set of recent log actions (see header).
void renderActions(const VisualizationState& state) {
    const int totalWidth = 118;
    const int rightWidth = 30;
    const int leftWidth = totalWidth - rightWidth - 3;
    std::string separator(totalWidth, '-');
    std::cout << separator << "\n";
    std::cout << "|" << padded(" LAST ACTIONS", leftWidth) << "|" << padded("", rightWidth) << "|\n";
    size_t actionsToShow = std::min<size_t>(state.lastActions.size(), 10);
    for (size_t i = 0; i < actionsToShow; ++i) {
        const std::string& act = state.lastActions[state.lastActions.size() - actionsToShow + i];
        std::cout << "|" << padded(act, leftWidth) << "|" << padded("", rightWidth) << "|\n";
    }
    std::string border(totalWidth, '=');
    std::cout << border << "\n";
}

// Render specialist queues/active patients and per-specialist stats (see header).
void renderSpecialists(const VisualizationState& state) {
    const int totalWidth = 118;
    const int colWidth = totalWidth / 3 - 1;
    std::array<std::vector<const PatientView*>, 6> queues{};
    std::array<std::vector<const PatientView*>, 6> active{};
    auto specialistLabel = [&](int idx) {
        SpecialistType type = static_cast<SpecialistType>(idx);
        if (state.specialistOnLeave[idx]) {
            const char* reset = "\033[0m";
            const char* bg = "\033[41m";
            const char* fg = "\033[97m";
            return std::string(bg) + fg + specialistName(type) + reset;
        }
        return specialistNameColored(type);
    };

    for (const auto& kv : state.patients) {
        const auto& p = kv.second;
        if (p.specialist == SpecialistType::None) continue;
        int idx = static_cast<int>(p.specialist);
        if (idx < 0 || idx >= 6) continue;
        if (p.stage == Stage::SpecialistQueue) queues[idx].push_back(&p);
        else if (p.stage == Stage::SpecialistActive) active[idx].push_back(&p);
    }
    for (int i = 0; i < 6; ++i) {
        std::sort(queues[i].begin(), queues[i].end(), [](const PatientView* a, const PatientView* b) {
            return a->id < b->id;
        });
        std::sort(active[i].begin(), active[i].end(), [](const PatientView* a, const PatientView* b) {
            return a->id < b->id;
        });
    }

    std::cout << "|" << padded(" SPECIALISTS", totalWidth - 2) << "|\n";
    for (int row = 0; row < 2; ++row) {
        std::array<std::string, 3> headers{};
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (idx >= 6) continue;
            std::stringstream ss;
            ss << specialistLabel(idx) << " pid=" << state.specialistPids[idx] << " q="
               << queues[idx].size() << " act=" << active[idx].size();
            headers[col] = padded(ss.str(), colWidth);
        }
        std::cout << "|" << headers[0] << "|" << headers[1] << "|" << headers[2] << "|\n";

        // stats line per specialist directly under header
        std::array<std::string, 3> statVals{};
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (idx >= 6) continue;
            int handled = state.specialistHandled[idx];
            int h = state.specialistHome[idx];
            int w = state.specialistWard[idx];
            int o = state.specialistOther[idx];
            std::stringstream ss;
            ss << "Handled=" << handled << " H/W/O " << h << "/" << w << "/" << o;
            statVals[col] = padded(ss.str(), colWidth);
        }
        std::cout << "|" << statVals[0] << "|" << statVals[1] << "|" << statVals[2] << "|\n";

        // queue header line
        std::array<std::string, 3> queueHdr{};
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (idx >= 6) continue;
            queueHdr[col] = padded("Queue", colWidth);
        }
        std::cout << "|" << queueHdr[0] << "|" << queueHdr[1] << "|" << queueHdr[2] << "|\n";

        // queue lines
        std::array<std::vector<std::string>, 3> queueLines{};
        std::array<size_t, 3> queueHeights{};
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (idx >= 6) continue;
            std::vector<std::string> tokens;
            for (auto* p : queues[idx]) tokens.push_back(formatPatientLabel(*p, Stage::SpecialistQueue));
            queueLines[col] = wrapTokens(tokens, static_cast<size_t>(colWidth - 2));
            queueHeights[col] = queueLines[col].size();
        }
        size_t maxQueueRows = std::max({queueHeights[0], queueHeights[1], queueHeights[2]});
        for (size_t r = 0; r < maxQueueRows; ++r) {
            std::array<std::string, 3> rowVals{};
            for (int col = 0; col < 3; ++col) {
                rowVals[col] = r < queueLines[col].size() ? queueLines[col][r] : "";
                rowVals[col] = padded(rowVals[col], colWidth);
            }
            std::cout << "|" << rowVals[0] << "|" << rowVals[1] << "|" << rowVals[2] << "|\n";
        }

        // active line
        std::array<std::string, 3> actVals{};
        std::array<std::string, 3> actHdr{};
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (idx >= 6) continue;
            actHdr[col] = padded("In room", colWidth);
            std::vector<std::string> tokens;
            for (auto* p : active[idx]) tokens.push_back(formatPatientLabel(*p, Stage::SpecialistActive));
            std::string combined;
            if (!tokens.empty()) {
                combined = tokens[0];
                for (size_t i = 1; i < tokens.size(); ++i) {
                    combined += " " + tokens[i];
                }
            }
            actVals[col] = padded(combined, colWidth);
        }
        std::cout << "|" << actHdr[0] << "|" << actHdr[1] << "|" << actHdr[2] << "|\n";
        std::cout << "|" << actVals[0] << "|" << actVals[1] << "|" << actVals[2] << "|\n";

        // separator between specialist rows for readability
        std::string sep(colWidth, '-');
        std::cout << "|" << padded(sep, colWidth) << "|" << padded(sep, colWidth) << "|" << padded(sep, colWidth) << "|\n";
    }
    std::string border(totalWidth, '=');
    std::cout << border << "\n";
}

// Full frame render: clears screen then draws all sections (see header).
void render(const VisualizationState& state) {
    // Clear screen (including scrollback) and move cursor home so each frame replaces the previous one.
    std::cout << "\033[H\033[2J\033[3J";
    renderTopSection(state);
    renderActions(state);
    renderSpecialists(state);
    std::cout.flush();
}
