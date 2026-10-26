/**
 * @file ModulationRoutes.h
 * @brief Routes connect an LFO to one slot parameter (A-D) with an amount.
 */
#pragma once

#include "Engine/ModuleChain.h"

#include <cmath>

namespace midiforge
{

constexpr int numLfos = 4;
constexpr int numModulationRoutes = 8;
/** Targets are numbered: 0 = none, then slot 1 A, slot 1 B, ... slot 8 D. */
constexpr int numModulationTargets = 1 + numSlots * numParametersPerSlot;

/** One modulation route. */
struct ModulationRoute
{
    int lfoIndex = 0;    ///< 0..numLfos-1
    int targetIndex = 0; ///< 0 = none, otherwise see targetSlotIndex() / targetParameterIndex()
    float amount = 0.5f; ///< -1..+1: how far the LFO moves the parameter (1 = the full parameter range)

    /** Amounts smaller than this (half a percent) count as "off". */
    static constexpr float smallestEffectiveAmount = 0.005f;

    bool isActive() const noexcept
    {
        return targetIndex > 0 && targetIndex < numModulationTargets && std::abs (amount) >= smallestEffectiveAmount;
    }
    int targetSlotIndex() const noexcept { return (targetIndex - 1) / numParametersPerSlot; }
    int targetParameterIndex() const noexcept { return (targetIndex - 1) % numParametersPerSlot; }
};

/** @return The target number for a slot and parameter (both zero based). */
constexpr int modulationTargetIndex (int slotIndex, int parameterIndex) noexcept
{
    return 1 + slotIndex * numParametersPerSlot + parameterIndex;
}

/**
 * Applies all active routes: each adds lfoValue * amount to its target's normalised value.
 * Several routes on one target add up. Results are clamped to 0..1.
 *
 * @param lfoValues     Current output of each LFO (-1..+1), numLfos entries.
 * @param routes        numModulationRoutes routes.
 * @param baseSlots     The slot settings as set by the user / host (numSlots entries).
 * @param modulatedSlots Receives the modulated slot settings (numSlots entries).
 */
inline void applyModulationRoutes (const float* lfoValues, const ModulationRoute* routes, const SlotSettings* baseSlots,
                                   SlotSettings* modulatedSlots) noexcept
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        modulatedSlots[slotIndex] = baseSlots[slotIndex];

    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        const auto& route = routes[routeIndex];
        if (! route.isActive() || route.lfoIndex < 0 || route.lfoIndex >= numLfos)
            continue;
        modulatedSlots[route.targetSlotIndex()].parameterValues[route.targetParameterIndex()] +=
            lfoValues[route.lfoIndex] * std::clamp (route.amount, -1.0f, 1.0f);
    }

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        for (float& value : modulatedSlots[slotIndex].parameterValues)
            value = std::clamp (value, 0.0f, 1.0f);
}

} // namespace midiforge
