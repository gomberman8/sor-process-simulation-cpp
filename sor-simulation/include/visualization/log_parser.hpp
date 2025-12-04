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

int toIntSafe(const std::string& s);
bool extractInt(const std::string& text, const std::string& key, int& out);
std::vector<std::string> split(const std::string& line, char delim);

TriageColor colorFromInt(int value);
SpecialistType specialistFromInt(int value);
std::string specialistName(SpecialistType t);
std::string specialistNameColored(SpecialistType t);
SpecialistType specialistFromLabel(const std::string& text);

bool parseLogLine(const std::string& line, LogEntry& out);
