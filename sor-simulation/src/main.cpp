#include <iostream>
#include <string>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "director.hpp"
#include "logging/logger.hpp"
#include "model/config.hpp"
#include "roles/registration.hpp"
#include "roles/triage.hpp"
#include "roles/specialist.hpp"
#include "roles/patient_generator.hpp"
#include "roles/patient.hpp"
#include "visualization/visualizer.hpp"

namespace {
/**
 * @brief Load key/value pairs from config file with defaults and validation.
 * @param path path to config file.
 * @param cfg destination structure to fill.
 * @param err error message on failure.
 * @return true if parsed and validated, false otherwise.
 */
bool parseConfigFile(const std::string& path, Config& cfg, std::string& err) {
    std::ifstream in(path);
    if (!in) {
        err = "Cannot open config file: " + path;
        return false;
    }
    // Defaults in case some keys are absent.
    cfg.N_waitingRoom = 30;
    cfg.K_registrationThreshold = 0; // 0 means auto = N/2
    cfg.timeScaleMsPerSimMinute = 20;
    cfg.simulationDurationMinutes = 0;
    cfg.randomSeed = 12345;
    cfg.visualizerRenderIntervalMs = 200;
    cfg.registrationServiceMs = 25;
    cfg.triageServiceMs = 0;
    cfg.specialistExamMinMs = 10;
    cfg.specialistExamMaxMs = 40;

    auto trim = [](const std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string();
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));
        try {
            if (key == "N_waitingRoom") cfg.N_waitingRoom = std::stoi(val);
            else if (key == "K_registrationThreshold") cfg.K_registrationThreshold = std::stoi(val);
            else if (key == "simulationDurationMinutes") cfg.simulationDurationMinutes = std::stoi(val);
            else if (key == "timeScaleMsPerSimMinute") cfg.timeScaleMsPerSimMinute = std::stoi(val);
            else if (key == "randomSeed") cfg.randomSeed = static_cast<unsigned int>(std::stoul(val));
            else if (key == "visualizerRenderIntervalMs") cfg.visualizerRenderIntervalMs = std::stoi(val);
            else if (key == "registrationServiceMs") cfg.registrationServiceMs = std::stoi(val);
            else if (key == "triageServiceMs") cfg.triageServiceMs = std::stoi(val);
            else if (key == "specialistExamMinMs") cfg.specialistExamMinMs = std::stoi(val);
            else if (key == "specialistExamMaxMs") cfg.specialistExamMaxMs = std::stoi(val);
        } catch (const std::exception&) {
            err = "Invalid value for key: " + key;
            return false;
        }
    }
    if (cfg.N_waitingRoom <= 0) {
        err = "N_waitingRoom must be > 0";
        return false;
    }
    if (cfg.K_registrationThreshold <= 0) {
        cfg.K_registrationThreshold = cfg.N_waitingRoom / 2;
    }
    if (cfg.K_registrationThreshold < cfg.N_waitingRoom / 2) {
        err = "K_registrationThreshold must be >= N/2";
        return false;
    }
    if (cfg.timeScaleMsPerSimMinute <= 0) {
        err = "timeScaleMsPerSimMinute must be > 0";
        return false;
    }
    if (cfg.visualizerRenderIntervalMs <= 0) {
        err = "visualizerRenderIntervalMs must be > 0";
        return false;
    }
    if (cfg.registrationServiceMs < 0) {
        err = "registrationServiceMs must be >= 0";
        return false;
    }
    if (cfg.triageServiceMs < 0) {
        err = "triageServiceMs must be >= 0";
        return false;
    }
    if (cfg.specialistExamMinMs <= 0 || cfg.specialistExamMaxMs <= 0 ||
        cfg.specialistExamMaxMs < cfg.specialistExamMinMs) {
        err = "specialistExamMinMs/maxMs must be >0 and max>=min";
        return false;
    }
    return true;
}
} // namespace

