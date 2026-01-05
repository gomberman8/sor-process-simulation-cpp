#include "asserts.hpp"
#include "log_utils.hpp"
#include "sim_runner.hpp"

#include "model/config.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path-to-sor_sim>" << std::endl;
        return 1;
    }
    std::string sorSimPath = argv[1];

    Config cfg{};
    cfg.N_waitingRoom = 8;
    cfg.K_registrationThreshold = 4;
    cfg.timeScaleMsPerSimMinute = 5;
    cfg.simulationDurationMinutes = 0;
    cfg.randomSeed = 11111;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 40;
    cfg.triageServiceMs = 10;
    cfg.specialistExamMinMs = 15;
    cfg.specialistExamMaxMs = 30;
    cfg.specialistLeaveMinMs = 20;
    cfg.specialistLeaveMaxMs = 50;
    cfg.reconcileWaitSem = 0;
    cfg.patientGenMinMs = 1;
    cfg.patientGenMaxMs = 4;

    SimResult result = runSimulation(sorSimPath, cfg, 3000);
    ASSERT_EQ(result.exitCode, 0, "Simulation exited with non-zero status");
    ASSERT_TRUE(!result.summaryPath.empty(), "Summary file path missing");

    SummaryData summary = parseSummary(result.summaryPath);
    ASSERT_TRUE(summary.totalPatients > 0, "No patients recorded");

    int triageTotal = summary.triageRed + summary.triageYellow + summary.triageGreen + summary.triageSentHome;
    ASSERT_EQ(triageTotal, summary.totalPatients, "Triage counts do not add up to total patients");

    int outcomesTotal = summary.outcomeHome + summary.outcomeWard + summary.outcomeOther;
    ASSERT_TRUE(outcomesTotal <= summary.totalPatients, "Outcomes exceed total patients");

    std::cout << "[OK] summary invariants satisfied (totalPatients=" << summary.totalPatients
              << ") summary=" << result.summaryPath << std::endl;
    return 0;
}
