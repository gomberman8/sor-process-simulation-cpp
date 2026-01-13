#include "asserts.hpp"
#include "log_utils.hpp"
#include "sim_runner.hpp"

#include "model/config.hpp"

#include <string>
#include <sstream>

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
    cfg.randomSeed = 55555;
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
    ASSERT_TRUE(summary.totalPatients > 20, "Too few patients to evaluate outcomes");

    int triageSentHome = summary.triageSentHome;
    int seenBySpecialist = summary.totalPatients - triageSentHome;
    int outcomesTotal = summary.outcomeHome + summary.outcomeWard + summary.outcomeOther;
    ASSERT_TRUE(outcomesTotal <= seenBySpecialist, "Outcomes exceed patients reaching specialists");

    if (seenBySpecialist > 0) {
        double pctHome = (summary.outcomeHome * 100.0) / seenBySpecialist;
        double pctWard = (summary.outcomeWard * 100.0) / seenBySpecialist;
        double pctOther = (summary.outcomeOther * 100.0) / seenBySpecialist;
        ASSERT_TRUE(pctHome >= 70.0, "Home outcome proportion too low");
        ASSERT_TRUE(pctWard <= 30.0, "Ward outcome proportion too high");
        ASSERT_TRUE(pctOther <= 5.0, "Other-facility outcome proportion too high");
        std::ostringstream msg;
        msg << "[OK] Outcomes within bands (Home=" << pctHome
            << "% Ward=" << pctWard << "% Other=" << pctOther
            << "%) summary=" << result.summaryPath << "\n";
        writeStdout(msg.str());
    } else {
        writeStdout("[WARN] No patients reached specialists; skipping proportion checks\n");
    }
    return 0;
}
