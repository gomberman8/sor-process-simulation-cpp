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
    cfg.N_waitingRoom = 20;
    cfg.K_registrationThreshold = 10;
    cfg.timeScaleMsPerSimMinute = 5;
    cfg.simulationDurationMinutes = 0;
    cfg.randomSeed = 44444;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 20;
    cfg.triageServiceMs = 5;
    cfg.specialistExamMinMs = 10;
    cfg.specialistExamMaxMs = 20;
    cfg.specialistLeaveMinMs = 15;
    cfg.specialistLeaveMaxMs = 30;
    cfg.reconcileWaitSem = 0;
    cfg.patientGenMinMs = 1;
    cfg.patientGenMaxMs = 3;

    SimResult result = runSimulation(sorSimPath, cfg, 4000);
    ASSERT_EQ(result.exitCode, 0, "Simulation exited with non-zero status");
    ASSERT_TRUE(!result.summaryPath.empty(), "Summary file missing");

    SummaryData summary = parseSummary(result.summaryPath);
    ASSERT_TRUE(summary.totalPatients > 20, "Too few patients to evaluate distribution");

    int triageTotal = summary.triageRed + summary.triageYellow + summary.triageGreen + summary.triageSentHome;
    ASSERT_EQ(triageTotal, summary.totalPatients, "Triage counts mismatch total");

    double pctRed = (summary.triageRed * 100.0) / triageTotal;
    double pctYellow = (summary.triageYellow * 100.0) / triageTotal;
    double pctGreen = (summary.triageGreen * 100.0) / triageTotal;
    double pctHome = (summary.triageSentHome * 100.0) / triageTotal;

    // Wide bands to accommodate randomness but ensure proportions are roughly respected.
    ASSERT_TRUE(pctRed >= 2.0 && pctRed <= 25.0, "Red triage proportion out of expected band");
    ASSERT_TRUE(pctYellow >= 20.0 && pctYellow <= 60.0, "Yellow triage proportion out of expected band");
    ASSERT_TRUE(pctGreen >= 20.0 && pctGreen <= 75.0, "Green triage proportion out of expected band");
    ASSERT_TRUE(pctHome >= 0.0 && pctHome <= 15.0, "Sent-home proportion out of expected band");

    std::cout << "[OK] Triage distribution within bands "
              << "(R=" << pctRed << "% Y=" << pctYellow << "% G=" << pctGreen
              << "% Home=" << pctHome << "%) summary=" << result.summaryPath << std::endl;
    return 0;
}
