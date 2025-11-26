#include "roles/patient_generator.hpp"

#include "logging/logger.hpp"
#include "model/config.hpp"
#include "model/types.hpp"
#include "roles/patient.hpp"
#include "util/error.hpp"
#include "util/random.hpp"

#include <atomic>
#include <csignal>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::atomic<bool> stopFlag(false);

void handleSigusr2(int) {
    stopFlag.store(true);
}
} // namespace

int PatientGenerator::run(const std::string& keyPath, const Config& cfg) {
    struct sigaction sa {};
    sa.sa_handler = handleSigusr2;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, nullptr);

    RandomGenerator rng(cfg.randomSeed);
    int spawned = 0;

    while (!stopFlag.load() && spawned < cfg.totalPatientsTarget) {
        int age = rng.uniformInt(1, 90);
        bool hasGuardian = age < 18;
        int personsCount = hasGuardian ? 2 : 1;
        bool isVip = rng.uniformInt(0, 99) < 10; // ~10% VIP

        pid_t pid = fork();
        if (pid == -1) {
            logErrno("PatientGenerator fork failed");
            break;
        } else if (pid == 0) {
            // exec self in patient mode: argv: [exe, "patient", keyPath, id, age, isVip, hasGuardian, personsCount]
            std::string idStr = std::to_string(spawned + 1);
            std::string ageStr = std::to_string(age);
            std::string vipStr = isVip ? "1" : "0";
            std::string guardianStr = hasGuardian ? "1" : "0";
            std::string personsStr = std::to_string(personsCount);

            std::vector<char*> args;
            args.push_back(const_cast<char*>(keyPath.c_str())); // executable path
            args.push_back(const_cast<char*>("patient"));
            args.push_back(const_cast<char*>(keyPath.c_str()));
            args.push_back(const_cast<char*>(idStr.c_str()));
            args.push_back(const_cast<char*>(ageStr.c_str()));
            args.push_back(const_cast<char*>(vipStr.c_str()));
            args.push_back(const_cast<char*>(guardianStr.c_str()));
            args.push_back(const_cast<char*>(personsStr.c_str()));
            args.push_back(nullptr);
            execv(keyPath.c_str(), args.data());
            logErrno("execv patient failed");
            _exit(1);
        }

        spawned++;
        int sleepMs = cfg.timeScaleMsPerSimMinute;
        usleep(static_cast<useconds_t>(sleepMs * 1000));
    }

    // Wait for children to finish (fire and forget here).
    while (waitpid(-1, nullptr, WNOHANG) > 0) {}
    return 0;
}
