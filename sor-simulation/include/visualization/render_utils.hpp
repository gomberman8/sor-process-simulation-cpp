#pragma once

#include "visualization/state.hpp"

#include <algorithm>
#include <string>
#include <vector>

/** @brief Format a patient label for a given stage (color/flags). */
std::string formatPatientLabel(const PatientView& pv, Stage areaStage);

/** @brief Terminal-visible length (ignores ANSI escapes). */
size_t visibleLength(const std::string& s);

/** @brief Wrap tokens into lines constrained by width. */
std::vector<std::string> wrapTokens(const std::vector<std::string>& tokens, size_t width);

/** @brief Pad/truncate a string to width (ANSI-aware). */
std::string padded(const std::string& s, size_t width);

template <typename KeyFn>
void trimQueue(std::vector<const PatientView*>& items, int limit, KeyFn keyFn) {
    if (limit < 0) return;
    std::sort(items.begin(), items.end(), [&](const PatientView* a, const PatientView* b) {
        return keyFn(a) < keyFn(b);
    });
    if (static_cast<int>(items.size()) > limit) {
        items.resize(static_cast<size_t>(limit));
    }
}
