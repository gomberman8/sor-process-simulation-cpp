#pragma once

#include "visualization/state.hpp"

#include <algorithm>
#include <string>
#include <vector>

std::string formatPatientLabel(const PatientView& pv, Stage areaStage);
size_t visibleLength(const std::string& s);
std::vector<std::string> wrapTokens(const std::vector<std::string>& tokens, size_t width);
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
