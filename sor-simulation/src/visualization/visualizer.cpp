#include "visualization/visualizer.hpp"

#include "visualization/log_parser.hpp"
#include "visualization/renderer.hpp"
#include "visualization/state.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>

namespace {
std::atomic<bool> g_stop(false);
constexpr int kDefaultRenderIntervalMs = 200;

void handleSigint(int) {
    g_stop.store(true);
}

class VisualizerApp {
public:
    VisualizerApp(std::string logPath, int renderIntervalMs)
        : logPath_(std::move(logPath)),
          renderIntervalMs_(renderIntervalMs > 0 ? renderIntervalMs : kDefaultRenderIntervalMs) {}

    int run();

private:
    bool waitForLog();
    bool pumpLines();
    void maybeRender(bool advanced);

    std::string logPath_;
    std::ifstream in_;
    VisualizationState state_;
    int renderIntervalMs_;
    std::chrono::steady_clock::time_point lastRender_{std::chrono::steady_clock::now()};
};

bool VisualizerApp::waitForLog() {
    while (!g_stop.load()) {
        in_.open(logPath_);
        if (in_) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!in_) {
        std::cerr << "Cannot open log file: " << logPath_ << std::endl;
        return false;
    }
    return true;
}

bool VisualizerApp::pumpLines() {
    bool advanced = false;
    std::string line;
    while (std::getline(in_, line)) {
        advanced = true;
        if (line.empty()) continue;
        LogEntry entry;
        if (parseLogLine(line, entry)) {
            applyLogEntry(entry, state_);
        }
    }
    if (in_.eof()) {
        in_.clear();
    }
    return advanced;
}

void VisualizerApp::maybeRender(bool advanced) {
    auto now = std::chrono::steady_clock::now();
    if (advanced || std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRender_).count() > renderIntervalMs_) {
        render(state_);
        lastRender_ = now;
    }
}

int VisualizerApp::run() {
    if (!waitForLog()) return 1;

    render(state_);
    lastRender_ = std::chrono::steady_clock::now();

    while (!g_stop.load()) {
        bool advanced = pumpLines();
        maybeRender(advanced);
        if (g_stop.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(renderIntervalMs_));
    }
    return 0;
}
} // namespace

int runVisualizer(const std::string& logPath, int renderIntervalMs) {
    std::signal(SIGINT, handleSigint);
    VisualizerApp app(logPath, renderIntervalMs);
    return app.run();
}
