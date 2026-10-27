/**
 * @file PitchSlideTests.cpp
 * @brief The Pitch Slide module: slide in and out, delay, curve, chance, chords, the player's own bend,
 *        the bend range, and leaving the synth in tune when the slot changes.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>

using namespace testing;

namespace
{
/** Choice indices (see ModuleDescriptions.cpp). */
namespace SlideMode
{
constexpr float slideIn = 0, slideOut = 1;
}
namespace SlideLength
{
constexpr float sixteenth = 5, quarter = 11;
}
namespace SlideDelay
{
constexpr float off = 0, sixteenth = 6;
}

constexpr int sixteenthAt120Bpm = 6000; // samples at 48 kHz
constexpr int centre = MidiEvent::pitchBendCentre;

struct Bend
{
    int time;
    int value;
};

std::vector<Bend> bendsOf (const ChainTestRig& rig)
{
    std::vector<Bend> bends;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isPitchBend())
            bends.push_back ({ event.samplePosition, event.pitchBendValue() });
    return bends;
}

/** Loads a Pitch Slide into slot 1: offset, mode, length, chance, curve, bend range, delay. */
void setUpPitchSlide (ChainTestRig& rig, float offset, float mode, float length, float chance = 100, float curve = 0,
                      float bendRange = 2, float delay = SlideDelay::off)
{
    rig.loadModule (0, ModuleType::PitchSlide, { offset, mode, length, chance, curve, bendRange, delay });
}

/** @return The index of the first event at the given time that is a note-on, or -1. */
int indexOfFirstNoteOn (const ChainTestRig& rig)
{
    for (size_t index = 0; index < rig.everyOutputEvent.size(); ++index)
        if (rig.everyOutputEvent[index].isNoteOn())
            return (int) index;
    return -1;
}
} // namespace

TEST_CASE ("pitch slide: slide in starts the note at the offset and glides to its pitch in the slide length")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::slideIn, SlideLength::sixteenth);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.5);

    // The bend comes first, so the note starts two semitones down (the full bend range).
    const int firstNoteOn = indexOfFirstNoteOn (rig);
    EXPECT (firstNoteOn == 1);
    EXPECT (rig.everyOutputEvent[0].isPitchBend() && rig.everyOutputEvent[0].pitchBendValue() == 0);
    EXPECT (rig.everyOutputEvent[1].noteNumber() == 60); // the note itself is untouched

    const auto bends = bendsOf (rig);
    EXPECT (bends.size() > 100 && bends.size() < 130); // about one per millisecond for 125 ms
    for (size_t index = 1; index < bends.size(); ++index)
        EXPECT (bends[index].value > bends[index - 1].value && bends[index].time > bends[index - 1].time);
    EXPECT (bends.back().value == centre && bends.back().time == sixteenthAt120Bpm);

    // Halfway through a linear slide the bend is halfway.
    for (const auto& bend : bends)
        if (bend.time == sixteenthAt120Bpm / 2)
            EXPECT (std::abs (bend.value - centre / 2) <= 1);
}

TEST_CASE ("pitch slide: slide out waits for the delay, glides to the offset, and returns when the note ends")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, 12, SlideMode::slideOut, SlideLength::sixteenth, 100, 0, 12, SlideDelay::sixteenth);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.5);
    rig.processBlock ({ noteOffAt (10, 60) });
    const int noteOffTime = (int) rig.samplesElapsed - testBlockSize + 10;

    const auto bends = bendsOf (rig);
    EXPECT (bends.size() > 100);
    EXPECT (bends.front().time > sixteenthAt120Bpm); // nothing during the delay
    EXPECT (bends.front().time <= sixteenthAt120Bpm + 48);
    EXPECT (bends[bends.size() - 2].value == MidiEvent::maximumPitchBend); // +12 of 12 semitones
    EXPECT (bends[bends.size() - 2].time == 2 * sixteenthAt120Bpm);
    EXPECT (bends.back().value == centre && bends.back().time == noteOffTime);

    // The note-off comes before the bend back, so the note ends at the slid pitch.
    const auto& lastEvents = rig.everyOutputEvent;
    EXPECT (lastEvents[lastEvents.size() - 2].isNoteOff() && lastEvents.back().isPitchBend());
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("pitch slide: slide in with a delay waits at the offset first")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::slideIn, SlideLength::sixteenth, 100, 0, 2, SlideDelay::sixteenth);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.5);

    const auto bends = bendsOf (rig);
    EXPECT (bends.front().value == 0 && bends.front().time == 0);
    EXPECT (bends[1].time > sixteenthAt120Bpm); // the glide starts after the delay
    EXPECT (bends.back().value == centre && bends.back().time == 2 * sixteenthAt120Bpm);
}

