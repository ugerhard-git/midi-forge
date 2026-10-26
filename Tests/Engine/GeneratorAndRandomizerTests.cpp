/**
 * @file GeneratorAndRandomizerTests.cpp
 * @brief Behaviour of the chord, echo, arpeggiator and randomizer modules.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cstdlib>
#include <set>

using namespace testing;

namespace
{
/** Index values of the choice parameters used below (see ModuleDescriptions.cpp). */
namespace ChordType
{
constexpr float major = 0, minor = 1, minor7 = 7;
}
namespace ArpRate
{
constexpr float eighth = 1, sixteenth = 3;
}
namespace ArpPattern
{
constexpr float ascending = 0, descending = 1;
}
namespace EchoTime
{
constexpr float eighth = 5;
}
} // namespace

//==============================================================================
// Chord

TEST_CASE ("chord: builds the chord and releases all of it, even after a type change")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Chord, { ChordType::minor, 0, 0, 100 });
    auto output = rig.processBlock ({ noteOnAt (0, 60) });
    EXPECT (output.size() == 3 && output[0].noteNumber() == 60 && output[1].noteNumber() == 63 &&
            output[2].noteNumber() == 67);

    rig.setParameter (0, 0, ChordType::minor7);
    output = rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (output.size() == 3);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("chord: inversion, open voicing, bass note and added-note velocity")
{
    ChainTestRig firstInversion;
    firstInversion.loadModule (0, ModuleType::Chord, { ChordType::major, 1, 0, 100 });
    auto output = firstInversion.processBlock ({ noteOnAt (0, 60) });
    EXPECT (output.size() == 3 && output[0].noteNumber() == 72 && output[1].noteNumber() == 64 &&
            output[2].noteNumber() == 67);

    ChainTestRig openVoicing;
    openVoicing.loadModule (0, ModuleType::Chord, { ChordType::major, 0, 1, 100 });
    output = openVoicing.processBlock ({ noteOnAt (0, 60) });
    EXPECT (output.size() == 3 && output[1].noteNumber() == 76); // E raised an octave

    ChainTestRig bassNote;
    bassNote.loadModule (0, ModuleType::Chord, { ChordType::major, 0, 2, 50 });
    output = bassNote.processBlock ({ noteOnAt (0, 60, 100) });
    EXPECT (output.size() == 4 && output[3].noteNumber() == 48);
    EXPECT (output[0].velocity() == 100 && output[1].velocity() == 50);
}

//==============================================================================
// Note Echo

TEST_CASE ("note echo: repeats with pitch step and decaying velocity, then releases everything")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteEcho, { EchoTime::eighth, 3, 50, 12 });
    rig.processBlock ({ noteOnAt (0, 48, 100) });
    rig.processSeconds (0.1);
    rig.processBlock ({ noteOffAt (0, 48) });
    rig.processSeconds (2.0);

    const auto played = rig.playedNoteNumbers();
    EXPECT (played.size() == 4);
    if (played.size() == 4)
        EXPECT (played[1] == 60 && played[2] == 72 && played[3] == 84);

    // 1/8 at 120 bpm = 0.25 s = 12000 samples between echoes
    std::vector<int> noteOnTimes;
    std::vector<int> velocities;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
        {
            noteOnTimes.push_back (event.samplePosition);
            velocities.push_back (event.velocity());
        }
    if (noteOnTimes.size() == 4)
    {
        EXPECT (noteOnTimes[1] - noteOnTimes[0] == 12000);
        EXPECT (noteOnTimes[3] - noteOnTimes[2] == 12000);
        EXPECT (velocities[1] == 50 && velocities[2] == 25);
    }
    EXPECT (rig.notesStillSounding().empty());
}

//==============================================================================
// Arpeggiator

