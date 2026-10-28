/**
 * @file StrumAndGhostNotesTests.cpp
 * @brief Strum (order, spacing, window, alternate, tilt, note lengths) and Ghost Notes (after, before from
 *        the previous bar, chance, count, velocity, length).
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <algorithm>

using namespace testing;

namespace
{
namespace StrumDirection
{
constexpr float upward = 0, downward = 1, alternate = 2;
}
namespace GhostPosition
{
constexpr float after = 0, before = 1;
}
constexpr float ghostSixteenth = 2;
constexpr int samplesPerMillisecond = 48;
constexpr int sixteenth = 6000; // at 120 bpm, 48 kHz

struct Hit
{
    int time;
    int noteNumber;
    int velocity;
};

std::vector<Hit> noteOnsOf (const ChainTestRig& rig)
{
    std::vector<Hit> hits;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            hits.push_back ({ event.samplePosition, event.noteNumber(), event.velocity() });
    return hits;
}

void playChord (ChainTestRig& rig, std::initializer_list<int> notes, int velocity = 100)
{
    MidiEventList chord;
    for (int note : notes)
        chord.push_back (noteOnAt (0, note, velocity));
    rig.processBlock (chord);
}

void releaseChord (ChainTestRig& rig, std::initializer_list<int> notes)
{
    MidiEventList chord;
    for (int note : notes)
        chord.push_back (noteOffAt (0, note));
    rig.processBlock (chord);
}
} // namespace

TEST_CASE ("strum: a chord is rolled Speed apart from low to high, after the Window")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Strum, { 20, StrumDirection::upward, 0, 10 });
    playChord (rig, { 67, 60, 64 });
    rig.processSeconds (0.2);
    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == 3);
    if (hits.size() == 3)
    {
        EXPECT (hits[0].noteNumber == 60 && hits[1].noteNumber == 64 && hits[2].noteNumber == 67);
        EXPECT (hits[0].time == 10 * samplesPerMillisecond);
        EXPECT (hits[1].time == 30 * samplesPerMillisecond && hits[2].time == 50 * samplesPerMillisecond);
    }
    releaseChord (rig, { 60, 64, 67 });
    rig.processSeconds (0.2);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("strum: notes played slightly apart within the Window still form one chord")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Strum, { 10, StrumDirection::downward, 0, 20 });
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processBlock ({ noteOnAt (100, 72), noteOnAt (200, 64) }); // 7 and 9 ms later
    rig.processSeconds (0.2);
    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == 3 && hits[0].noteNumber == 72 && hits[1].noteNumber == 64 && hits[2].noteNumber == 60);
    if (hits.size() == 3)
        EXPECT (hits[0].time == 20 * samplesPerMillisecond);
}

TEST_CASE ("strum: window 0 strums exactly simultaneous chords without any delay")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Strum, { 15, StrumDirection::upward, 0, 0 });
    playChord (rig, { 64, 60 });
    rig.processSeconds (0.1);
    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == 2 && hits[0].time == 0 && hits[0].noteNumber == 60 &&
            hits[1].time == 15 * samplesPerMillisecond);
}

TEST_CASE ("strum: alternate goes up, down, up, and starts up again after a pause; velocity tilt")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Strum, { 5, StrumDirection::alternate, -30, 0 });
    std::vector<int> firstNotes;
    const auto strumOnce = [&]
    {
        const size_t before = noteOnsOf (rig).size();
        playChord (rig, { 60, 64, 67 }, 100);
        rig.processSeconds (0.1);
        releaseChord (rig, { 60, 64, 67 });
        rig.processSeconds (0.1);
        const auto hits = noteOnsOf (rig);
        firstNotes.push_back (hits[before].noteNumber);
        return std::vector<Hit> (hits.begin() + (std::ptrdiff_t) before, hits.end());
    };
    const auto first = strumOnce();
    strumOnce();
    strumOnce();
    rig.processSeconds (1.0); // a pause: a new phrase
    strumOnce();
    EXPECT (firstNotes == std::vector<int> ({ 60, 67, 60, 60 }));
    EXPECT (first.size() == 3 && first[0].velocity == 100 && first[1].velocity == 85 && first[2].velocity == 70);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("strum: single notes pass delayed by the window, with their length kept")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Strum, { 20, StrumDirection::upward, 0, 10 });
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.1);
    const int releaseTime = (int) rig.samplesElapsed;
    rig.processBlock ({ noteOffAt (0, 60) });
    rig.processSeconds (0.1);
    int onTime = -1, offTime = -1;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isNoteOn())
            onTime = event.samplePosition;
        if (event.isNoteOff())
            offTime = event.samplePosition;
    }
    EXPECT (onTime == 480 && offTime == releaseTime + 480);
}

TEST_CASE ("ghost notes: after a note, Count quiet short copies Distance apart")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::GhostNotes, { 100, GhostPosition::after, ghostSixteenth, 2, 25 });
    rig.processBlock ({ noteOnAt (0, 38, 100) });
    rig.processBlock ({ noteOffAt (0, 38) });
    rig.processSeconds (0.5);
    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == 3);
    if (hits.size() == 3)
    {
        EXPECT (hits[0].time == 0 && hits[0].velocity == 100);
        EXPECT (hits[1].time == sixteenth && hits[1].velocity == 25 && hits[2].time == 2 * sixteenth);
    }
    // Ghost notes are short (a 1/64 note: 1500 samples).
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOff() && event.samplePosition > sixteenth)
            EXPECT (event.samplePosition == sixteenth + 1500 || event.samplePosition == 2 * sixteenth + 1500);
    EXPECT (rig.notesStillSounding().empty());

    ChainTestRig never;
    never.loadModule (0, ModuleType::GhostNotes, { 0, GhostPosition::after, ghostSixteenth, 3, 25 });
    never.processBlock ({ noteOnAt (0, 38) });
    never.processSeconds (0.5);
    EXPECT (noteOnsOf (never).size() == 1);
}

TEST_CASE ("ghost notes: before uses the previous bar, while the DAW plays")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::GhostNotes, { 100, GhostPosition::before, ghostSixteenth, 1, 30 });

    // A snare on beat 2 (sample 24000) of bar 1; nothing in bar 2 is played, but the ghost comes before beat 2.
    const int beat = 24000;
    while (rig.samplesElapsed + testBlockSize <= beat)
        rig.processBlock();
    rig.processBlock ({ noteOnAt (beat - (int) rig.samplesElapsed, 38, 100) });
    rig.processBlock ({ noteOffAt (0, 38) });
    rig.processSeconds (2.5); // bar 2 runs from sample 96000 to 192000
    const auto hits = noteOnsOf (rig);
    EXPECT (hits.size() == 2);
    if (hits.size() == 2)
        EXPECT (hits[1].time == 96000 + beat - sixteenth && hits[1].velocity == 30);
    EXPECT (rig.notesStillSounding().empty());
}