TEST_CASE ("pitch slide: the curve makes the slide start fast or slow")
{
    const auto bendHalfway = [] (float curve)
    {
        ChainTestRig rig;
        setUpPitchSlide (rig, -2, SlideMode::slideIn, SlideLength::sixteenth, 100, curve);
        rig.processBlock ({ noteOnAt (0, 60) });
        rig.processSeconds (0.3);
        int value = -1;
        for (const auto& bend : bendsOf (rig))
            if (bend.time <= sixteenthAt120Bpm / 2)
                value = bend.value;
        return value;
    };
    const int linear = bendHalfway (0);
    EXPECT (bendHalfway (-100) > linear + 2000); // fast start: most of the way already
    EXPECT (bendHalfway (100) < linear - 2000);  // slow start: still near the offset
}

TEST_CASE ("pitch slide: chance 0 % never bends, the notes pass untouched")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::slideIn, SlideLength::sixteenth, 0);
    for (int note = 0; note < 20; ++note)
    {
        rig.processBlock ({ noteOnAt (0, 60 + note) });
        rig.processSeconds (0.1);
        rig.processBlock ({ noteOffAt (0, 60 + note) });
    }
    EXPECT (bendsOf (rig).empty());
    EXPECT (rig.playedNoteNumbers().size() == 20);
}

TEST_CASE ("pitch slide: with a chance, some notes slide and others don't; a chord decides once")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::slideIn, SlideLength::sixteenth, 50);
    int numSlid = 0;
    constexpr int numChords = 200;
    for (int chord = 0; chord < numChords; ++chord)
    {
        const size_t eventsBefore = rig.everyOutputEvent.size();
        // A chord played slightly unevenly: within 30 ms.
        rig.processBlock ({ noteOnAt (0, 60), noteOnAt (100, 64) });
        rig.processBlock ({ noteOnAt (50, 67) });
        rig.processSeconds (0.2);
        rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });

        // One slide for the whole chord: a bend to the offset only before its first note.
        int numJumpsToOffset = 0;
        for (size_t index = eventsBefore; index < rig.everyOutputEvent.size(); ++index)
            if (rig.everyOutputEvent[index].isPitchBend() && rig.everyOutputEvent[index].pitchBendValue() == 0)
                ++numJumpsToOffset;
        EXPECT (numJumpsToOffset <= 1);
        numSlid += numJumpsToOffset;
    }
    EXPECT (numSlid > numChords / 2 - 40 && numSlid < numChords / 2 + 40);
    EXPECT (rig.notesStillSounding().empty());
    EXPECT (bendsOf (rig).back().value == centre); // in tune at the end
}

TEST_CASE ("pitch slide: a later note starts a new slide and the held notes bend along")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::slideIn, SlideLength::sixteenth);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.5);
    const int secondNoteTime = (int) rig.samplesElapsed;
    rig.processBlock ({ noteOnAt (0, 67) });
    rig.processSeconds (0.5);

    int numJumpsToOffset = 0;
    for (const auto& bend : bendsOf (rig))
        if (bend.value == 0)
        {
            ++numJumpsToOffset;
            EXPECT (bend.time == 0 || bend.time == secondNoteTime);
        }
    EXPECT (numJumpsToOffset == 2);
}

TEST_CASE ("pitch slide: the player's own bend is added to the slide")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, 1, SlideMode::slideOut, SlideLength::sixteenth); // ends a semitone up (+4096)
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.5);
    auto playerBend = MidiEvent::pitchBend (0, 0, centre + 1000);
    rig.processBlock ({ playerBend });
    EXPECT (bendsOf (rig).back().value == centre + 4096 + 1000);

    // Without a slide the player's bend passes as it is.
    rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (bendsOf (rig).back().value == centre + 1000);
    rig.processBlock ({ MidiEvent::pitchBend (0, 0, 3000) });
    EXPECT (bendsOf (rig).back().value == 3000);
}

TEST_CASE ("pitch slide: an offset beyond the bend range stops at the end of the range")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -24, SlideMode::slideIn, SlideLength::sixteenth, 100, 0, 2);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.3);
    const auto bends = bendsOf (rig);
    EXPECT (bends.front().value == 0);
    EXPECT (bends.back().value == centre);
}

TEST_CASE ("pitch slide: switching the slot off or panic in the middle of a slide leaves the synth in tune")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, 2, SlideMode::slideOut, SlideLength::quarter);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (0.1);
    EXPECT (bendsOf (rig).back().value > centre);
    rig.slots[0].isEnabled = false;
    rig.processSeconds (0.2);
    EXPECT (bendsOf (rig).back().value == centre);
    EXPECT (rig.notesStillSounding().empty());

    ChainTestRig panicked;
    setUpPitchSlide (panicked, 2, SlideMode::slideOut, SlideLength::quarter);
    panicked.processBlock ({ noteOnAt (0, 60) });
    panicked.processSeconds (0.1);
    panicked.engine.requestPanic();
    panicked.processBlock();
    EXPECT (bendsOf (panicked).back().value == centre);
    panicked.processSeconds (0.5);
    EXPECT (bendsOf (panicked).back().value == centre); // the slide does not continue after the panic
}
