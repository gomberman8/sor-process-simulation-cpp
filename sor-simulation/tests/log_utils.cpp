#include "log_utils.hpp"

#include <fstream>
#include <sstream>

LogData readLogFile(const std::string& path) {
    LogData data{};
    std::ifstream in(path);
    if (!in) {
        return data;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        data.rawLines.push_back(line);
        LogEntry entry{};
        if (parseLogLine(line, entry)) {
            data.entries.push_back(entry);
        }
    }
    return data;
}

static int extractValue(const std::string& line) {
    std::size_t pos = line.find(':');
    if (pos == std::string::npos) return 0;
    std::string value = line.substr(pos + 1);
    std::stringstream ss(value);
    int v = 0;
    ss >> v;
    return v;
}

SummaryData parseSummary(const std::string& path) {
    SummaryData summary{};
    std::ifstream in(path);
    if (!in) {
        return summary;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("Total patients") != std::string::npos) {
            summary.totalPatients = extractValue(line);
        } else if (line.find("Red") != std::string::npos && line.find("Triage assignments") == std::string::npos) {
            summary.triageRed = extractValue(line);
        } else if (line.find("Yellow") != std::string::npos) {
            summary.triageYellow = extractValue(line);
        } else if (line.find("Green") != std::string::npos) {
            summary.triageGreen = extractValue(line);
        } else if (line.find("Sent home from triage") != std::string::npos) {
            summary.triageSentHome = extractValue(line);
        } else if (line.find("Home:") != std::string::npos && line.find("Final dispositions") == std::string::npos) {
            summary.outcomeHome = extractValue(line);
        } else if (line.find("Ward:") != std::string::npos) {
            summary.outcomeWard = extractValue(line);
        } else if (line.find("Other:") != std::string::npos) {
            summary.outcomeOther = extractValue(line);
        }
    }
    return summary;
}
