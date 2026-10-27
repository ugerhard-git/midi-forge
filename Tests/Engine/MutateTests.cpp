/**
 * @file MutateTests.cpp
 * @brief The Mutate module: drifting in scale steps, returning, memory per pitch and per loop position,
 *        chords passing untouched, and resetting by button and transport start.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

using namespace testing;

namespace
{
/** Choice indices (see ModuleDescriptions.cpp). */
namespace Direction
{
constexpr float upward = 0, downward = 1, both = 2;
}
namespace Memory
{
constexpr float oneBar = 2, perPitch = 3;
}
constexpr float rootC = 0, majorScale = 0;

/** Loads Mutate into slot 1: amount, range, direction, return, root, scale, memory, reset. */
void setUpMutate (ChainTestRig& rig, float amount, float range, float direction, float returnAmount,
                  float memory = Memory::perPitch)
{
    rig.loadModule (0, ModuleType::Mutate, { amount, range, direction, returnAmount, rootC, majorScale, memory, 0 });
}

/** Plays a short note and returns the pitch that came out. */
int playNote (ChainTestRig& rig, int noteNumber)
{
    const size_t before = rig.everyOutputEvent.size();
    rig.processBlock ({ noteOnAt (0, noteNumber) });
    rig.processSeconds (0.05);
    rig.processBlock ({ noteOffAt (0, noteNumber) });
    rig.processSeconds (0.05);
    for (size_t index = before; index < rig.everyOutputEvent.size(); ++index)
        if (rig.everyOutputEvent[index].isNoteOn())
            return rig.everyOutputEvent[index].noteNumber();
    return -1;
}
} // namespace

TEST_CASE ("mutate: amount 0 leaves every note as it is")
{
    ChainTestRig rig;
    setUpMutate (rig, 0, 1, Direction::both, 30);
    for (int note = 50; note < 70; ++note)
        EXPECT (playNote (rig, note) == note);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("mutate: with memory a note drifts one scale step per play, and at most about an octave")
{
    ChainTestRig rig;
    setUpMutate (rig, 100, 1, Direction::upward, 0);
    // C major from C4 (60): D, E, F, G, A, B, C ... and then it stays seven steps up.
    const int expected[] = { 62, 64, 65, 67, 69, 71, 72, 72, 72 };
    for (int pitch : expected)
        EXPECT (playNote (rig, 60) == pitch);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("mutate: return pulls a drifted note back towards the original")
{
    ChainTestRig rig;
    setUpMutate (rig, 100, 1, Direction::downward, 0);
    EXPECT (playNote (rig, 64) == 62); // E -> D
    EXPECT (playNote (rig, 64) == 60); // -> C
    rig.setParameter (0, 0, 0);        // no more changes ...
    rig.setParameter (0, 3, 100);      // ... and always one step back
    EXPECT (playNote (rig, 64) == 62);
    EXPECT (playNote (rig, 64) == 64);
    EXPECT (playNote (rig, 64) == 64);
}

TEST_CASE ("mutate: notes outside the scale move to scale notes; every result is in key")
{
    ChainTestRig rig;
    setUpMutate (rig, 100, 3, Direction::both, 30);
    EXPECT (playNote (rig, 61) != 61);
    for (int round = 0; round < 200; ++round)
    {
        const int pitch = playNote (rig, 48 + round % 24);
        const int semitone = pitch % 12;
        const bool isInCMajor = semitone == 0 || semitone == 2 || semitone == 4 || semitone == 5 || semitone == 7 ||
                                semitone == 9 || semitone == 11;
        EXPECT (isInCMajor || pitch == 48 + round % 24); // unchanged notes may be out of key
    }
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("mutate: memory per position remembers each place in the loop separately")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    setUpMutate (rig, 100, 1, Direction::upward, 0, Memory::oneBar);

    // Bar 1: C on beat 1 -> D, C on beat 2 -> D (its own memory).  Bar 2: beat 1 -> E (same place, one more step).
    const auto playAtBeat = [&rig] (double beat)
    {
        rig.processSeconds (0.05); // well apart, so the notes don't count as a chord
        rig.context.ppqPositionAtBlockStart = beat;
        const auto output = rig.processBlock ({ noteOnAt (0, 60) });
        rig.processBlock ({ noteOffAt (0, 60) });
        return output.empty() ? -1 : output[0].noteNumber();
    };
    EXPECT (playAtBeat (0.0) == 62);
    EXPECT (playAtBeat (1.0) == 62);
    EXPECT (playAtBeat (4.0) == 64);
    EXPECT (playAtBeat (5.0) == 64);
    EXPECT (playAtBeat (4.02) == 65); // slightly late still counts as the same sixteenth
}

TEST_CASE ("mutate: chords pass untouched; a changed first note of a chord is restarted at its own pitch")
{
    ChainTestRig rig;
    setUpMutate (rig, 100, 1, Direction::upward, 0);
    const auto chord = rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64), noteOnAt (0, 67) });
    std::vector<int> started;
    for (const auto& event : chord)
        if (event.isNoteOn())
            started.push_back (event.noteNumber());
    // The first note was changed before the others arrived, then restarted as C.
    EXPECT (started.size() == 4 && started[0] == 62 && started[1] == 60 && started[2] == 64 && started[3] == 67);
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    EXPECT (rig.notesStillSounding().empty());

    // Legato melody notes (overlapping, but not starting together) still change.
    ChainTestRig legato;
    setUpMutate (legato, 100, 1, Direction::upward, 0);
    legato.processBlock ({ noteOnAt (0, 60) });
    legato.processSeconds (0.2);
    const auto second = legato.processBlock ({ noteOnAt (0, 64) });
    EXPECT (second.size() == 1 && second[0].noteNumber() == 65);
}

TEST_CASE ("mutate: the Reset button and the DAW starting to play bring back the original melody")
{
    ChainTestRig rig;
    setUpMutate (rig, 100, 1, Direction::upward, 0);
    playNote (rig, 60);
    playNote (rig, 60);
    rig.setParameter (0, 0, 0); // stop changing, so the next notes show the memory
    EXPECT (playNote (rig, 60) == 64);
    rig.setParameter (0, 7, 1); // press Reset
    EXPECT (playNote (rig, 60) == 60);
    rig.setParameter (0, 7, 0);

    rig.setParameter (0, 0, 100);
    playNote (rig, 60);
    rig.setParameter (0, 0, 0);
    EXPECT (playNote (rig, 60) == 62);
    rig.context.isPlaying = true; // the DAW starts playing
    EXPECT (playNote (rig, 60) == 60);
}
