/**
 * @file ModulationRoutes.h
 * @brief Routes connect a modulation source (LFO or Note Random) to one slot parameter (A-H) or a slot's
 *        on/off switch, with an amount and a polarity.
 */
#pragma once

#include "Engine/ModuleChain.h"

#include <cmath>

namespace midiforge
{

constexpr int numLfos = 4;
constexpr int numNoteRandoms = 4;
constexpr int numStepModulators = 4;
/**
 * Sources are numbered: LFO 1..4 first, then Random 1..4, then Steps 1..4 (added in 0.14.0). The numbers
 * are stored in projects, so new sources are appended.
 */
constexpr int numModulationSources = numLfos + numNoteRandoms + numStepModulators;
constexpr int firstNoteRandomSource = numLfos;
constexpr int firstStepModulatorSource = numLfos + numNoteRandoms;
constexpr int numModulationRoutes = 8;
/**
 * Targets are numbered: 0 = none, then slot 1 A, slot 1 B, ... slot 8 D (1..32, since 0.2.0), then
 * slot 1 E ... slot 8 H (33..64, added in 0.7.0), then the on/off switches of slot 1 ... 8 (65..72, added in
 * 0.13.0). The numbers are stored in projects, so targets added later get numbers after the existing ones.
 */
constexpr int firstSecondPageTarget = 1 + numSlots * numParametersPerPage;
constexpr int firstSlotSwitchTarget = 1 + numSlots * numParametersPerSlot;
constexpr int numModulationTargets = firstSlotSwitchTarget + numSlots;
/** The "parameter index" that stands for a slot's on/off switch in targetParameterIndex() and below. */
constexpr int slotSwitchParameterIndex = numParametersPerSlot;

/**
 * A modulated on/off switch has hysteresis, so a slow or wobbling source near the middle does not flip the
 * slot back and forth: a switched-off slot turns on above switchOnThreshold, a switched-on slot turns off
 * below switchOffThreshold (the switch itself counts as 0 or 1, the modulation is added to it).
 */
constexpr float switchOnThreshold = 0.55f;
constexpr float switchOffThreshold = 0.45f;

/** One modulation route. */
struct ModulationRoute
{
    int sourceIndex = 0; ///< 0..numModulationSources-1: LFO 1..4, then Random 1..4, then Steps 1..4
    int targetIndex = 0; ///< 0 = none, otherwise see targetSlotIndex() / targetParameterIndex()
    float amount = 0.5f; ///< -1..+1: how far the source moves the parameter (1 = the full parameter range)
    /**
     * False: bipolar, the source swings the value below and above the knob position (-1..+1).
     * True: unipolar, the source only pushes the value up from the knob position (0..+1), or down with a
     * negative amount.
     */
    bool isUnipolar = false;

    /** Amounts smaller than this (half a percent) count as "off". */
    static constexpr float smallestEffectiveAmount = 0.005f;

    bool isActive() const noexcept
    {
        return targetIndex > 0 && targetIndex < numModulationTargets && sourceIndex >= 0 &&
               sourceIndex < numModulationSources && std::abs (amount) >= smallestEffectiveAmount;
    }
    /** @return True if the route moves a slot's on/off switch rather than a knob. */
    bool targetsSlotSwitch() const noexcept { return targetIndex >= firstSlotSwitchTarget; }
    int targetSlotIndex() const noexcept
    {
        if (targetsSlotSwitch())
            return targetIndex - firstSlotSwitchTarget;
        return targetIndex < firstSecondPageTarget ? (targetIndex - 1) / numParametersPerPage
                                                   : (targetIndex - firstSecondPageTarget) / numParametersPerPage;
    }
    /** @return The knob (0..7), or slotSwitchParameterIndex for the on/off switch. */
    int targetParameterIndex() const noexcept
    {
        if (targetsSlotSwitch())
            return slotSwitchParameterIndex;
        return targetIndex < firstSecondPageTarget
                   ? (targetIndex - 1) % numParametersPerPage
                   : numParametersPerPage + (targetIndex - firstSecondPageTarget) % numParametersPerPage;
    }
};

/**
 * @return The target number for a slot and parameter (both zero based); parameterIndex
 *         slotSwitchParameterIndex means the slot's on/off switch.
 */
constexpr int modulationTargetIndex (int slotIndex, int parameterIndex) noexcept
{
    if (parameterIndex == slotSwitchParameterIndex)
        return firstSlotSwitchTarget + slotIndex;
    return parameterIndex < numParametersPerPage
               ? 1 + slotIndex * numParametersPerPage + parameterIndex
               : firstSecondPageTarget + slotIndex * numParametersPerPage + (parameterIndex - numParametersPerPage);
}

/** The current output of every modulation source, in both polarities. */
struct ModulationSourceValues
{
    float bipolar[numModulationSources] {};  ///< -1..+1
    float unipolar[numModulationSources] {}; ///< 0..+1
};

/**
 * Applies all active routes: each adds sourceValue * amount to its target's normalised value, using the
 * bipolar or unipolar value of its source. Several routes on one target add up. Knob values are clamped to
 * 0..1. A modulated on/off switch counts as 0 or 1 plus its modulation and flips at the thresholds above.
 *
 * @param sourceValues   Current output of every source.
 * @param routes         numModulationRoutes routes.
 * @param baseSlots      The slot settings as set by the user / host (numSlots entries).
 * @param modulatedSlots Receives the modulated slot settings (numSlots entries).
 * @param switchStates   numSlots entries: each slot's switch state after the previous call (for the
 *                       hysteresis), updated here. nullptr: no memory, the switch flips at 0.5.
 */
inline void applyModulationRoutes (const ModulationSourceValues& sourceValues, const ModulationRoute* routes,
                                   const SlotSettings* baseSlots, SlotSettings* modulatedSlots,
                                   bool* switchStates = nullptr) noexcept
{
    float switchModulation[numSlots] {};
    bool isSwitchModulated[numSlots] {};
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        modulatedSlots[slotIndex] = baseSlots[slotIndex];

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        const auto& route = routes[routeIndex];
        if (! route.isActive())
            continue;
        const float sourceValue =
            route.isUnipolar ? sourceValues.unipolar[route.sourceIndex] : sourceValues.bipolar[route.sourceIndex];
        const float change = sourceValue * std::clamp (route.amount, -1.0f, 1.0f);
        if (route.targetsSlotSwitch())
        {
            switchModulation[route.targetSlotIndex()] += change;
            isSwitchModulated[route.targetSlotIndex()] = true;
        }
        else
        {
            modulatedSlots[route.targetSlotIndex()].parameterValues[route.targetParameterIndex()] += change;
        }
    }

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        for (float& value : modulatedSlots[slotIndex].parameterValues)
            value = std::clamp (value, 0.0f, 1.0f);

        bool isOn = baseSlots[slotIndex].isEnabled;
        if (isSwitchModulated[slotIndex])
        {
            const float switchValue = (isOn ? 1.0f : 0.0f) + switchModulation[slotIndex];
            const bool wasOn = switchStates != nullptr ? switchStates[slotIndex] : isOn;
            if (switchStates == nullptr)
                isOn = switchValue >= 0.5f;
            else
                isOn = wasOn ? switchValue >= switchOffThreshold : switchValue >= switchOnThreshold;
        }
        modulatedSlots[slotIndex].isEnabled = isOn;
        if (switchStates != nullptr)
            switchStates[slotIndex] = isOn;
    }
}

} // namespace midiforge
