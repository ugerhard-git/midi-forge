/**
 * @file NoteLengthTests.cpp
 * @brief The Note Length module: fixed lengths (note values, ms), Stretch, Legato with Overlap and Max Length,
 *        chords, keys played again, the DAW stopping, Velocity > Length, the meter and the dimmed knobs.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

using namespace testing;

namespace
{
constexpr int samplesPerQuarter = 24000; // 120 bpm at 48 kHz

/** Note Length knob positions used below. */
namespace Length
{
constexpr float noteValue = 0, milliseconds = 1, stretch = 2;
constexpr float eighth = 8, oneBar = 16, legato = 17; // see noteLengthValueLabels
constexpr float maximumOff = 0, maximumQuarter = 1;
constexpr int lengthKnob = 1;
} // namespace Length

/** A note at the chain's output: when it started and ended (-1: not ended). */
struct PlayedNote
{
    int noteNumber;
    int start;
    int end;
};

/** @return Every note at the output with its start and end, in the order the notes started. */
std::vector<PlayedNote> notesOf (const ChainTestRig& rig)
{
    std::vector<PlayedNote> notes;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isNoteOn())
            notes.push_back ({ event.noteNumber(), event.samplePosition, -1 });
        else if (event.isNoteOff())
            for (auto& note : notes)
                if (note.noteNumber == event.noteNumber() && note.end < 0)
                {
                    note.end = event.samplePosition;
                    break;
                }
    }
    return notes;
}

/** @return How many note-offs of one note number were sent. */
int noteOffCountOf (const ChainTestRig& rig, int noteNumber)
{
    int count = 0;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOff() && event.noteNumber() == noteNumber)
            ++count;
    return count;
}

/** Sets Length in the unit the current Mode uses (ms or stretch %). */
void setLengthInCurrentUnit (ChainTestRig& rig, float plainValue)
{
    auto& values = rig.slots[0].parameterValues;
    const auto& lengthDescription =
        getEffectiveParameterDescription ((int) ModuleType::NoteLength, Length::lengthKnob, values);
    values[Length::lengthKnob] = plainValueToNormalised (lengthDescription, plainValue);
}

/** Plays a key at an absolute block and releases it some blocks later. */
void playKey (ChainTestRig& rig, int noteNumber, int heldBlocks, int velocity = 100)
{
    rig.processBlock ({ noteOnAt (0, noteNumber, velocity) });
    for (int block = 1; block < heldBlocks; ++block)
        rig.processBlock();
    rig.processBlock ({ noteOffAt (0, noteNumber) });
}
} // namespace

TEST_CASE ("note length: a note value sets the length, however long the key is held")
{
    EXPECT (getModuleDescription (ModuleType::NoteLength).category == ModuleCategory::Transform);

    for (int heldBlocks : { 2, 100 }) // released long before and long after the 1/8 (12000 samples)
    {
        ChainTestRig rig;
        rig.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::eighth });
        playKey (rig, 60, heldBlocks);
        rig.processSeconds (1.0);
        const auto notes = notesOf (rig);
        EXPECT (notes.size() == 1 && notes[0].start == 0 && notes[0].end == samplesPerQuarter / 2);
        EXPECT (noteOffCountOf (rig, 60) == 1); // the key's own note-off is not sent on
    }

    // 1 Bar follows the time signature: 3 quarter notes in 3/4.
    ChainTestRig waltz;
    waltz.context.timeSignature = { 3, 4 };
    waltz.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::oneBar });
    playKey (waltz, 60, 2);
    waltz.processSeconds (2.0);
    const auto notes = notesOf (waltz);
    EXPECT (notes.size() == 1 && notes[0].end == 3 * samplesPerQuarter);
}

TEST_CASE ("note length: milliseconds and Stretch; Stretch only lengthens and keeps to Max Length")
{
    ChainTestRig milliseconds;
    milliseconds.loadModule (0, ModuleType::NoteLength, { Length::milliseconds });
    setLengthInCurrentUnit (milliseconds, 250.0f);
    playKey (milliseconds, 60, 100);
    milliseconds.processSeconds (1.0);
    auto notes = notesOf (milliseconds);
    EXPECT (notes.size() == 1 && notes[0].end == 12000);

    // Stretch 300 %: a key held 10 blocks (2560 samples) sounds 7680 samples.
    ChainTestRig stretched;
    stretched.loadModule (0, ModuleType::NoteLength, { Length::stretch, 0, 0, Length::maximumOff });
    setLengthInCurrentUnit (stretched, 300.0f);
    playKey (stretched, 60, 10);
    stretched.processSeconds (1.0);
    notes = notesOf (stretched);
    EXPECT (notes.size() == 1 && notes[0].end == 3 * 10 * testBlockSize);
    EXPECT (noteOffCountOf (stretched, 60) == 1);

    // Held for 1 s with Max Length 1/4 (0.5 s): it ends at the maximum, before the key is released.
    ChainTestRig capped;
    capped.loadModule (0, ModuleType::NoteLength, { Length::stretch, 0, 0, Length::maximumQuarter });
    setLengthInCurrentUnit (capped, 200.0f);
    playKey (capped, 60, 190);
    capped.processSeconds (2.0);
    notes = notesOf (capped);
    EXPECT (notes.size() == 1 && notes[0].end == samplesPerQuarter);
    EXPECT (capped.notesStillSounding().empty());
}

