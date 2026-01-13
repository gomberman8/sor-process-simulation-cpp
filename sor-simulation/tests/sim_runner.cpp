#include "sim_runner.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <thread>
#include <unistd.h>

namespace {
// Create a unique log path under a temp folder to avoid collisions between tests.
std::string makeLogPath(const std::string& prefix) {
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / "sor_sim_tests";
    std::error_code ec;
    fs::create_directories(base, ec);
    long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    fs::path path = base / (prefix + "_" + std::to_string(nowMs) + ".log");
    return path.string();
}
} // namespace

SimResult runSimulation(const std::string& sorSimPath, const Config& cfg, int runtimeMs) {
    SimResult result{};
    result.logPath = makeLogPath("run");
    std::atomic<bool> finished{false};

    // Stopper thread wakes after runtimeMs and triggers orderly shutdown.
    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(runtimeMs));
        if (!finished.load()) {
            // Trigger Director's SIGUSR2 handler to request shutdown.
            kill(getpid(), SIGUSR2);
        }
    });

    Director director;
    result.exitCode = director.run(sorSimPath, cfg, &result.logPath);
    finished.store(true);
    if (stopper.joinable()) stopper.join();
    result.summaryPath = director.lastSummaryPath();
    return result;
}
