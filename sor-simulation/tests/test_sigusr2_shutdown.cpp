#include "asserts.hpp"
#include "log_utils.hpp"
#include "sim_runner.hpp"

#include "model/config.hpp"

#include <string>
#include <vector>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path-to-sor_sim>" << std::endl;
        return 1;
    }
    std::string sorSimPath = argv[1];

    Config cfg{};
    cfg.N_waitingRoom = 6;
    cfg.K_registrationThreshold = 3;
    cfg.timeScaleMsPerSimMinute = 5;
    cfg.simulationDurationMinutes = 0;
    cfg.randomSeed = 22222;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 30;
    cfg.triageServiceMs = 10;
    cfg.specialistExamMinMs = 15;
    cfg.specialistExamMaxMs = 30;
    cfg.specialistLeaveMinMs = 20;
    cfg.specialistLeaveMaxMs = 50;
    cfg.reconcileWaitSem = 0;
    cfg.patientGenMinMs = 1;
    cfg.patientGenMaxMs = 4;

    // The runner will send SIGUSR2 after runtimeMs.
    SimResult result = runSimulation(sorSimPath, cfg, 2500);
    ASSERT_EQ(result.exitCode, 0, "Simulation exited with non-zero status");

    LogData log = readLogFile(result.logPath);
    ASSERT_TRUE(!log.rawLines.empty(), "Log file is empty");

    const std::vector<std::string> mustHave = {
        "Director received SIGUSR2",
        "Registration shutting down (SIGUSR2)",
        "Triage shutting down (SIGUSR2)",
    };
    for (const auto& needle : mustHave) {
        bool found = false;
        for (const auto& line : log.rawLines) {
            if (line.find(needle) != std::string::npos) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found, "Missing shutdown log: " + needle);
    }

    std::ostringstream msg;
    msg << "[OK] SIGUSR2 shutdown observed in log=" << result.logPath << "\n";
    writeStdout(msg.str());
    return 0;
}
