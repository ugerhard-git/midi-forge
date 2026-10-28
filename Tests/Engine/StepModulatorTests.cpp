/**
 * @file StepModulatorTests.cpp
 * @brief The step modulator: step values in time with the host and on its own clock, uneven step counts,
 *        glide, restart on key, and steps driving a module through a route.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>

using namespace testing;

namespace
{
constexpr int sixteenthIndex = defaultStepDivisionIndex; // "1/16"

/** Settings with the given values (-1..+1); the number of steps is the number of values. */
StepModulatorSettings stepsWithValues (std::initializer_list<float> values, int divisionIndex = sixteenthIndex)
{
    StepModulatorSettings settings;
    settings.numSteps = (int) values.size();
    int step = 0;
    for (float value : values)
        settings.stepValues[step++] = value;
    settings.divisionIndex = divisionIndex;
    return settings;
}

BlockContext hostContextAtBeat (double beat)
{
    BlockContext context;
    context.sampleRate = testSampleRate;
    context.tempoBpm = 120.0;
    context.isPlaying = context.hasPpqPosition = true;
    context.ppqPositionAtBlockStart = beat;
    context.numSamples = 32;
    return context;
}
} // namespace

TEST_CASE ("step modulator: follows the host grid, one step per division, and wraps after the last step")
{
    StepModulator steps;
    const auto settings = stepsWithValues ({ 0.5f, -1.0f, 0.25f, 1.0f });
    const float expected[] = { 0.5f, -1.0f, 0.25f, 1.0f, 0.5f, -1.0f };
    for (int sixteenth = 0; sixteenth < 6; ++sixteenth)
    {
        const auto context = hostContextAtBeat (sixteenth * 0.25 + 0.1);
        EXPECT (std::abs (steps.valueAt (settings, context) - expected[sixteenth]) < 1.0e-6f);
        EXPECT (steps.currentStepAt (settings, context) == sixteenth % 4);
    }
}

TEST_CASE ("step modulator: an uneven number of steps drifts against the bar")
{
    StepModulator steps;
    auto settings = stepsWithValues ({ 1, 0, 0, 0, 0, 0, 0 }); // 7 steps, the first one high
    // Step 1 comes back every 7 sixteenths: at beats 0, 1.75, 3.5, 5.25 ...
    for (int sixteenth = 0; sixteenth < 28; ++sixteenth)
    {
        const float value = steps.valueAt (settings, hostContextAtBeat (sixteenth * 0.25 + 0.01));
        EXPECT ((value > 0.5f) == (sixteenth % 7 == 0));
    }
}

TEST_CASE ("step modulator: glide slides to the next value at the end of each step")
{
    auto settings = stepsWithValues ({ -1.0f, 1.0f, -1.0f, 1.0f });
    settings.glide = 0.5;
    EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, 0.25) - -1.0f) < 1.0e-6f); // before the glide
    EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, 0.75) - 0.0f) < 1.0e-6f);  // halfway
    EXPECT (StepModulator::valueAtStepPosition (settings, 0.99) > 0.99f);                      // almost there
    EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, 3.75) - 0.0f) < 1.0e-6f);  // last -> first
}

TEST_CASE ("step modulator: runs on its own clock while stopped, and restarts on key")
{
    StepModulator steps;
    const auto settings = stepsWithValues ({ 1, 2, 3, 4 });
    BlockContext stopped;
    stopped.sampleRate = testSampleRate;
    stopped.tempoBpm = 120.0;
    stopped.numSamples = 3000; // half a sixteenth at 120 bpm

    for (int halfStep = 0; halfStep < 5; ++halfStep)
        steps.advance (settings, stopped);
    EXPECT (steps.currentStepAt (settings, stopped) == 2); // 2.5 steps in
    steps.restart (stopped);
    EXPECT (steps.currentStepAt (settings, stopped) == 0);
}

TEST_CASE ("step modulator: steps drive a module through a route, locked to the host's sixteenths")
{
    ChainTestRig rig;
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
    // Steps 1: +12, 0, -12, 0 semitones (Semitones spans 48 semitones, so +-0.25 at 100 % is +-12).
    rig.settings.stepModulators[0] = stepsWithValues ({ 0.25f, 0.0f, -0.25f, 0.0f });
    rig.routeLfo (0, firstStepModulatorSource, 0, 0, 100);

    // One short note on every sixteenth (6000 samples), started a little after the step begins.
    std::vector<int> expectedNotes;
    for (int sixteenth = 0; sixteenth < 8; ++sixteenth)
    {
        rig.processBlock ({ noteOnAt (10, 60) });
        rig.processBlock ({ noteOffAt (10, 60) });
        const int samplesLeft = 6000 - 2 * testBlockSize;
        for (int block = 0; block < samplesLeft / testBlockSize; ++block)
            rig.processBlock();
        rig.context.ppqPositionAtBlockStart = (sixteenth + 1) * 0.25; // keep exactly on the grid
        const int offsets[] = { 12, 0, -12, 0 };
        expectedNotes.push_back (60 + offsets[sixteenth % 4]);
    }
    EXPECT (rig.playedNoteNumbers() == expectedNotes);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("step modulator: a tied step continues the step before it, and glide waits for the end of the group")
{
    // 8 steps, steps 2 and 6 tied: long-short-short-long-short-short ... in values A A B C D D E F.
    auto settings = stepsWithValues ({ 0.5f, -1.0f, 0.25f, -0.25f, 1.0f, -1.0f, 0.75f, -0.75f });
    settings.isTied[1] = true;
    settings.isTied[5] = true;
    settings.isTied[0] = true; // step 1 cannot be tied: ignored
    const float expected[] = { 0.5f, 0.5f, 0.25f, -0.25f, 1.0f, 1.0f, 0.75f, -0.75f };
    for (int step = 0; step < 8; ++step)
        EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, step + 0.5) - expected[step]) < 1.0e-6f);

    // Glide 50 % over a two-step group: still on the value after one and a half steps, halfway at 1.5 of 2 ...
    settings.glide = 0.5;
    EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, 0.9) - 0.5f) < 1.0e-6f);
    EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, 1.5) - 0.375f) < 1.0e-6f); // halfway to 0.25
    EXPECT (std::abs (StepModulator::valueAtStepPosition (settings, 1.999) - 0.25f) < 1.0e-3f);
    EXPECT (settings.groupStartOf (5) == 4 && settings.groupEndOf (4, 8) == 6 && settings.groupEndOf (7, 8) == 8);
}
