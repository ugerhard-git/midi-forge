/**
 * @file FactoryPresets.h
 * @brief The presets shipped with the plug-in, exposed to hosts as "programs".
 *
 * When you add or change a preset, update the preset list in docs/MANUAL.md.
 */
#pragma once

#include "Engine/MidiForgeEngine.h"

#include <limits>
#include <vector>

namespace midiforge
{

/** Marks a preset value that should stay at the module's default. */
extern const float keepDefault;

/** A value that means "keep the module's default" (usable in in-class initialisers). */
inline constexpr float keepDefaultValue = std::numeric_limits<float>::quiet_NaN();

/** One slot of a preset. Values are plain (not normalised); keepDefault keeps the module's default. */
struct PresetSlot
{
    ModuleType moduleType;
    float plainValues[numParametersPerPage];         ///< Parameters A..D
    float secondPageValues[numParametersPerPage] = { ///< Parameters E..H; by default the module's defaults
        keepDefaultValue, keepDefaultValue, keepDefaultValue, keepDefaultValue
    };

    /** @return The plain value for parameter 0..7, or keepDefault. */
    float valueOf (int parameterIndex) const noexcept
    {
        return parameterIndex < numParametersPerPage ? plainValues[parameterIndex]
                                                     : secondPageValues[parameterIndex - numParametersPerPage];
    }
};

/** An LFO a preset sets up. LFOs a preset does not mention are reset to their defaults. */
struct PresetLfo
{
    int lfoIndex; ///< 0..numLfos-1
    LfoSettings settings;
};

/** A Note Random modulator a preset sets up. Modulators a preset does not mention get their defaults. */
struct PresetNoteRandom
{
    int randomIndex; ///< 0..numNoteRandoms-1
    NoteRandomSettings settings;
};

/** A modulation route a preset sets up, filled from route 1 on; the remaining routes are cleared. */
struct PresetRoute
{
    int sourceIndex;         ///< 0..3 for LFO 1..4, 4..7 for Random 1..4
    int slotIndex;           ///< 0..numSlots-1
    int parameterIndex;      ///< 0..3 for A..D
    float amountPercent;     ///< -100..+100
    bool isUnipolar = false; ///< see ModulationRoute::isUnipolar
};

struct FactoryPreset
{
    const char* name;
    std::vector<PresetSlot> slots; ///< Filled from slot 1 on; the remaining slots are emptied.
    std::vector<PresetLfo> lfos;
    std::vector<PresetRoute> routes;
    std::vector<PresetNoteRandom> noteRandoms;
};

/** @return All factory presets. The first one ("Init") is an empty chain without modulation. */
const std::vector<FactoryPreset>& getFactoryPresets();

} // namespace midiforge
