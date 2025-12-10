#pragma once

#include "model/types.hpp"

#include <string>
#include <vector>

struct LogEntry {
    int simTime{0};
    int pid{0};
    bool hasMetrics{false};
    int waitingCurrent{0};
    int waitingCapacity{0};
    int regQueue{0};
    int triageQueue{0};
    int specialistsQueue{0};
    int waitSem{0};
    int stateSem{0};
    std::string role;
    std::string text;
};

/** @brief Safe stoi returning 0 on failure. */
int toIntSafe(const std::string& s);

/** @brief Extract integer value for a given key in free-form text. */
bool extractInt(const std::string& text, const std::string& key, int& out);

/** @brief Split string by delimiter into parts. */
std::vector<std::string> split(const std::string& line, char delim);

/** @brief Map integer to TriageColor. */
TriageColor colorFromInt(int value);

/** @brief Map integer to SpecialistType. */
SpecialistType specialistFromInt(int value);

/** @brief Short uppercase specialist label. */
std::string specialistName(SpecialistType t);

/** @brief Short colored specialist label for terminal output. */
std::string specialistNameColored(SpecialistType t);

/** @brief Infer SpecialistType from descriptive label text. */
SpecialistType specialistFromLabel(const std::string& text);

/** @brief Parse a log line into structured LogEntry (handles metric-prefixed format). */
bool parseLogLine(const std::string& line, LogEntry& out);
