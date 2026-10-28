/**
 * @file ModulatorModulation.cpp
 * @brief Implementation of applyRoutesToModulators() and the rate conversions.
 */
#include "ModulatorModulation.h"

#include <algorithm>
#include <cmath>

namespace midiforge
{

namespace
{
/** The free LFO rate knob: 0.01 … 20 Hz, 1 Hz in the middle (as the host parameter). */
constexpr double lowestFreeRate = 0.01;
constexpr double highestFreeRate = 20.0;
constexpr double middleFreeRate = 1.0;
/** Pulse width range of the Width knob (5 … 95 %). */
constexpr double narrowestPulse = 0.05;
constexpr double widestPulse = 0.95;

/** The skew that puts middleFreeRate in the middle of the knob (as juce::NormalisableRange::setSkewForCentre). */
double freeRateSkew() noexcept
{
    return std::log (0.5) / std::log ((middleFreeRate - lowestFreeRate) / (highestFreeRate - lowestFreeRate));
}

float indexToNormalised (int index, int numValues) noexcept
{
    return numValues > 1 ? (float) std::clamp (index, 0, numValues - 1) / (float) (numValues - 1) : 0.0f;
}

int normalisedToIndex (float normalised, int numValues) noexcept
{
    return (int) std::lround (std::clamp (normalised, 0.0f, 1.0f) * (float) (numValues - 1));
}
} // namespace

float normalisedLfoRate (const LfoSettings& settings) noexcept
{
    if (settings.isTempoSynced)
        return indexToNormalised (settings.divisionIndex, numLfoDivisions);
    const double proportion = (std::clamp (settings.rateHz, lowestFreeRate, highestFreeRate) - lowestFreeRate) /
                              (highestFreeRate - lowestFreeRate);
    return (float) std::pow (proportion, freeRateSkew());
}

void setLfoRateFromNormalised (LfoSettings& settings, float normalisedRate) noexcept
{
    const float clamped = std::clamp (normalisedRate, 0.0f, 1.0f);
    if (settings.isTempoSynced)
    {
        settings.divisionIndex = normalisedToIndex (clamped, numLfoDivisions);
        return;
    }
    const double proportion = clamped <= 0.0f ? 0.0 : std::exp (std::log ((double) clamped) / freeRateSkew());
    settings.rateHz = lowestFreeRate + (highestFreeRate - lowestFreeRate) * proportion;
}

void applyRoutesToModulators (const ModulationSourceValues& previousValues, const ModulationRoute* routes,
                              const float* previousAmounts, const LfoSettings* baseLfos,
                              const StepModulatorSettings* baseSteps, ModulatedModulators& result) noexcept
{
    // Sum the changes per target first, then apply each once (several routes on one target add up).
    float amountChange[numModulationRoutes] {};
    float lfoChange[numLfos][numLfoSettingTargets] {};
    float stepChange[numStepModulators][numStepSettingTargets] {};
    bool isLfoSettingModulated[numLfos][numLfoSettingTargets] {};
    bool isStepSettingModulated[numStepModulators][numStepSettingTargets] {};

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        const auto& route = routes[routeIndex];
        const float amount = std::clamp (previousAmounts[routeIndex], -1.0f, 1.0f);
        if (! route.isActiveWithAmount (amount) || ! route.targetsModulation())
            continue;
        const float sourceValue =
            route.isUnipolar ? previousValues.unipolar[route.sourceIndex] : previousValues.bipolar[route.sourceIndex];
        const float change = sourceValue * amount;
        if (const int targetRoute = route.targetRouteIndex(); targetRoute >= 0)
        {
            amountChange[targetRoute] += change;
        }
        else if (const int lfo = route.targetLfoIndex(); lfo >= 0)
        {
            lfoChange[lfo][(int) route.targetLfoSetting()] += change;
            isLfoSettingModulated[lfo][(int) route.targetLfoSetting()] = true;
        }
        else if (const int steps = route.targetStepIndex(); steps >= 0)
        {
            stepChange[steps][(int) route.targetStepSetting()] += change;
            isStepSettingModulated[steps][(int) route.targetStepSetting()] = true;
        }
    }

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
        result.routeAmounts[routeIndex] =
            std::clamp (std::clamp (routes[routeIndex].amount, -1.0f, 1.0f) + amountChange[routeIndex], -1.0f, 1.0f);

    for (int lfo = 0; lfo < numLfos; ++lfo)
    {
        auto& settings = result.lfos[lfo];
        settings = baseLfos[lfo];
        const auto* change = lfoChange[lfo];
        result.isLfoRateModulated[lfo] = isLfoSettingModulated[lfo][(int) LfoSettingTarget::rate];
        if (result.isLfoRateModulated[lfo])
            setLfoRateFromNormalised (settings, normalisedLfoRate (settings) + change[(int) LfoSettingTarget::rate]);
        if (isLfoSettingModulated[lfo][(int) LfoSettingTarget::phase])
        {
            const double turns = settings.phaseDegrees / 360.0 + change[(int) LfoSettingTarget::phase];
            settings.phaseDegrees = (turns - std::floor (turns)) * 360.0; // wraps around
        }
        if (isLfoSettingModulated[lfo][(int) LfoSettingTarget::width])
        {
            const double position = (settings.pulseWidth - narrowestPulse) / (widestPulse - narrowestPulse) +
                                    change[(int) LfoSettingTarget::width];
            settings.pulseWidth = narrowestPulse + (widestPulse - narrowestPulse) * std::clamp (position, 0.0, 1.0);
        }
    }

    for (int steps = 0; steps < numStepModulators; ++steps)
    {
        auto& settings = result.stepModulators[steps];
        settings = baseSteps[steps];
        const auto* change = stepChange[steps];
        result.isStepRateModulated[steps] = isStepSettingModulated[steps][(int) StepSettingTarget::rate];
        if (result.isStepRateModulated[steps])
            settings.divisionIndex = normalisedToIndex (indexToNormalised (settings.divisionIndex, numStepDivisions) +
                                                            change[(int) StepSettingTarget::rate],
                                                        numStepDivisions);
        if (isStepSettingModulated[steps][(int) StepSettingTarget::glide])
            settings.glide = std::clamp (settings.glide + (double) change[(int) StepSettingTarget::glide], 0.0, 1.0);
    }
}

} // namespace midiforge
