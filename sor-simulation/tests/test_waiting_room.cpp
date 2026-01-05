#include "asserts.hpp"
#include "log_utils.hpp"
#include "sim_runner.hpp"

#include "model/config.hpp"

#include <algorithm>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path-to-sor_sim>" << std::endl;
        return 1;
    }
    std::string sorSimPath = argv[1];

    Config cfg{};
    cfg.N_waitingRoom = 6;
    cfg.K_registrationThreshold = 3; // >= N/2
    cfg.timeScaleMsPerSimMinute = 5;
    cfg.simulationDurationMinutes = 0; // run until SIGUSR2
    cfg.randomSeed = 12345;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 30;
    cfg.triageServiceMs = 10;
    cfg.specialistExamMinMs = 20;
    cfg.specialistExamMaxMs = 40;
    cfg.specialistLeaveMinMs = 20;
    cfg.specialistLeaveMaxMs = 60;
    cfg.reconcileWaitSem = 0;
    cfg.patientGenMinMs = 1;
    cfg.patientGenMaxMs = 5;

    // Run for a short real-time window; the stopper thread sends SIGUSR2.
    SimResult result = runSimulation(sorSimPath, cfg, 2500);
    ASSERT_EQ(result.exitCode, 0, "Simulation exited with non-zero status");

    LogData log = readLogFile(result.logPath);
    ASSERT_TRUE(!log.entries.empty(), "Log file is empty, cannot evaluate capacity");

    int maxInside = 0;
    bool sawFull = false;
    for (const auto& entry : log.entries) {
        if (!entry.hasMetrics) continue;
        if (entry.waitingCapacity > 0) {
            ASSERT_EQ(entry.waitingCapacity, cfg.N_waitingRoom,
                      "Metrics waiting room capacity mismatch");
        }
        maxInside = std::max(maxInside, entry.waitingCurrent);
        if (entry.waitingCurrent == cfg.N_waitingRoom) {
            sawFull = true;
        }
        ASSERT_TRUE(entry.waitingCurrent <= cfg.N_waitingRoom,
                    "Waiting room occupancy exceeded capacity");
    }

    bool sawWaitingOutside = false;
    for (const auto& line : log.rawLines) {
        if (line.find("Patient waiting to enter waiting room") != std::string::npos) {
            sawWaitingOutside = true;
            break;
        }
    }

    ASSERT_TRUE(sawFull, "Simulation never filled waiting room to test limit");
    ASSERT_TRUE(sawWaitingOutside, "No evidence of patients waiting outside when full");
    std::cout << "[OK] maxInside=" << maxInside << " capacity=" << cfg.N_waitingRoom
              << " log=" << result.logPath << std::endl;
    return 0;
}
