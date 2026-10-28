/**
 * @file BeatThinningAndHarmonizerTests.cpp
 * @brief Beat Thinning (chances by place in the bar, odd-meter grouping, chords, stopped transport) and the
 *        Diatonic Harmonizer (intervals in key, two voices, velocity, chords passing, no stuck notes).
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

using namespace testing;

namespace
{
namespace Grid
{
constexpr float eighth = 0, sixteenth = 1;
}
namespace Grouping
{
constexpr float threeLast = 0, threeFirst = 1;
}
/** Voice knob positions (see harmonyIntervalLabels). */
namespace Voice
{
constexpr float octaveDown = 0, fifthDown = 3, off = 7, thirdUp = 9, fifthUp = 11, octaveUp = 14;
}

/**
 * Plays one note at each of the given places (quarter notes from a bar start), for many bars.
 * @return How often the note at each place came through.
 */
std::vector<int> timesPlayedPerPlace (ChainTestRig& rig, const std::vector<double>& placesInBar, int numBars)
{
    std::vector<int> timesPlayed (placesInBar.size(), 0);
    const double barLength = rig.context.timeSignature.barLengthInQuarters();
    for (int bar = 0; bar < numBars; ++bar)
    {
        for (size_t place = 0; place < placesInBar.size(); ++place)
        {
            rig.context.ppqPositionAtBlockStart = bar * barLength + placesInBar[place];
            const auto output = rig.processBlock ({ noteOnAt (0, 60) });
            rig.processBlock ({ noteOffAt (0, 60) });
            rig.processSeconds (0.05); // well apart, so notes don't count as a chord
            for (const auto& event : output)
                if (event.isNoteOn())
                    ++timesPlayed[place];
        }
    }
    return timesPlayed;
}

std::vector<int> noteOnNumbersOf (const MidiEventList& events)
{
    std::vector<int> notes;
    for (const auto& event : events)
        if (event.isNoteOn())
            notes.push_back (event.noteNumber());
    return notes;
}
} // namespace

TEST_CASE ("beat thinning: at 100 % the bar start always plays and the finest off-beats never do")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::BeatThinning, { 100, Grid::sixteenth });
    // Bar start, beat 3, beat 2, the "and" of 2, a sixteenth, a slightly late bar start.
    const auto played = timesPlayedPerPlace (rig, { 0.0, 2.0, 1.0, 1.5, 1.25, 0.02 }, 200);
    EXPECT (played[0] == 200 && played[5] == 200);
    EXPECT (played[4] == 0);
    EXPECT (played[1] > played[2] && played[2] > played[3] && played[3] > 0); // 75 %, 50 %, 25 %
    EXPECT (played[1] > 120 && played[1] < 180);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("beat thinning: amount 0 and a stopped transport let every note through")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::BeatThinning, { 0, Grid::sixteenth });
    EXPECT (timesPlayedPerPlace (rig, { 0.25, 0.75 }, 50) == (std::vector<int> { 50, 50 }));

    ChainTestRig stopped;
    stopped.loadModule (0, ModuleType::BeatThinning, { 100, Grid::sixteenth });
    EXPECT (timesPlayedPerPlace (stopped, { 0.25, 0.75 }, 50) == (std::vector<int> { 50, 50 }));
}

TEST_CASE ("beat thinning: in 7/8 the grouping decides which eighths are strong")
{
    // On a 1/8 grid in 7/8, at 100 %, a group start plays half the time and other eighths never.
    for (const auto& [grouping, strongEighth, weakEighth] :
         { std::tuple<float, double, double> { Grouping::threeLast, 1.0, 1.5 },    // 2+2+3: eighth 3 strong
           std::tuple<float, double, double> { Grouping::threeFirst, 1.5, 1.0 } }) // 3+2+2: eighth 4 strong
    {
        ChainTestRig rig;
        rig.context.isPlaying = rig.context.hasPpqPosition = true;
        rig.context.timeSignature = { 7, 8 };
        rig.loadModule (0, ModuleType::BeatThinning, { 100, Grid::eighth, grouping });
        const auto played = timesPlayedPerPlace (rig, { 0.0, strongEighth, weakEighth }, 200);
        EXPECT (played[0] == 200);
        EXPECT (played[1] > 60 && played[1] < 140);
        EXPECT (played[2] == 0);
    }
}

