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

/**
 * Decides whether Note Split selects a note.
 * @param lowNote, highNote  The selected range (may be given in either order).
 * @param everyOctave        If true, the range's pitch classes are selected in every octave, wrapping
 *                           around the octave (A3..C4 selects every A, A#, B and C); a range of 12
 *                           semitones or more selects every note.
 */
bool isNoteSelectedByNoteSplit (int noteNumber, int lowNote, int highNote, bool everyOctave) noexcept;

} // namespace midiforge