// Entry point dispatches run modes (simulator, visualizer, logger, or individual roles) and shares IPC via ftok keys from argv[0].
int main(int argc, char* argv[]) {
    if (argc >= 3 && std::string(argv[1]) == "visualize") {
        int intervalMs = 200;
        try {
            if (argc >= 4) {
                intervalMs = std::stoi(argv[3]);
            }
        } catch (const std::exception&) {
            intervalMs = 200;
        }
        return runVisualizer(argv[2], intervalMs);
    }

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
        return reg.run(argv[2], false);
    }

    if (argc >= 2 && std::string(argv[1]) == "registration2") {
        if (argc < 3) {
            std::cerr << "Registration2 mode usage: " << argv[0] << " registration2 <keyPath>" << std::endl;
            return EXIT_FAILURE;
        }
        Registration reg;
        return reg.run(argv[2], true);
    }

    if (argc >= 2 && std::string(argv[1]) == "triage") {
        if (argc < 3) {
            std::cerr << "Triage mode usage: " << argv[0] << " triage <keyPath>" << std::endl;
            return EXIT_FAILURE;
        }
        Triage triage;
        return triage.run(argv[2]);
    }

    if (argc >= 2 && std::string(argv[1]) == "specialist") {
        if (argc < 4) {
            std::cerr << "Specialist mode usage: " << argv[0] << " specialist <keyPath> <typeInt>" << std::endl;
            return EXIT_FAILURE;
        }
        SpecialistType type = static_cast<SpecialistType>(std::stoi(argv[3]));
        Specialist spec;
        return spec.run(argv[2], type);
    }

    if (argc >= 2 && std::string(argv[1]) == "patient_generator") {
        if (argc < 8) {
            std::cerr << "Patient generator usage: " << argv[0]
                      << " patient_generator <keyPath> <N> <K> <simMinutes> <msPerMinute> <seed>"
                      << std::endl;
            return EXIT_FAILURE;
        }
        Config cfg{};
        cfg.N_waitingRoom = std::stoi(argv[3]);
        cfg.K_registrationThreshold = std::stoi(argv[4]);
        cfg.simulationDurationMinutes = std::stoi(argv[5]);
        cfg.timeScaleMsPerSimMinute = std::stoi(argv[6]);
        cfg.randomSeed = static_cast<unsigned int>(std::stoul(argv[7]));
        cfg.visualizerRenderIntervalMs = 200;
        cfg.registrationServiceMs = 25;
        cfg.triageServiceMs = 0;
        cfg.specialistExamMinMs = 10;
        cfg.specialistExamMaxMs = 40;
        PatientGenerator gen;
        return gen.run(argv[2], cfg);
    }

    if (argc >= 2 && std::string(argv[1]) == "patient") {
        if (argc < 8) {
            std::cerr << "Patient usage: " << argv[0]
                      << " patient <keyPath> <id> <age> <isVip> <hasGuardian> <personsCount>"
                      << std::endl;
            return EXIT_FAILURE;
        }
        Patient pat;
        int id = std::stoi(argv[3]);
        int age = std::stoi(argv[4]);
        bool isVip = std::stoi(argv[5]) != 0;
        bool hasGuardian = std::stoi(argv[6]) != 0;
        int personsCount = std::stoi(argv[7]);
        return pat.run(argv[2], id, age, isVip, hasGuardian, personsCount);
    }

    Config cfg{};
    std::string err;
    bool configOk = false;

    // Prefer config file; legacy positional numeric args still supported.
    if (argc >= 2 && std::string(argv[1]) == "--config") {
        if (argc < 3) {
            std::cerr << "Usage: ./sor_sim --config <path>" << std::endl;
            return EXIT_FAILURE;
        }
        configOk = parseConfigFile(argv[2], cfg, err);
    } else if (argc >= 6) {
        try {
            cfg.N_waitingRoom = std::stoi(argv[1]);
            cfg.K_registrationThreshold = std::stoi(argv[2]);
            cfg.simulationDurationMinutes = std::stoi(argv[3]);
            cfg.timeScaleMsPerSimMinute = std::stoi(argv[4]);
            cfg.randomSeed = static_cast<unsigned int>(std::stoul(argv[5]));
            cfg.visualizerRenderIntervalMs = 200;
            cfg.registrationServiceMs = 25;
            cfg.triageServiceMs = 0;
            cfg.specialistExamMinMs = 10;
            cfg.specialistExamMaxMs = 40;
            // basic validation
            if (cfg.N_waitingRoom <= 0) {
                err = "N_waitingRoom must be > 0";
                configOk = false;
            } else {
                if (cfg.K_registrationThreshold <= 0) {
                    cfg.K_registrationThreshold = cfg.N_waitingRoom / 2;
                }
                if (cfg.K_registrationThreshold < cfg.N_waitingRoom / 2 ||
                    cfg.timeScaleMsPerSimMinute <= 0) {
                    err = "Invalid numeric configuration values";
                    configOk = false;
                } else {
                    configOk = true;
                }
            }
        } catch (const std::exception&) {
            err = "Invalid numeric argument";
            configOk = false;
        }
    } else {
        // default config file paths: current dir then parent
        std::string errLocal;
        configOk = parseConfigFile("config.cfg", cfg, errLocal);
        if (!configOk) {
            std::string errParent;
            configOk = parseConfigFile("../config.cfg", cfg, errParent);
            if (!configOk) {
                err = "Cannot open config file: tried config.cfg and ../config.cfg";
            }
        } else {
            err.clear();
        }
    }

    if (!configOk) {
        std::cerr << "Config error: " << err << std::endl;
        return EXIT_FAILURE;
    }

    // macOS does not support System V IPC reliably; warn early.
#ifdef __APPLE__
    std::cerr << "Warning: System V IPC (msg/sem/shm) may not work on macOS. "
                 "Run on the Debian lab target for correct behavior." << std::endl;
#endif

    // Compute log path upfront so the visualizer can attach immediately.
    std::string logPath = "sor_run_" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".log";
    pid_t vizPid = -1;

    vizPid = fork();
    if (vizPid == -1) {
        std::perror("Failed to fork visualizer");
    } else if (vizPid == 0) {
        std::vector<char*> args;
        args.push_back(const_cast<char*>(argv[0]));
        args.push_back(const_cast<char*>("visualize"));
        args.push_back(const_cast<char*>(logPath.c_str()));
        std::string intervalStr = std::to_string(cfg.visualizerRenderIntervalMs);
        args.push_back(const_cast<char*>(intervalStr.c_str()));
        args.push_back(nullptr);
        execv(argv[0], args.data());
        std::perror("Failed to exec visualizer");
        _exit(1);
    }

    Director director;
    int rc = director.run(argv[0], cfg, &logPath);
    if (rc == 0) {
        std::cout << "SOR simulation WIP – director init and logger handshake OK" << std::endl;
        const std::string& summaryPath = director.lastSummaryPath();
        if (!summaryPath.empty()) {
            std::ifstream in(summaryPath);
            if (in) {
                std::cout << "\n=== " << summaryPath << " ===\n";
                std::cout << in.rdbuf() << std::flush;
            } else {
                std::cerr << "Failed to open summary file: " << summaryPath << std::endl;
            }
        } else {
            std::cerr << "No summary file (sor_summary_*.txt) found to display" << std::endl;
        }
    } else {
        std::cerr << "SOR simulation initialization failed" << std::endl;
    }

    if (vizPid > 0) {
        kill(vizPid, SIGTERM);
        waitpid(vizPid, nullptr, 0);
    }
    return rc;
}
