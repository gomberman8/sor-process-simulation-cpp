#include "visualization/render_utils.hpp"

#include "visualization/log_parser.hpp"

std::string formatPatientLabel(const PatientView& pv, Stage areaStage) {
    std::string label;
    if (pv.isVip) {
        label += "[VIP] ";
    }
    if (pv.persons > 1) {
        label += "(" + std::to_string(pv.persons) + ") ";
    }
    int displayPid = pv.patientPid > 0 ? pv.patientPid : 0;
    if (displayPid > 0) {
        label += "pid=" + std::to_string(displayPid);
    } else {
        label += "id=" + std::to_string(pv.id);
    }

    const char* reset = "\033[0m";
    std::string bg = "\033[47m"; // default white
    if (areaStage == Stage::RegistrationQueue) {
        if (pv.registrationWindow == "reg2") {
            bg = "\033[43m"; // yellow for reg2
        } else {
            bg = "\033[48;5;208m"; // orange for reg1/default
        }
    } else {
        switch (pv.color) {
            case TriageColor::Red: bg = "\033[41m"; break;
            case TriageColor::Yellow: bg = "\033[43m"; break;
            case TriageColor::Green: bg = "\033[42m"; break;
            default: break;
        }
    }
    std::string fg;
    if (pv.isVip) {
        fg = "\033[35m"; // purple text for VIP
    }

    return bg + fg + label + reset;
}

// Compute printable width ignoring ANSI escape sequences.
size_t visibleLength(const std::string& s) {
    size_t len = 0;
    bool inEscape = false;
    for (char c : s) {
        if (inEscape) {
            if (c == 'm') inEscape = false;
            continue;
        }
        if (c == '\033') {
            inEscape = true;
            continue;
        }
        len++;
    }
    return len;
}

std::vector<std::string> wrapTokens(const std::vector<std::string>& tokens, size_t width) {
    std::vector<std::string> lines;
    std::string current;
    for (const auto& tok : tokens) {
        size_t tokLen = visibleLength(tok);
        if (current.empty()) {
            current = tok;
            continue;
        }
        size_t currentLen = visibleLength(current);
        if (currentLen + 1 + tokLen <= width) {
            current += " " + tok;
        } else {
            lines.push_back(current);
            current = tok;
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    if (lines.empty()) {
        lines.emplace_back("");
    }
    return lines;
}

std::string padded(const std::string& s, size_t width) {
    size_t vis = visibleLength(s);
    if (vis >= width) return s;
    return s + std::string(width - vis, ' ');
}
