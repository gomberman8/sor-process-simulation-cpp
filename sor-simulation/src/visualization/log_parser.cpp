#include "visualization/log_parser.hpp"

#include <cctype>
#include <sstream>

int toIntSafe(const std::string& s) {
    try {
        return std::stoi(s);
    } catch (const std::exception&) {
        return 0;
    }
}

bool extractInt(const std::string& text, const std::string& key, int& out) {
    size_t pos = 0;
    while (true) {
        pos = text.find(key, pos);
        if (pos == std::string::npos) return false;
        // Ensure we're not matching a substring inside another token (e.g. "pid=").
        if (pos > 0 && std::isalnum(static_cast<unsigned char>(text[pos - 1]))) {
            pos += key.size();
            continue;
        }
        pos += key.size();
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
        long val = 0;
        bool found = false;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            val = val * 10 + (text[pos] - '0');
            found = true;
            ++pos;
        }
        if (found) {
            out = static_cast<int>(val);
        }
        return found;
    }
}

std::vector<std::string> split(const std::string& line, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    return parts;
}

TriageColor colorFromInt(int value) {
    switch (value) {
        case 0: return TriageColor::Red;
        case 1: return TriageColor::Yellow;
        case 2: return TriageColor::Green;
        default: return TriageColor::None;
    }
}

SpecialistType specialistFromInt(int value) {
    switch (value) {
        case 0: return SpecialistType::Cardiologist;
        case 1: return SpecialistType::Neurologist;
        case 2: return SpecialistType::Ophthalmologist;
        case 3: return SpecialistType::Laryngologist;
        case 4: return SpecialistType::Surgeon;
        case 5: return SpecialistType::Paediatrician;
        default: return SpecialistType::None;
    }
}

std::string specialistName(SpecialistType t) {
    switch (t) {
        case SpecialistType::Cardiologist: return "CARDIO";
        case SpecialistType::Neurologist: return "NEURO";
        case SpecialistType::Ophthalmologist: return "OPHTH";
        case SpecialistType::Laryngologist: return "LARYNG";
        case SpecialistType::Surgeon: return "SURGEON";
        case SpecialistType::Paediatrician: return "PAEDI";
        default: return "UNKNOWN";
    }
}

std::string specialistNameColored(SpecialistType t) {
    const char* reset = "\033[0m";
    const char* color = "\033[36m"; // default cyan
    switch (t) {
        case SpecialistType::Cardiologist: color = "\033[31m"; break;   // red
        case SpecialistType::Neurologist: color = "\033[35m"; break;    // magenta
        case SpecialistType::Ophthalmologist: color = "\033[36m"; break;// cyan
        case SpecialistType::Laryngologist: color = "\033[33m"; break;  // yellow
        case SpecialistType::Surgeon: color = "\033[34m"; break;        // blue
        case SpecialistType::Paediatrician: color = "\033[32m"; break;  // green
        default: break;
    }
    return std::string(color) + specialistName(t) + reset;
}

SpecialistType specialistFromLabel(const std::string& text) {
    if (text.find("Cardiologist") != std::string::npos) return SpecialistType::Cardiologist;
    if (text.find("Neurologist") != std::string::npos) return SpecialistType::Neurologist;
    if (text.find("Ophthalmologist") != std::string::npos) return SpecialistType::Ophthalmologist;
    if (text.find("Laryngologist") != std::string::npos) return SpecialistType::Laryngologist;
    if (text.find("Surgeon") != std::string::npos) return SpecialistType::Surgeon;
    if (text.find("Paediatrician") != std::string::npos) return SpecialistType::Paediatrician;
    return SpecialistType::None;
}

bool parseLogLine(const std::string& line, LogEntry& out) {
    auto parts = split(line, ';');
    if (parts.size() < 3) {
        return false;
    }
    out.simTime = toIntSafe(parts[0]);
    out.pid = toIntSafe(parts[1]);

    if (parts.size() >= 9 && parts[2].rfind("wR=", 0) == 0) {
        out.hasMetrics = true;
        // wR is x/y
        auto slashPos = parts[2].find('/');
        if (slashPos != std::string::npos) {
            out.waitingCurrent = toIntSafe(parts[2].substr(3, slashPos - 3));
            out.waitingCapacity = toIntSafe(parts[2].substr(slashPos + 1));
        }
        out.regQueue = toIntSafe(parts[3].substr(parts[3].find('=') + 1));
        out.triageQueue = toIntSafe(parts[4].substr(parts[4].find('=') + 1));
        out.specialistsQueue = toIntSafe(parts[5].substr(parts[5].find('=') + 1));
        out.waitSem = toIntSafe(parts[6].substr(parts[6].find('=') + 1));
        out.stateSem = toIntSafe(parts[7].substr(parts[7].find('=') + 1));
        out.role = parts[8];
        std::string remaining;
        for (size_t i = 9; i < parts.size(); ++i) {
            if (i > 9) remaining.push_back(';');
            remaining += parts[i];
        }
        out.text = remaining;
    } else {
        out.role = parts[2];
        std::string remaining;
        for (size_t i = 3; i < parts.size(); ++i) {
            if (i > 3) remaining.push_back(';');
            remaining += parts[i];
        }
        out.text = remaining.empty() ? parts[2] : remaining;
    }
    return true;
}
