/**
 * @file ParameterIds.h
 * @brief The host parameter IDs. They are stored in projects and automation, so never rename them.
 *
 * Per slot n (1..8):   sn_type (module), sn_on (on/off), sn_a .. sn_h (the eight module parameters;
 *                      sn_e .. sn_h were added in 0.7.0 and come after all older parameters)
 * Per LFO n (1..4):    ln_wave, ln_sync, ln_rate, ln_div, ln_phase, ln_width, ln_restart
 * Per route n (1..8):  rn_source, rn_target, rn_amount, rn_polarity
 * Per Note Random n (1..4): nn_window, nn_trigger, nn_steps
 */
#pragma once

#include "Engine/ModuleDescriptions.h"

#include <juce_core/juce_core.h>

namespace midiforge::ParameterIds
{

/** Suffixes of the eight module parameters, in order A..H. */
inline constexpr const char* valueSuffixes[numParametersPerSlot] = { "a", "b", "c", "d", "e", "f", "g", "h" };

/** Builds an ID such as "s3_type" or "l2_rate": prefix, 1-based number, underscore, suffix. */
inline juce::String makeId (const char* prefix, int zeroBasedIndex, const char* suffix)
{
    return prefix + juce::String (zeroBasedIndex + 1) + "_" + suffix;
}

// Slots ------------------------------------------------------------------------------------------
inline juce::String moduleType (int slotIndex)
{
    return makeId ("s", slotIndex, "type");
}
inline juce::String slotEnabled (int slotIndex)
{
    return makeId ("s", slotIndex, "on");
}
inline juce::String moduleParameter (int slotIndex, int parameterIndex)
{
    return makeId ("s", slotIndex, valueSuffixes[parameterIndex]);
}

// LFOs -------------------------------------------------------------------------------------------
inline juce::String lfoWaveform (int lfoIndex)
{
    return makeId ("l", lfoIndex, "wave");
}
inline juce::String lfoSyncMode (int lfoIndex)
{
    return makeId ("l", lfoIndex, "sync");
}
inline juce::String lfoRate (int lfoIndex)
{
    return makeId ("l", lfoIndex, "rate");
}
inline juce::String lfoDivision (int lfoIndex)
{
    return makeId ("l", lfoIndex, "div");
}
inline juce::String lfoPhase (int lfoIndex)
{
    return makeId ("l", lfoIndex, "phase");
}
inline juce::String lfoPulseWidth (int lfoIndex)
{
    return makeId ("l", lfoIndex, "width");
}
inline juce::String lfoRestartOnKey (int lfoIndex)
{
    return makeId ("l", lfoIndex, "restart");
}

// Modulation routes ------------------------------------------------------------------------------
inline juce::String routeSource (int routeIndex)
{
    return makeId ("r", routeIndex, "source");
}
inline juce::String routeTarget (int routeIndex)
{
    return makeId ("r", routeIndex, "target");
}
inline juce::String routeAmount (int routeIndex)
{
    return makeId ("r", routeIndex, "amount");
}
inline juce::String routePolarity (int routeIndex)
{
    return makeId ("r", routeIndex, "polarity");
}

// Note Random modulators -------------------------------------------------------------------------
inline juce::String noteRandomWindow (int randomIndex)
{
    return makeId ("n", randomIndex, "window");
}
inline juce::String noteRandomTrigger (int randomIndex)
{
    return makeId ("n", randomIndex, "trigger");
}
inline juce::String noteRandomSteps (int randomIndex)
{
    return makeId ("n", randomIndex, "steps");
}

/** Version hints for the hosts' parameter caching (see juce::ParameterID). */
inline constexpr int slotParameterVersion = 1;       ///< Version 0.1.0: slot parameters
inline constexpr int modulationParameterVersion = 2; ///< Version 0.2.0: LFOs and routes
inline constexpr int noteRandomParameterVersion = 3; ///< Version 0.6.0: route polarity, Note Random
inline constexpr int secondPageParameterVersion = 4; ///< Version 0.7.0: slot parameters E-H

} // namespace midiforge::ParameterIds
