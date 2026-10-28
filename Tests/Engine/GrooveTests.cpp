/**
 * @file GrooveTests.cpp
 * @brief The Groove module: snapping to the templates, the amount, velocity accents, the Early delay,
 *        note lengths, and passing notes unchanged while the DAW is stopped.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>

using namespace testing;

namespace
{
/** Choice indices (see ModuleDescriptions.cpp). */
namespace GrooveTemplate
{
constexpr float swing = 0, laidBackSnare = 1, pushedOffBeats = 2, shuffle = 3;
}
namespace Grid
{
constexpr float eighth = 0, sixteenth = 1;
}
constexpr double samplesPerBeat = 24000.0; // 120 bpm at 48 kHz
constexpr int sixteenth = 6000;

/** Loads a Groove into slot 1 with the transport running from beat 0. */
void setUpGroove (ChainTestRig& rig, float grooveTemplate, float amount, float grid, float velocity = 0,
                  float early = 0)
{
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.context.ppqPositionAtBlockStart = 0.0;
    rig.loadModule (0, ModuleType::Groove, { grooveTemplate, amount, grid, velocity, early });
}

struct Hit
{
    int time;
    int velocity;
};

/** Plays a short note at each of the given times (in samples from the start) and returns the note-ons. */
std::vector<Hit> playAt (ChainTestRig& rig, std::initializer_list<int> times, int velocity = 100)
{
    for (int time : times)
    {
        while (rig.samplesElapsed + testBlockSize <= time)
            rig.processBlock();
        rig.processBlock ({ noteOnAt (time - (int) rig.samplesElapsed, 60, velocity) });
        rig.processBlock ({ noteOffAt (100, 60) });
    }
    rig.processSeconds (0.5);
    std::vector<Hit> hits;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            hits.push_back ({ event.samplePosition, event.velocity() });
    return hits;
}
} // namespace

TEST_CASE ("groove: swing delays every second grid step, fully at amount 100 %")
{
    ChainTestRig rig;
    setUpGroove (rig, GrooveTemplate::swing, 100, Grid::sixteenth);
    const auto hits = playAt (rig, { 0, sixteenth, 2 * sixteenth, 3 * sixteenth });
    EXPECT (hits.size() == 4);
    if (hits.size() == 4)
    {
        EXPECT (hits[0].time == 0 && hits[2].time == 2 * sixteenth);
        EXPECT (hits[1].time == sixteenth + 1200 && hits[3].time == 3 * sixteenth + 1200); // 60 % swing: +0.2 step
    }
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("groove: amount sets how far notes move; off-grid notes snap towards the groove")
{
    ChainTestRig half;
    setUpGroove (half, GrooveTemplate::shuffle, 50, Grid::eighth);
    const auto halfHits = playAt (half, { 12000 });                    // the second eighth of beat 1
    EXPECT (halfHits.size() == 1 && halfHits[0].time == 12000 + 2000); // half of a third of an eighth

    // A late note can only be moved earlier with room from Early (30 ms = 1440 samples).
    ChainTestRig sloppy;
    setUpGroove (sloppy, GrooveTemplate::swing, 100, Grid::sixteenth, 0, 30);
    const auto sloppyHits = playAt (sloppy, { 2 * sixteenth + 700, 4 * sixteenth - 500 }); // late and early
    EXPECT (sloppyHits.size() == 2 && sloppyHits[0].time == 2 * sixteenth + 1440 &&
            sloppyHits[1].time == 4 * sixteenth + 1440);
}

TEST_CASE ("groove: notes can only come early with Early, which delays everything else")
{
    // Pushed off-beats: the "and" of each beat comes 0.15 steps early.
    ChainTestRig noEarly;
    setUpGroove (noEarly, GrooveTemplate::pushedOffBeats, 100, Grid::sixteenth);
    const auto noEarlyHits = playAt (noEarly, { 0, 2 * sixteenth });
    EXPECT (noEarlyHits.size() == 2 && noEarlyHits[0].time == 0 && noEarlyHits[1].time == 2 * sixteenth);

    ChainTestRig withEarly;
    setUpGroove (withEarly, GrooveTemplate::pushedOffBeats, 100, Grid::sixteenth, 0, 30); // 30 ms = 1440 samples
    const auto earlyHits = playAt (withEarly, { 0, 2 * sixteenth });
    EXPECT (earlyHits.size() == 2 && earlyHits[0].time == 1440 && earlyHits[1].time == 2 * sixteenth + 1440 - 900);
    EXPECT (withEarly.notesStillSounding().empty());
}

TEST_CASE ("groove: the laid-back snare drags beats 2 and 4 and accents them")
{
    ChainTestRig rig;
    setUpGroove (rig, GrooveTemplate::laidBackSnare, 100, Grid::sixteenth, 100);
    const int beat = (int) samplesPerBeat;
    const auto hits = playAt (rig, { 0, beat, 2 * beat, 3 * beat }, 90);
    EXPECT (hits.size() == 4);
    if (hits.size() == 4)
    {
        EXPECT (hits[0].time == 0 && hits[2].time == 2 * beat);
        EXPECT (hits[1].time == beat + 1200 && hits[3].time == 3 * beat + 1200);
        EXPECT (hits[1].velocity == 102 && hits[0].velocity == 94);
    }
}

TEST_CASE ("groove: note lengths stay, and notes pass unchanged while the DAW is stopped")
{
    ChainTestRig rig;
    setUpGroove (rig, GrooveTemplate::swing, 100, Grid::sixteenth);
    rig.processBlock ({ noteOnAt (0, 60) });
    for (int block = 0; block < 22; ++block)
        rig.processBlock();
    rig.processBlock ({ noteOffAt (100, 60) });
    rig.processSeconds (0.2);
    // The note started on step 1 (sample 6000 is in block 23): it is moved, and so is its note-off.
    int onTime = -1, offTime = -1;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isNoteOn())
            onTime = event.samplePosition;
        if (event.isNoteOff())
            offTime = event.samplePosition;
    }
    EXPECT (onTime == 0 && offTime == 23 * testBlockSize + 100);

    ChainTestRig stopped;
    stopped.loadModule (0, ModuleType::Groove, { GrooveTemplate::swing, 100, Grid::sixteenth, 100, 0 });
    const auto output = stopped.processBlock ({ noteOnAt (10, 60, 100) });
    EXPECT (output.size() == 1 && output[0].samplePosition == 10 && output[0].velocity() == 100);
}
