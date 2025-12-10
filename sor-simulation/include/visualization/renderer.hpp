#pragma once

#include "visualization/state.hpp"

/** @brief Render waiting room / triage / entrance overview with live stats. */
void renderTopSection(const VisualizationState& state);

/** @brief Render the trailing set of recent log actions. */
void renderActions(const VisualizationState& state);

/** @brief Render specialist queues/active patients and per-specialist stats. */
void renderSpecialists(const VisualizationState& state);

/** @brief Full frame render: clears screen then draws all sections. */
void render(const VisualizationState& state);
