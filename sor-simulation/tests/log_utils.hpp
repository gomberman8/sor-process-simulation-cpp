#pragma once

#include "visualization/log_parser.hpp"

#include <string>
#include <vector>

/** @brief Parsed simulator log lines plus raw text. */
struct LogData {
    std::vector<LogEntry> entries;
    std::vector<std::string> rawLines;
};

/** @brief Parse a log file into structured entries and raw lines (best-effort). */
LogData readLogFile(const std::string& path);

/** @brief Aggregated summary.txt values for assertions. */
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

/** @brief Extract high-level counters from a summary file written by Director. */
SummaryData parseSummary(const std::string& path);