TEST_CASE ("note length: Legato holds each note until the next one starts, plus Overlap")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::legato, 10, Length::maximumOff });
    playKey (rig, 60, 2);                    // released at once
    for (int block = 0; block < 97; ++block) // blocks 3 ... 99; the next note starts at block 100
        rig.processBlock();
    playKey (rig, 64, 2);
    rig.processSeconds (0.5);
    auto notes = notesOf (rig);
    EXPECT (notes.size() == 2);
    if (notes.size() == 2)
    {
        EXPECT (notes[0].end == 100 * testBlockSize + 480); // 10 ms after the next note
        EXPECT (notes[1].end < 0);                          // still waiting for its next note
    }

    // The DAW stopping ends a note that only waits for the next one.
    ChainTestRig stopping;
    stopping.context.isPlaying = stopping.context.hasPpqPosition = true;
    stopping.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::legato, 0, Length::maximumOff });
    playKey (stopping, 60, 2);
    stopping.processSeconds (0.5);
    EXPECT (! stopping.notesStillSounding().empty());
    stopping.context.isPlaying = false;
    stopping.processBlock();
    EXPECT (stopping.notesStillSounding().empty());

    // Max Length ends the last note of a phrase.
    ChainTestRig capped;
    capped.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::legato, 0, Length::maximumQuarter });
    playKey (capped, 60, 2);
    capped.processSeconds (1.0);
    notes = notesOf (capped);
    EXPECT (notes.size() == 1 && notes[0].end == samplesPerQuarter);
}

TEST_CASE ("note length: Legato ends a whole chord at the next chord; a key played again ends its note first")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::legato, 0, Length::maximumOff });
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (100, 64), noteOnAt (200, 67) }); // one chord (within 30 ms)
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    rig.processSeconds (0.2);
    EXPECT (rig.notesStillSounding().size() == 3);
    rig.processBlock ({ noteOnAt (0, 62), noteOnAt (10, 65) });
    const auto stillSounding = rig.notesStillSounding();
    EXPECT (stillSounding.size() == 2 && stillSounding.count (62) == 1 && stillSounding.count (65) == 1);

    // The same key again (on, on, off from a module in front): one note at a time, nothing left hanging.
    ChainTestRig again;
    again.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::oneBar });
    again.processBlock ({ noteOnAt (0, 60), noteOnAt (50, 60) });
    again.processBlock ({ noteOffAt (0, 60) });
    again.processSeconds (3.0);
    const auto notes = notesOf (again);
    EXPECT (notes.size() == 2 && notes[0].end == 50);
    EXPECT (again.notesStillSounding().empty());

    // A note-off for a note Note Length never saw passes on.
    ChainTestRig unknown;
    unknown.loadModule (0, ModuleType::NoteLength);
    const auto output = unknown.processBlock ({ noteOffAt (0, 70) });
    EXPECT (output.size() == 1 && output[0].isNoteOff());
}

TEST_CASE ("note length: Velocity > Length makes hard notes longer; knobs dim with the mode")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::eighth, 0, 0, 100 });
    rig.processBlock ({ noteOnAt (0, 60, 127), noteOnAt (0, 62, 64) });
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 62) });
    rig.processSeconds (1.0);
    const auto notes = notesOf (rig);
    EXPECT (notes.size() == 2);
    if (notes.size() == 2)
        EXPECT (notes[0].end == samplesPerQuarter && notes[1].end == samplesPerQuarter / 2); // 1/8 × 2, × 1

    const auto& noteLength = getModuleDescription (ModuleType::NoteLength);
    float values[numParametersPerSlot] {};
    const auto isDimmed = [&] (int knob) { return isParameterInactive ((int) ModuleType::NoteLength, knob, values); };
    values[1] = plainValueToNormalised (noteLength.parameters[1], Length::eighth);
    EXPECT (isDimmed (2) && isDimmed (3) && ! isDimmed (4)); // a fixed note value
    values[1] = plainValueToNormalised (noteLength.parameters[1], Length::legato);
    EXPECT (! isDimmed (2) && ! isDimmed (3) && isDimmed (4)); // Legato
    values[0] = plainValueToNormalised (noteLength.parameters[0], Length::stretch);
    EXPECT (isDimmed (2) && ! isDimmed (3) && ! isDimmed (4)); // Stretch

    // Length's unit follows Mode.
    EXPECT (formatParameterValue (getEffectiveParameterDescription ((int) ModuleType::NoteLength, 1, values), 0.0f) ==
            "100%");
    values[0] = plainValueToNormalised (noteLength.parameters[0], Length::milliseconds);
    EXPECT (formatParameterValue (getEffectiveParameterDescription ((int) ModuleType::NoteLength, 1, values), 1.0f) ==
            "2000 ms");
}

TEST_CASE ("note length: an LFO on Length goes from short notes to legato; each note keeps its start value")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteLength, { Length::noteValue, Length::eighth, 0, Length::maximumOff });
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 0.25; // one cycle in 4 s
    rig.lfos[0].waveform = LfoWaveform::SawUp;
    rig.routeLfo (0, 0, 0, Length::lengthKnob, 60.0f, true); // unipolar: Length from 1/8 up to Legato
    for (int note = 0; note < 16; ++note)                    // a note every quarter (0.5 s), each key held briefly
    {
        playKey (rig, 60 + note % 12, 2);
        for (int block = 0; block < 91; ++block)
            rig.processBlock();
    }
    rig.processSeconds (5.0);
    const auto notes = notesOf (rig);
    EXPECT (notes.size() == 16);
    // Early in the cycle the notes are short; late in it they last until the next note (legato).
    int legatoNotes = 0, shortNotes = 0;
    for (size_t index = 0; index + 1 < notes.size(); ++index)
    {
        if (notes[index].end == notes[index + 1].start)
            ++legatoNotes;
        else if (notes[index].end - notes[index].start <= samplesPerQuarter / 2)
            ++shortNotes;
    }
    EXPECT (legatoNotes > 0 && shortNotes > 0);
}
