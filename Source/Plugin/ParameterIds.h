/**
 * @file ParameterIds.h
 * @brief The host parameter IDs. They are stored in projects and automation, so never rename them.
 *
 * Per slot n (1..8):  sn_type  (module), sn_on (on/off), sn_a .. sn_d (the four module parameters).
 */
#pragma once

#include "Engine/ModuleDescriptions.h"

#include <juce_core/juce_core.h>

namespace midiforge::ParameterIds
{

/** Suffixes of the four module parameters, in order A..D. */
inline constexpr const char* valueSuffixes[numParametersPerSlot] = { "a", "b", "c", "d" };

inline juce::String forSlot (int slotIndex, const char* suffix)
{
    return "s" + juce::String (slotIndex + 1) + "_" + suffix;
}

inline juce::String moduleType (int slotIndex)
{
    return forSlot (slotIndex, "type");
}
inline juce::String slotEnabled (int slotIndex)
{
    return forSlot (slotIndex, "on");
}
inline juce::String moduleParameter (int slotIndex, int parameterIndex)
{
    return forSlot (slotIndex, valueSuffixes[parameterIndex]);
}

/** Version hint for the hosts' parameter caching (see juce::ParameterID). Bump when adding parameters. */
inline constexpr int parameterVersion = 1;

} // namespace midiforge::ParameterIds