TEST_CASE ("beat thinning: a chord is kept or dropped as a whole")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::BeatThinning, { 50, Grid::sixteenth });
    int wholeChords = 0, droppedChords = 0, partialChords = 0;
    for (int bar = 0; bar < 100; ++bar)
    {
        rig.context.ppqPositionAtBlockStart = bar * 4.0 + 1.5;
        const auto output = rig.processBlock ({ noteOnAt (0, 60), noteOnAt (10, 64), noteOnAt (20, 67) });
        rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
        rig.processSeconds (0.05);
        const auto count = noteOnNumbersOf (output).size();
        (count == 3 ? wholeChords : count == 0 ? droppedChords : partialChords)++;
    }
    EXPECT (partialChords == 0 && wholeChords > 0 && droppedChords > 0);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("diatonic harmonizer: a third above stays in key, with the harmony's velocity share")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::DiatonicHarmonizer, { Voice::thirdUp, Voice::off, 0, 0, 50 }); // C major
    const std::vector<std::pair<int, int>> expected = {
        { 60, 64 }, // C -> E (major third)
        { 62, 65 }, // D -> F (minor third)
        { 71, 74 }, // B -> D
        { 61, 64 }, // C# (not in the scale) -> E
    };
    for (const auto& [played, harmony] : expected)
    {
        const auto output = rig.processBlock ({ noteOnAt (0, played, 100) });
        EXPECT (noteOnNumbersOf (output) == (std::vector<int> { played, harmony }));
        if (output.size() == 2)
            EXPECT (output[1].velocity() == 50);
        rig.processBlock ({ noteOffAt (0, played) });
        rig.processSeconds (0.05);
    }
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("diatonic harmonizer: two voices, other keys and octaves")
{
    ChainTestRig rig;
    // A minor: C -> E above and F below (a fifth down in A minor).
    rig.loadModule (0, ModuleType::DiatonicHarmonizer, { Voice::thirdUp, Voice::fifthDown, 9, 1, 80 });
    auto output = rig.processBlock ({ noteOnAt (0, 60) });
    EXPECT (noteOnNumbersOf (output) == (std::vector<int> { 60, 64, 53 }));
    rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (rig.notesStillSounding().empty());

    ChainTestRig octaves;
    octaves.loadModule (0, ModuleType::DiatonicHarmonizer, { Voice::octaveUp, Voice::octaveDown, 0, 9 }); // pent.
    output = octaves.processBlock ({ noteOnAt (0, 61) });
    EXPECT (noteOnNumbersOf (output) == (std::vector<int> { 61, 73, 49 })); // octaves ignore the scale
    octaves.processSeconds (0.1);                                           // well after, so not a chord
    octaves.processBlock ({ noteOnAt (0, 125) });                           // octave up is out of range
    EXPECT (noteOnNumbersOf (octaves.everyOutputEvent).size() == 5);
    octaves.processBlock ({ noteOffAt (0, 61), noteOffAt (0, 125) });
    EXPECT (octaves.notesStillSounding().empty());

    // Both voices off: the note passes alone.
    ChainTestRig silent;
    silent.loadModule (0, ModuleType::DiatonicHarmonizer, { Voice::off, Voice::off });
    EXPECT (noteOnNumbersOf (silent.processBlock ({ noteOnAt (0, 60) })) == (std::vector<int> { 60 }));
}

TEST_CASE ("diatonic harmonizer: chords pass; the harmony of a chord's first note is ended")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::DiatonicHarmonizer, { Voice::fifthUp, Voice::off, 0, 0 });
    const auto chord = rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64), noteOnAt (0, 67) });
    // C plays with its fifth G; then E arrives (a chord): the harmony G ends, E and G play as played.
    EXPECT (noteOnNumbersOf (chord) == (std::vector<int> { 60, 67, 64, 67 }));
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    EXPECT (rig.notesStillSounding().empty());

    // A melody note after the chord gets its harmony again.
    rig.processSeconds (0.1);
    EXPECT (noteOnNumbersOf (rig.processBlock ({ noteOnAt (0, 62) })) == (std::vector<int> { 62, 69 }));
    rig.processBlock ({ noteOffAt (0, 62) });
    EXPECT (rig.notesStillSounding().empty());
}