TEST_CASE ("arpeggiator: free running, up pattern, starts with the first key")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Arpeggiator, { ArpRate::sixteenth, ArpPattern::ascending, 1, 50 });
    rig.processBlock ({ noteOnAt (0, 64), noteOnAt (0, 60), noteOnAt (0, 67) });
    rig.processSeconds (0.99); // 1/16 at 120 bpm = 125 ms -> 8 steps

    const auto played = rig.playedNoteNumbers();
    const std::vector<int> expected { 60, 64, 67, 60, 64, 67, 60, 64 };
    EXPECT (played == expected);

    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    rig.processSeconds (0.5);
    EXPECT (rig.playedNoteNumbers().size() <= expected.size() + 1);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("arpeggiator: follows the host grid, down pattern over two octaves")
{
    ChainTestRig rig;
    rig.context.isPlaying = true;
    rig.context.hasPpqPosition = true;
    rig.context.ppqPositionAtBlockStart = 0.1;
    rig.loadModule (0, ModuleType::Arpeggiator, { ArpRate::eighth, ArpPattern::descending, 2, 90 });
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 62) });
    rig.processSeconds (2.0); // 4 beats -> 8 eighths

    const std::vector<int> expected { 74, 72, 62, 60, 74, 72, 62, 60 };
    EXPECT (rig.playedNoteNumbers() == expected);

    // The first step lands on the next eighth at beat 0.5: (0.5 - 0.1) beats * 24000 samples = 9600.
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
        {
            EXPECT (std::abs (event.samplePosition - 9600) <= 1);
            break;
        }
}

TEST_CASE ("arpeggiator: steps exactly on the grid across many blocks")
{
    ChainTestRig rig;
    rig.context.isPlaying = true;
    rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::Arpeggiator, { ArpRate::sixteenth, ArpPattern::ascending, 1, 50 });
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (4.0);

    int previousTime = -1;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
        {
            if (previousTime >= 0)
                EXPECT (std::abs (event.samplePosition - previousTime - 6000) <= 1);
            previousTime = event.samplePosition;
        }
}

//==============================================================================
// Randomizers

TEST_CASE ("probability: 0% blocks everything, 100% passes everything")
{
    ChainTestRig never;
    never.loadModule (0, ModuleType::Probability, { 0 });
    ChainTestRig always;
    always.loadModule (0, ModuleType::Probability, { 100 });
    for (int note = 30; note < 90; ++note)
    {
        EXPECT (never.processBlock ({ noteOnAt (0, note) }).empty());
        EXPECT (always.processBlock ({ noteOnAt (0, note) }).size() == 1);
    }
}

TEST_CASE ("random pitch: octave mode only moves by octaves within the range")
{
    setRandomSeedForNewModules (42);
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::RandomPitch, { 24, 100, 1 });
    std::set<int> seenShifts;
    for (int repeat = 0; repeat < 200; ++repeat)
    {
        const auto output = rig.processBlock ({ noteOnAt (0, 60) });
        EXPECT (output.size() >= 1);
        const int shift = output.back().noteNumber() - 60;
        EXPECT (shift % 12 == 0 && std::abs (shift) <= 24 && shift != 0);
        seenShifts.insert (shift);
        rig.processBlock ({ noteOffAt (0, 60) });
    }
    EXPECT (seenShifts.size() == 4); // -24, -12, +12, +24
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("humanize: delays notes within the limit and keeps note lengths")
{
    setRandomSeedForNewModules (7);
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Humanize, { 20, 0 }); // up to 20 ms = 960 samples
    for (int repeat = 0; repeat < 50; ++repeat)
    {
        rig.processBlock ({ noteOnAt (0, 60) });
        rig.processSeconds (0.1);
        rig.processBlock ({ noteOffAt (0, 60) });
        rig.processSeconds (0.1);
    }

    int noteOnTime = -1;
    int index = 0;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isNoteOn())
        {
            noteOnTime = event.samplePosition;
            const int inputTime = index * 2 * (int) (0.1 * testSampleRate / testBlockSize + 1) * testBlockSize;
            EXPECT (noteOnTime >= inputTime && noteOnTime <= inputTime + 960);
            EXPECT (event.velocity() == 100);
        }
        else if (event.isNoteOff())
        {
            const int expectedLength = (int) (0.1 * testSampleRate / testBlockSize + 1) * testBlockSize;
            EXPECT (event.samplePosition - noteOnTime == expectedLength);
            ++index;
        }
    }
    EXPECT (index == 50);
}
