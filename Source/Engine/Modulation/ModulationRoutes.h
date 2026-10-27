/**
 * @file ModulationRoutes.h
 * @brief Routes connect a modulation source (LFO or Note Random) to one slot parameter (A-D) with an
 *        amount and a polarity.
 */
#pragma once

#include "Engine/ModuleChain.h"

#include <cmath>

namespace midiforge
{

constexpr int numLfos = 4;
constexpr int numNoteRandoms = 4;
/** Sources are numbered: LFO 1..4 first, then Random 1..4. The numbers are stored in projects. */
constexpr int numModulationSources = numLfos + numNoteRandoms;
constexpr int firstNoteRandomSource = numLfos;
constexpr int numModulationRoutes = 8;
/** Targets are numbered: 0 = none, then slot 1 A, slot 1 B, ... slot 8 D. */
constexpr int numModulationTargets = 1 + numSlots * numParametersPerSlot;

/** One modulation route. */
struct ModulationRoute
{
    int sourceIndex = 0; ///< 0..numModulationSources-1: LFO 1..4, then Random 1..4
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
    int targetSlotIndex() const noexcept { return (targetIndex - 1) / numParametersPerSlot; }
    int targetParameterIndex() const noexcept { return (targetIndex - 1) % numParametersPerSlot; }
};

/** @return The target number for a slot and parameter (both zero based). */
constexpr int modulationTargetIndex (int slotIndex, int parameterIndex) noexcept
{
    return 1 + slotIndex * numParametersPerSlot + parameterIndex;
}

/** The current output of every modulation source, in both polarities. */
struct ModulationSourceValues
{
    float bipolar[numModulationSources] {};  ///< -1..+1
    float unipolar[numModulationSources] {}; ///< 0..+1
};

/**
 * Applies all active routes: each adds sourceValue * amount to its target's normalised value, using the
 * bipolar or unipolar value of its source. Several routes on one target add up. Results are clamped to 0..1.
 *
 * @param sourceValues   Current output of every source.
 * @param routes         numModulationRoutes routes.
 * @param baseSlots      The slot settings as set by the user / host (numSlots entries).
 * @param modulatedSlots Receives the modulated slot settings (numSlots entries).
 */
inline void applyModulationRoutes (const ModulationSourceValues& sourceValues, const ModulationRoute* routes,
                                   const SlotSettings* baseSlots, SlotSettings* modulatedSlots) noexcept
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        modulatedSlots[slotIndex] = baseSlots[slotIndex];

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        const auto& route = routes[routeIndex];
        if (! route.isActive())
            continue;
        const float sourceValue =
            route.isUnipolar ? sourceValues.unipolar[route.sourceIndex] : sourceValues.bipolar[route.sourceIndex];
        modulatedSlots[route.targetSlotIndex()].parameterValues[route.targetParameterIndex()] +=
            sourceValue * std::clamp (route.amount, -1.0f, 1.0f);
    }

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        for (float& value : modulatedSlots[slotIndex].parameterValues)
            value = std::clamp (value, 0.0f, 1.0f);
}

} // namespace midiforge
