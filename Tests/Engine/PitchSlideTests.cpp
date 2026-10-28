/**
 * @file PitchSlideTests.cpp
 * @brief The Pitch Slide module: slide in and out, fall off on release, delay, curve, chance, chords, the
 *        player's own bend, the bend range, and leaving the synth in tune when the slot changes.
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
constexpr float slideIn = 0, slideOut = 1, fallOff = 2, inAndFallOff = 3;
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

namespace
{
/** @return The absolute times of the note-offs (and velocity-0 note-ons) of a note. */
std::vector<int> noteOffTimesOf (const ChainTestRig& rig, int noteNumber)
{
    std::vector<int> times;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOff() && event.noteNumber() == noteNumber)
            times.push_back (event.samplePosition);
    return times;
}

/** Runs blocks until the given absolute time is inside the next block, then plays the events there. */
void playAt (ChainTestRig& rig, int time, MidiEventList events)
{
    while (rig.samplesElapsed + testBlockSize <= time)
        rig.processBlock();
    for (auto& event : events)
        event.samplePosition = time - (int) rig.samplesElapsed;
    rig.processBlock (events);
}
} // namespace

TEST_CASE ("pitch slide: fall off keeps the note after release and lets it fall to Offset")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::fallOff, SlideLength::sixteenth);
    playAt (rig, 0, { noteOnAt (0, 60) });
    const int release = 20000;
    playAt (rig, release, { noteOffAt (0, 60) });
    rig.processSeconds (0.5);

    EXPECT (noteOffTimesOf (rig, 60) == (std::vector<int> { release + sixteenthAt120Bpm }));
    const auto bends = bendsOf (rig);
    EXPECT (! bends.empty());
    if (! bends.empty())
    {
        EXPECT (bends.front().time >= release);                    // nothing bends while the key is held
        EXPECT (bends.back().value == centre);                     // back in tune afterwards
        EXPECT (bends[bends.size() - 2].value == 0);               // arrived at -2 st (the bend range)
        EXPECT (bends.back().time == release + sixteenthAt120Bpm); // right after the note-off
    }
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("pitch slide: a released chord falls together; a key released alone ends a moment later")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -12, SlideMode::fallOff, SlideLength::sixteenth, 100, 0, 12);
    playAt (rig, 0, { noteOnAt (0, 48), noteOnAt (0, 60), noteOnAt (0, 64) });
    playAt (rig, 10000, { noteOffAt (0, 48) }); // bass let go early: ends 30 ms later, no fall
    playAt (rig, 30000, { noteOffAt (0, 60) });
    playAt (rig, 30480, { noteOffAt (0, 64) }); // 10 ms later: the chord falls as one
    rig.processSeconds (0.5);

    EXPECT (noteOffTimesOf (rig, 48) == (std::vector<int> { 10000 + 1440 }));
    EXPECT (noteOffTimesOf (rig, 60) == (std::vector<int> { 30480 + sixteenthAt120Bpm }));
    EXPECT (noteOffTimesOf (rig, 64) == (std::vector<int> { 30480 + sixteenthAt120Bpm }));
    EXPECT (bendsOf (rig).front().time >= 30480);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("pitch slide: a new note cuts a fall short and starts in tune")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::fallOff, SlideLength::quarter);
    playAt (rig, 0, { noteOnAt (0, 60) });
    playAt (rig, 10000, { noteOffAt (0, 60) });
    playAt (rig, 16000, { noteOnAt (0, 62) }); // during the fall (a quarter note = 24000 samples)
    playAt (rig, 30000, { noteOffAt (0, 62) });
    rig.processSeconds (1.0);

    EXPECT (noteOffTimesOf (rig, 60) == (std::vector<int> { 16000 }));
    // The bend is back at the centre when the new note starts.
    int bendAtNewNote = -1;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isPitchBend() && event.samplePosition <= 16000)
            bendAtNewNote = event.pitchBendValue();
    }
    EXPECT (bendAtNewNote == centre);
    EXPECT (noteOffTimesOf (rig, 62) == (std::vector<int> { 30000 + 24000 }));
    EXPECT (bendsOf (rig).back().value == centre);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("pitch slide: in + fall off slides in and falls off; Chance 0 leaves notes alone")
{
    ChainTestRig rig;
    setUpPitchSlide (rig, -2, SlideMode::inAndFallOff, SlideLength::sixteenth);
    playAt (rig, 0, { noteOnAt (0, 60) });
    playAt (rig, 20000, { noteOffAt (0, 60) });
    rig.processSeconds (0.5);
    const auto bends = bendsOf (rig);
    EXPECT (! bends.empty() && bends.front().time == 0 && bends.front().value == 0); // starts at -2 st
    EXPECT (noteOffTimesOf (rig, 60) == (std::vector<int> { 20000 + sixteenthAt120Bpm }));
    EXPECT (bendsOf (rig).back().value == centre);

    ChainTestRig never;
    setUpPitchSlide (never, -2, SlideMode::fallOff, SlideLength::sixteenth, 0);
    playAt (never, 0, { noteOnAt (0, 60) });
    playAt (never, 20000, { noteOffAt (0, 60) });
    never.processSeconds (0.5);
    EXPECT (noteOffTimesOf (never, 60) == (std::vector<int> { 20000 }));
    EXPECT (bendsOf (never).empty());

    // The Delay knob only applies to Slide In and Slide Out, so the editor dims it in the fall modes.
    const auto& description = getModuleDescription (ModuleType::PitchSlide);
    float values[numParametersPerSlot] {};
    values[1] = plainValueToNormalised (description.parameters[1], SlideMode::fallOff);
    EXPECT (isParameterInactive ((int) ModuleType::PitchSlide, 6, values));
    values[1] = plainValueToNormalised (description.parameters[1], SlideMode::slideOut);
    EXPECT (! isParameterInactive ((int) ModuleType::PitchSlide, 6, values));
}
