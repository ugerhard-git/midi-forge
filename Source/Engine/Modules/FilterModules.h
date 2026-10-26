/**
 * @file FilterModules.h
 * @brief Factory for the filter modules (Note Range, Velocity Range, Channel Filter, Event Blocker).
 */
#pragma once

#include "../MidiModule.h"

namespace midiforge
{

/** Creates the filter module of the given type, or nullptr if the type is not a filter. */
std::unique_ptr<MidiModule> createFilterModule (ModuleType type);

} // namespace midiforge
