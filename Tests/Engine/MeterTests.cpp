/**
 * @file MeterTests.cpp
 * @brief Time signatures: the bar structure (groups, accents, backbeats), bar positions from the host, and
 *        the modules that follow the meter (Groove, Ghost Notes, Mutate, bar-long divisions).
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

using namespace testing;

namespace
{
constexpr int samplesPerQuarter = 24000; // 120 bpm at 48 kHz
constexpr int sixteenthInSamples = 6000;

/** @return The counts (0-based) that are backbeats in a bar. */
std::vector<int> backbeatsOf (const BarStructure& bar)
{
    std::vector<int> counts;
    for (int count = 0; count < bar.getTimeSignature().numerator; ++count)
        if (bar.isBackbeat (count))
            counts.push_back (count);
    return counts;
}

/** @return The counts (0-based) that start a group. */
std::vector<int> groupStartsOf (const BarStructure& bar)
{
    std::vector<int> counts;
    for (int count = 0; count < bar.getTimeSignature().numerator; ++count)
        if (bar.isGroupStart (count))
            counts.push_back (count);
    return counts;
}

/** Plays a short note at an absolute sample time; earlier blocks are run empty. */
void playNoteAt (ChainTestRig& rig, int time, int noteNumber, int velocity = 100)
{
    while (rig.samplesElapsed + testBlockSize <= time)
        rig.processBlock();
    rig.processBlock ({ noteOnAt (time - (int) rig.samplesElapsed, noteNumber, velocity) });
    rig.processBlock ({ noteOffAt (0, noteNumber) });
}

std::vector<int> noteOnTimesOf (const ChainTestRig& rig)
{
    std::vector<int> times;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            times.push_back (event.samplePosition);
    return times;
}
} // namespace

TEST_CASE ("meter: odd host time signatures fall back to 4/4; bar lengths in quarter notes")
{
    EXPECT (TimeSignature::fromHost (7, 8) == (TimeSignature { 7, 8 }));
    EXPECT (TimeSignature::fromHost (0, 4) == TimeSignature {});
    EXPECT (TimeSignature::fromHost (4, 3) == TimeSignature {});
    EXPECT (TimeSignature::fromHost (65, 4) == TimeSignature {});
    EXPECT ((TimeSignature { 7, 8 }).barLengthInQuarters() == 3.5);
    EXPECT ((TimeSignature { 6, 8 }).barLengthInQuarters() == 3.0);
    EXPECT ((TimeSignature { 3, 4 }).barLengthInQuarters() == 3.0);
}

TEST_CASE ("meter: groups and backbeats of common and odd meters")
{
    const BarStructure fourFour (TimeSignature { 4, 4 });
    EXPECT (groupStartsOf (fourFour) == (std::vector<int> { 0, 2 }));
    EXPECT (backbeatsOf (fourFour) == (std::vector<int> { 1, 3 })); // beats 2 and 4

    const BarStructure threeFour (TimeSignature { 3, 4 });
    EXPECT (backbeatsOf (threeFour) == (std::vector<int> { 1, 2 })); // waltz: 2 and 3

    const BarStructure sixEight (TimeSignature { 6, 8 });
    EXPECT (groupStartsOf (sixEight) == (std::vector<int> { 0, 3 }));
    EXPECT (backbeatsOf (sixEight) == (std::vector<int> { 3 })); // the second dotted quarter

    const BarStructure twelveEight (TimeSignature { 12, 8 });
    EXPECT (backbeatsOf (twelveEight) == (std::vector<int> { 3, 9 }));

    EXPECT (groupStartsOf (BarStructure (TimeSignature { 7, 8 }, OddMeterGrouping::longGroupLast)) ==
            (std::vector<int> { 0, 2, 4 })); // 2+2+3
    EXPECT (groupStartsOf (BarStructure (TimeSignature { 7, 8 }, OddMeterGrouping::longGroupFirst)) ==
            (std::vector<int> { 0, 3, 5 })); // 3+2+2
    EXPECT (groupStartsOf (BarStructure (TimeSignature { 7, 8 }, OddMeterGrouping::longGroupMiddle)) ==
            (std::vector<int> { 0, 2, 5 })); // 2+3+2
    EXPECT (backbeatsOf (BarStructure (TimeSignature { 7, 8 })) == (std::vector<int> { 2, 4 }));
    EXPECT (backbeatsOf (BarStructure (TimeSignature { 5, 4 })) == (std::vector<int> { 2 })); // 2+3
}

TEST_CASE ("meter: accent levels from the bar start down to the finest off-beats")
{
    const BarStructure fourFour (TimeSignature { 4, 4 });
    EXPECT (fourFour.accentLevelAt (0.0) == BarStructure::barStart);
    EXPECT (fourFour.accentLevelAt (2.0) == BarStructure::groupStart);
    EXPECT (fourFour.accentLevelAt (1.0) == BarStructure::countStart);
    EXPECT (fourFour.accentLevelAt (1.5) == BarStructure::halfCount);
    EXPECT (fourFour.accentLevelAt (1.25) == BarStructure::quarterCount);
    EXPECT (fourFour.accentLevelAt (1.125) == BarStructure::finer);

    const BarStructure sevenEight (TimeSignature { 7, 8 });
    EXPECT (sevenEight.accentLevelAt (1.0) == BarStructure::groupStart); // eighth 3 of 2+2+3
    EXPECT (sevenEight.accentLevelAt (0.5) == BarStructure::countStart);
    EXPECT (sevenEight.accentLevelAt (0.25) == BarStructure::halfCount);
}

