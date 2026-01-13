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
    cfg.N_waitingRoom = 10;
    cfg.K_registrationThreshold = 5;
    cfg.timeScaleMsPerSimMinute = 5;
    cfg.simulationDurationMinutes = 0;
    cfg.randomSeed = 33333;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 35;
    cfg.triageServiceMs = 10;
    cfg.specialistExamMinMs = 15;
    cfg.specialistExamMaxMs = 30;
    cfg.specialistLeaveMinMs = 20;
    cfg.specialistLeaveMaxMs = 50;
    cfg.reconcileWaitSem = 0;
    cfg.patientGenMinMs = 1;
    cfg.patientGenMaxMs = 3;

    SimResult result = runSimulation(sorSimPath, cfg, 3000);
    ASSERT_EQ(result.exitCode, 0, "Simulation exited with non-zero status");

    LogData log = readLogFile(result.logPath);
    ASSERT_TRUE(!log.rawLines.empty(), "Log file is empty");

    bool sawGuardian = false;
    bool guardianThroughTriage = false;
    bool sawVip = false;
    for (const auto& line : log.rawLines) {
        if (line.find("guardian=1") != std::string::npos && line.find("persons=2") != std::string::npos) {
            sawGuardian = true;
        }
        if (line.find("persons=2") != std::string::npos &&
            (line.find("Forwarded patient") != std::string::npos || line.find("Handled patient") != std::string::npos)) {
            guardianThroughTriage = true;
        }
        if (line.find("vip=1") != std::string::npos) {
            sawVip = true;
        }
    }

    ASSERT_TRUE(sawGuardian, "No child+guardian pair observed");
    ASSERT_TRUE(guardianThroughTriage, "Guardian pair did not flow through triage/specialist logs");
    ASSERT_TRUE(sawVip, "No VIP patient observed");

    std::ostringstream msg;
    msg << "[OK] VIP and guardian cases observed in log=" << result.logPath << "\n";
    writeStdout(msg.str());
    return 0;
}
