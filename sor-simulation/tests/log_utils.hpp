#pragma once

#include "visualization/log_parser.hpp"

#include <string>
#include <vector>

struct LogData {
    std::vector<LogEntry> entries;
    std::vector<std::string> rawLines;
};

LogData readLogFile(const std::string& path);

struct SummaryData {
    int totalPatients{0};
    int triageRed{0};
    int triageYellow{0};
    int triageGreen{0};
    int triageSentHome{0};
    int outcomeHome{0};
    int outcomeWard{0};
    int outcomeOther{0};
};

SummaryData parseSummary(const std::string& path);
