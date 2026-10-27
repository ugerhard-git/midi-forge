/**
 * @file NoteSplitTests.cpp
 * @brief The Note Split module: which notes take which path, note-offs following their note, and slot
 *        changes or panic while notes are held.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"
#include "Engine/Modules/FilterModules.h"

#include <algorithm>

using namespace testing;

namespace
{
namespace Octaves
{
constexpr float thisOctave = 0, everyOctave = 1;
}

constexpr int hiHat = 42, kick = 36, snare = 38; // F#1, C1, D1 (General MIDI drums)

/** Note Split in slot 1 letting through [low, high]; Transpose +12 in slot 2 marks the notes that went down the chain.
 */
void setUpSplitWithTranspose (ChainTestRig& rig, float low, float high, float octaves = Octaves::thisOctave)
{
    rig.loadModule (0, ModuleType::NoteSplit, { low, high, octaves });
    rig.loadModule (1, ModuleType::Transpose, { 12, 0 });
}

bool containsNoteOn (const MidiEventList& events, int noteNumber)
{
    return std::any_of (events.begin(), events.end(), [noteNumber] (const MidiEvent& event)
                        { return event.isNoteOn() && event.noteNumber() == noteNumber; });
}
} // namespace

TEST_CASE ("note split: selection by range, in one octave or in every octave")
{
    EXPECT (isNoteSelectedByNoteSplit (60, 60, 60, false));
    EXPECT (! isNoteSelectedByNoteSplit (61, 60, 60, false));
    EXPECT (isNoteSelectedByNoteSplit (62, 64, 60, false)); // Low and High in either order
    EXPECT (! isNoteSelectedByNoteSplit (72, 60, 64, false));

    EXPECT (isNoteSelectedByNoteSplit (72, 60, 60, true)); // every C
    EXPECT (isNoteSelectedByNoteSplit (0, 60, 60, true));
    EXPECT (! isNoteSelectedByNoteSplit (73, 60, 60, true));
    EXPECT (isNoteSelectedByNoteSplit (48, 57, 60, true));   // A..C wraps around the octave: C
    EXPECT (isNoteSelectedByNoteSplit (35, 57, 60, true));   // B
    EXPECT (! isNoteSelectedByNoteSplit (50, 57, 60, true)); // D
    for (int note = 0; note < 128; ++note)
        EXPECT (isNoteSelectedByNoteSplit (note, 40, 51, true)); // a whole octave selects everything
}

TEST_CASE ("note split: selected notes go down the chain, all others skip it unchanged")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteSplit, { (float) hiHat, (float) hiHat, Octaves::thisOctave });
    rig.loadModule (1, ModuleType::Transpose, { 12, 0 });
    rig.loadModule (2, ModuleType::VelocityCurve, { 0, 1, 127, 127 }); // fixed velocity 127 below the split

    const auto output =
        rig.processBlock ({ noteOnAt (0, kick, 90), noteOnAt (10, hiHat, 60), noteOnAt (20, snare, 70) });
    EXPECT (output.size() == 3);
    for (const auto& event : output)
    {
        if (event.samplePosition == 10)
            EXPECT (event.noteNumber() == hiHat + 12 && event.velocity() == 127); // processed by slots 2 and 3
        else
            EXPECT ((event.noteNumber() == kick && event.velocity() == 90) ||
                    (event.noteNumber() == snare && event.velocity() == 70));
    }
}

TEST_CASE ("note split: slots above the split act on every note")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 1, 0 });                        // C3 -> C#3, D3 -> D#3
    rig.loadModule (1, ModuleType::NoteSplit, { 61, 61, Octaves::thisOctave }); // selects C#3 only
    rig.loadModule (2, ModuleType::Transpose, { 12, 0 });
    const auto output = rig.processBlock ({ noteOnAt (0, 60), noteOnAt (1, 62) });
    EXPECT (containsNoteOn (output, 73)); // 60 +1 selected +12
    EXPECT (containsNoteOn (output, 63)); // 62 +1, skipped the rest
}

TEST_CASE ("note split: non-note messages continue down the chain")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteSplit, { 60, 60, Octaves::thisOctave });
    rig.loadModule (1, ModuleType::ChannelRemap, { 0, 5 }); // everything to channel 5
    const MidiEvent pitchBend { 2, 0xE0, 0, 80, 3 };
    const MidiEvent channelPressure { 3, 0xD0, 50, 0, 2 };
    const auto output = rig.processBlock (
        { noteOnAt (0, 50), MidiEvent::controlChange (1, 0, 1, 64), pitchBend, channelPressure, noteOnAt (4, 60) });
    for (const auto& event : output)
    {
        if (event.isNoteOn() && event.noteNumber() == 50)
            EXPECT (event.channel() == 0); // skipped the remap
        else
            EXPECT (event.channel() == 4); // remapped: went down the chain
    }
    EXPECT (output.size() == 5);
}

