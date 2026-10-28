/**
 * @file GeneratorModules.h
 * @brief Factory for the generator modules (Arpeggiator, Chord, Note Echo, Repeater, Strum, Ghost Notes).
 */
#pragma once

#include "../MidiModule.h"

namespace midiforge
{

/** Creates the generator module of the given type, or nullptr if the type is not a generator. */
std::unique_ptr<MidiModule> createGeneratorModule (ModuleType type);

} // namespace midiforge
