#include <iostream>
#include <string>
#include <cstdlib>
#include <iostream>

#include "director.hpp"
#include "logging/logger.hpp"
#include "model/config.hpp"
#include "roles/registration.hpp"

namespace {
bool parseConfig(int argc, char* argv[], Config& cfg, std::string& err) {
    // Defaults if no args provided.
    cfg.N_waitingRoom = 10;
    cfg.K_registrationThreshold = 5;
    cfg.timeScaleMsPerSimMinute = 100;
    cfg.simulationDurationMinutes = 60;
    cfg.totalPatientsTarget = 20;
    cfg.randomSeed = 12345;

    // If arguments beyond program name exist and not in logger mode, expect all fields.
    if (argc > 1 && std::string(argv[1]) != "logger") {
        if (argc < 7) {
            err = "Usage: ./sor_sim <N_waitingRoom> <K_threshold> <simMinutes> <totalPatients> <msPerSimMinute> <seed>";
            return false;
        }
        try {
            cfg.N_waitingRoom = std::stoi(argv[1]);
            cfg.K_registrationThreshold = std::stoi(argv[2]);
            cfg.simulationDurationMinutes = std::stoi(argv[3]);
            cfg.totalPatientsTarget = std::stoi(argv[4]);
            cfg.timeScaleMsPerSimMinute = std::stoi(argv[5]);
            cfg.randomSeed = static_cast<unsigned int>(std::stoul(argv[6]));
        } catch (const std::exception&) {
            err = "Invalid numeric argument";
            return false;
        }
    }

    if (cfg.N_waitingRoom <= 0) {
        err = "N_waitingRoom must be > 0";
        return false;
    }
    if (cfg.K_registrationThreshold < cfg.N_waitingRoom / 2) {
        err = "K_threshold must be >= N/2";
        return false;
    }
    if (cfg.timeScaleMsPerSimMinute <= 0) {
        err = "timeScaleMsPerSimMinute must be > 0";
        return false;
    }
    if (cfg.simulationDurationMinutes <= 0) {
        err = "simulationDurationMinutes must be > 0";
        return false;
    }
    if (cfg.totalPatientsTarget <= 0) {
        err = "totalPatientsTarget must be > 0";
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc >= 2 && std::string(argv[1]) == "logger") {
        if (argc < 4) {
            std::cerr << "Logger mode usage: " << argv[0] << " logger <queueId> <logPath>" << std::endl;
            return EXIT_FAILURE;
        }
        int queueId = std::stoi(argv[2]);
        std::string logPath = argv[3];
        return runLogger(queueId, logPath);
    }

    if (argc >= 2 && std::string(argv[1]) == "registration") {
        if (argc < 3) {
            std::cerr << "Registration mode usage: " << argv[0] << " registration <keyPath>" << std::endl;
            return EXIT_FAILURE;
        }
        Registration reg;
        return reg.run(argv[2]);
    }

    Config cfg{};
    std::string err;
    if (!parseConfig(argc, argv, cfg, err)) {
        std::cerr << "Config error: " << err << std::endl;
        return EXIT_FAILURE;
    }

    // macOS does not support System V IPC reliably; warn early.
#ifdef __APPLE__
    std::cerr << "Warning: System V IPC (msg/sem/shm) may not work on macOS. "
                 "Run on the Debian lab target for correct behavior." << std::endl;
#endif

    Director director;
    int rc = director.run(argv[0], cfg);
    if (rc == 0) {
        std::cout << "SOR simulation WIP – director init and logger handshake OK" << std::endl;
    } else {
        std::cerr << "SOR simulation initialization failed" << std::endl;
    }
    return rc;
}
