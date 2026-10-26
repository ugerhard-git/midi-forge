/**
 * @file FactoryPresets.h
 * @brief The presets shipped with the plug-in, exposed to hosts as "programs".
 *
 * When you add or change a preset, update the preset list in docs/MANUAL.md.
 */
#pragma once

#include "Engine/ModuleChain.h"

#include <vector>

namespace midiforge
{

/** One slot of a preset. Values are plain (not normalised); keepDefault keeps the module's default. */
struct PresetSlot
{
    ModuleType moduleType;
    float plainValues[numParametersPerSlot];
};

struct FactoryPreset
{
    const char* name;
    std::vector<PresetSlot> slots; ///< Filled from slot 1 on; the remaining slots are emptied.
};

/** Marks a preset value that should stay at the module's default. */
extern const float keepDefault;

/** @return All factory presets. The first one ("Init") is an empty chain. */
const std::vector<FactoryPreset>& getFactoryPresets();

} // namespace midiforge
