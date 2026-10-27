/**
 * @file RandomizerModules.h
 * @brief Factory for the randomizer modules (Humanize, Probability, Random Pitch, Pitch Slide, Mutate).
 */
#pragma once

#include "../MidiModule.h"

namespace midiforge
{

/** Creates the randomizer module of the given type, or nullptr if the type is not a randomizer. */
std::unique_ptr<MidiModule> createRandomizerModule (ModuleType type);

} // namespace midiforge
