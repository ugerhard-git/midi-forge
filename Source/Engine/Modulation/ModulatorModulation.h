/**
 * @file ModulatorModulation.h
 * @brief "Modulating the modulation": routes whose target is another route's amount, an LFO's rate, phase or
 *        width, or a step modulator's rate or glide.
 *
 * These routes use the source values of the previous moment (the previous segment, under 1 ms earlier), so
 * loops such as LFO 1 → LFO 2 Rate and LFO 2 → LFO 1 Rate, or an LFO moving its own rate, stay stable.
 */
#pragma once

#include "Engine/Modulation/Lfo.h"
#include "Engine/Modulation/ModulationRoutes.h"
#include "Engine/Modulation/StepModulator.h"

namespace midiforge
{

/** The modulators' settings and the route amounts after the routes that target them. */
struct ModulatedModulators
{
    LfoSettings lfos[numLfos];
    StepModulatorSettings stepModulators[numStepModulators];
    float routeAmounts[numModulationRoutes] {}; ///< -1..+1
    bool isLfoRateModulated[numLfos] {};
    bool isStepRateModulated[numStepModulators] {};
};

/**
 * Applies the routes whose target is a setting of the modulation. Like routes to slot knobs, each adds
 * sourceValue × amount to the setting's normalised value (the position of its control, 0..1), so 100 %
 * moves it across the whole range. Exceptions: a route's amount moves by sourceValue × amount itself
 * (100 % moves it by up to 100 percentage points) and stays within −100 … +100 %; the phase wraps around.
 *
 * @param previousValues   The sources' output of the previous segment.
 * @param routes           numModulationRoutes routes.
 * @param previousAmounts  The routes' modulated amounts of the previous segment (for routes that move the
 *                         amount of a route that itself moves an amount).
 * @param baseLfos         The LFO settings as set by the user (numLfos entries).
 * @param baseSteps        The step modulator settings as set by the user (numStepModulators entries).
 * @param result           Receives the modulated settings and amounts.
 */
void applyRoutesToModulators (const ModulationSourceValues& previousValues, const ModulationRoute* routes,
                              const float* previousAmounts, const LfoSettings* baseLfos,
                              const StepModulatorSettings* baseSteps, ModulatedModulators& result) noexcept;

/** @return The position (0..1) of an LFO's rate control: the note value when tempo synced, else the Hz knob. */
float normalisedLfoRate (const LfoSettings& settings) noexcept;
/** Sets an LFO's rate (note value or Hz, whichever is active) from a control position (0..1). */
void setLfoRateFromNormalised (LfoSettings& settings, float normalisedRate) noexcept;

} // namespace midiforge
