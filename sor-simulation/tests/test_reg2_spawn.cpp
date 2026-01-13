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
    cfg.N_waitingRoom = 9;
    cfg.K_registrationThreshold = 5; // >= N/2 to satisfy constraints
    cfg.timeScaleMsPerSimMinute = 5;
    cfg.simulationDurationMinutes = 0;
    cfg.randomSeed = 54321;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 120; // slow to build queue
    cfg.triageServiceMs = 10;
    cfg.specialistExamMinMs = 10;
    cfg.specialistExamMaxMs = 25;
    cfg.specialistLeaveMinMs = 20;
    cfg.specialistLeaveMaxMs = 40;
    cfg.reconcileWaitSem = 0;
    cfg.patientGenMinMs = 1;
    cfg.patientGenMaxMs = 4;

    // Run long enough to let queue build and reg2 spawn.
    SimResult result = runSimulation(sorSimPath, cfg, 3000);
    ASSERT_EQ(result.exitCode, 0, "Simulation exited with non-zero status");

    LogData log = readLogFile(result.logPath);
    ASSERT_TRUE(!log.rawLines.empty(), "Log file is empty");

    bool sawSpawn = false;
    bool sawClose = false;
    for (const auto& line : log.rawLines) {
        if (line.find("Registration2 spawned") != std::string::npos) {
            sawSpawn = true;
        }
        if (line.find("Registration2 closing") != std::string::npos) {
            sawClose = true;
        }
    }

    ASSERT_TRUE(sawSpawn, "Director never spawned Registration2");
    // Close may depend on queue drain; tolerate absence but log.
    if (!sawClose) {
        std::cerr << "[WARN] Registration2 did not close during the run (queue may have stayed high)\n";
    }

    std::ostringstream msg;
    msg << "[OK] reg2 spawn observed in log=" << result.logPath << "\n";
    writeStdout(msg.str());
    return 0;
}