TEST_CASE ("meter: bar positions follow the host's bar start and time signature")
{
    BlockContext context;
    context.timeSignature = { 7, 8 };
    auto position = context.barPositionAtBeat (8.0); // bars of 3.5 quarters from the song start
    EXPECT (position.barNumber == 2 && std::abs (position.positionInBar - 1.0) < 1.0e-9);

    // A host that changed the meter: the current bar started at quarter 10.
    context.timeSignature = { 3, 4 };
    context.hasBarStart = true;
    context.ppqPositionOfBarStart = 10.0;
    position = context.barPositionAtBeat (14.5); // the next bar started at 13
    EXPECT (std::abs (position.positionInBar - 1.5) < 1.0e-9 && position.barLength == 3.0);
    EXPECT (std::abs (context.barsAtBeat (13.0) - context.barsAtBeat (10.0) - 1.0) < 1.0e-9);
}

TEST_CASE ("meter: bar-long divisions (echo, LFO, steps) last one real bar")
{
    BlockContext context;
    context.timeSignature = { 3, 4 };
    const NoteDivision oneBar { "1 Bar", 4.0, 1 };
    const NoteDivision quarter { "1/4", 1.0 };
    EXPECT (divisionLengthInBeats (oneBar, context.timeSignature) == 3.0);
    EXPECT (divisionLengthInBeats (quarter, context.timeSignature) == 1.0);
    EXPECT (std::abs (divisionsBetween (oneBar, context, 0.0, 6.0) - 2.0) < 1.0e-9);

    // A Note Echo of 1 bar in 3/4 repeats three quarter notes later.
    ChainTestRig rig;
    rig.context.timeSignature = { 3, 4 };
    rig.loadModule (0, ModuleType::NoteEcho, { 11, 1, 50 }); // 1 Bar, one repeat
    playNoteAt (rig, 0, 60);
    rig.processSeconds (2.0);
    const auto times = noteOnTimesOf (rig);
    EXPECT (times.size() == 2 && times.size() == 2 && times.back() == 3 * samplesPerQuarter);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("meter: Groove's laid-back snare drags the backbeats of 3/4 and 6/8")
{
    for (const auto& [meter, expectedLate] :
         { std::pair<TimeSignature, std::vector<bool>> { { 3, 4 }, { false, true, true, false } },
           std::pair<TimeSignature, std::vector<bool>> { { 6, 8 }, { false, false, false, true } } })
    {
        ChainTestRig rig;
        rig.context.isPlaying = rig.context.hasPpqPosition = true;
        rig.context.timeSignature = meter;
        rig.loadModule (0, ModuleType::Groove, { 1, 100, 1, 0 }); // laid-back snare, 1/16 grid
        // 3/4: beats 1, 2, 3 and the next bar's 1. 6/8: eighths 1, 2, 3, 4.
        const int spacing = meter.denominator == 4 ? samplesPerQuarter : samplesPerQuarter / 2;
        for (int hit = 0; hit < 4; ++hit)
            playNoteAt (rig, hit * spacing, 38);
        rig.processSeconds (0.5);
        const auto times = noteOnTimesOf (rig);
        EXPECT (times.size() == 4);
        for (size_t hit = 0; hit < times.size() && hit < 4; ++hit)
            EXPECT ((times[hit] > (int) hit * spacing) == expectedLate[hit]);
    }
}

TEST_CASE ("meter: Ghost Notes' before uses the real bar length")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.context.timeSignature = { 3, 4 };
    rig.loadModule (0, ModuleType::GhostNotes, { 100, 1, 2, 1, 30 }); // before, 1/16, one ghost
    playNoteAt (rig, samplesPerQuarter, 38);
    rig.processSeconds (2.0);
    const auto times = noteOnTimesOf (rig);
    EXPECT (times.size() == 2 && times.back() == 4 * samplesPerQuarter - sixteenthInSamples); // bar 2 starts at 3
}

TEST_CASE ("meter: Mutate remembers places per real bar, and forgets them when the meter changes")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.context.timeSignature = { 3, 4 };
    rig.loadModule (0, ModuleType::Mutate, { 100, 1, 0, 0, 0, 0, 2 }); // up one step each time, 1 Bar memory

    const auto playAtBeat = [&rig] (double beat)
    {
        rig.processSeconds (0.05);
        rig.context.ppqPositionAtBlockStart = beat;
        const auto output = rig.processBlock ({ noteOnAt (0, 60) });
        rig.processBlock ({ noteOffAt (0, 60) });
        return output.empty() ? -1 : output[0].noteNumber();
    };
    EXPECT (playAtBeat (0.0) == 62);
    EXPECT (playAtBeat (3.0) == 64); // bar 2, beat 1 of 3/4: the same place
    EXPECT (playAtBeat (4.0) == 62); // bar 2, beat 2: its own place
    rig.context.timeSignature = { 4, 4 };
    EXPECT (playAtBeat (8.0) == 62); // new meter: memory cleared
}