TEST_CASE ("note split: note-offs and poly aftertouch follow their note after the range changes")
{
    ChainTestRig rig;
    setUpSplitWithTranspose (rig, 60, 60);
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 62) }); // 60 down the chain (as 72), 62 skips

    rig.setParameter (0, 0, 62); // now select 62 instead of 60
    rig.setParameter (0, 1, 62);
    const MidiEvent pressureOn60 { 0, 0xA0, 60, 100, 3 };
    const MidiEvent pressureOn62 { 0, 0xA0, 62, 100, 3 };
    const auto pressure = rig.processBlock ({ pressureOn60, pressureOn62 });
    EXPECT (pressure.size() == 2 && pressure[0].noteNumber() == 72 && pressure[1].noteNumber() == 62);

    const auto releases = rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 62) });
    EXPECT (releases.size() == 2);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note split: a key pressed again on the other path ends its first note")
{
    ChainTestRig rig;
    setUpSplitWithTranspose (rig, 60, 60);
    rig.processBlock ({ noteOnAt (0, 60) }); // down the chain, sounds as 72
    rig.setParameter (0, 0, 0);
    rig.setParameter (0, 1, 0);              // 60 is no longer selected
    rig.processBlock ({ noteOnAt (0, 60) }); // pressed again: now skips the chain
    rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note split: switching the split slot or panic while notes are held leaves nothing stuck")
{
    ChainTestRig switchedOff;
    setUpSplitWithTranspose (switchedOff, 60, 60);
    switchedOff.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 50) }); // one per path
    switchedOff.slots[0].isEnabled = false;
    switchedOff.processBlock();
    EXPECT (switchedOff.notesStillSounding().empty());
    switchedOff.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 50) }); // late note-offs are swallowed
    EXPECT (switchedOff.notesStillSounding().empty());

    ChainTestRig replaced;
    setUpSplitWithTranspose (replaced, 60, 60);
    replaced.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 50) });
    replaced.loadModule (0, ModuleType::Chord, { 0, 0, 0, 100 });
    replaced.processBlock();
    EXPECT (replaced.notesStillSounding().empty());

    ChainTestRig panicked;
    setUpSplitWithTranspose (panicked, 60, 60);
    panicked.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 50) });
    panicked.engine.requestPanic();
    panicked.processBlock();
    EXPECT (panicked.notesStillSounding().empty());
}

TEST_CASE ("note split: chained splits split again; every octave selects all Cs")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteSplit, { 48, 72, Octaves::thisOctave });  // C2..C4 continue
    rig.loadModule (1, ModuleType::NoteSplit, { 60, 60, Octaves::everyOctave }); // of those, only Cs continue
    rig.loadModule (2, ModuleType::Transpose, { 1, 0 });
    const auto output = rig.processBlock ({ noteOnAt (0, 36), noteOnAt (0, 48), noteOnAt (0, 50), noteOnAt (0, 72) });
    EXPECT (containsNoteOn (output, 36)); // outside the first range: skipped at slot 1
    EXPECT (containsNoteOn (output, 49)); // C2: through both splits, transposed
    EXPECT (containsNoteOn (output, 50)); // D2: skipped at slot 2
    EXPECT (containsNoteOn (output, 73)); // C4: through both, transposed
}

TEST_CASE ("note split: the hi-hat ratchets, kick and snare stay as played")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteSplit, { (float) hiHat, (float) hiHat, Octaves::thisOctave });
    rig.loadModule (1, ModuleType::Repeater, { 7, 0, 50, 0 }); // 1/16
    rig.processBlock ({ noteOnAt (0, kick), noteOnAt (0, hiHat) });
    rig.processSeconds (0.5);
    rig.processBlock ({ noteOffAt (0, kick), noteOffAt (0, hiHat) });
    rig.processSeconds (0.5);

    int kickHits = 0, hiHatHits = 0;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            (event.noteNumber() == kick ? kickHits : hiHatHits)++;
    EXPECT (kickHits == 1);
    EXPECT (hiHatHits >= 4);
    EXPECT (rig.notesStillSounding().empty());
}
