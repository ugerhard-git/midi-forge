/**
 * @file ModulatingModulationTests.cpp
 * @brief Routes whose target is the modulation itself: route amounts, LFO rate / phase / width, step rate and
 *        glide; smooth rate changes off the bar grid; stable feedback loops; twelve routes.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>

using namespace testing;

namespace
{
constexpr int stepsAlwaysUp = 0; ///< Steps 1 is set up as a constant +1 source
constexpr int stepsAlwaysUp2 = 1;

/** Makes a step modulator a constant source (every step +100 %). */
void makeConstantSource (ChainTestRig& rig, int stepIndex)
{
    auto& steps = rig.settings.stepModulators[stepIndex];
    steps.numSteps = 4;
    for (auto& value : steps.stepValues)
        value = 1.0f;
}

/** Connects a route to any target number. */
void connect (ChainTestRig& rig, int routeIndex, int sourceIndex, int targetIndex, float amountPercent,
              bool isUnipolar = false)
{
    auto& route = rig.settings.routes[routeIndex];
    route.sourceIndex = sourceIndex;
    route.targetIndex = targetIndex;
    route.amount = amountPercent / 100.0f;
    route.isUnipolar = isUnipolar;
}

void startTransport (ChainTestRig& rig, double beat = 0.0)
{
    rig.context.isPlaying = rig.context.hasPpqPosition = true;
    rig.context.ppqPositionAtBlockStart = beat;
}
} // namespace

TEST_CASE ("modulating modulation: a route moves another route's amount, even one set to 0 %")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose); // Semitones at 0 = the middle of the knob
    makeConstantSource (rig, stepsAlwaysUp);
    makeConstantSource (rig, stepsAlwaysUp2);
    connect (rig, 0, firstStepModulatorSource + stepsAlwaysUp, modulationTargetIndex (0, 0), 0);
    connect (rig, 1, firstStepModulatorSource + stepsAlwaysUp2, routeAmountTarget (0), 25);
    rig.processSeconds (0.1);

    const auto& modulated = rig.engine.getLastModulatedModulators();
    EXPECT (std::abs (modulated.routeAmounts[0] - 0.25f) < 1.0e-4f);
    EXPECT (std::abs (rig.engine.getLastModulatedSlots()[0].parameterValues[0] - 0.75f) < 1.0e-4f);

    // Amounts stay within -100 … +100 %.
    rig.settings.routes[0].amount = 0.9f;
    rig.settings.routes[1].amount = 0.5f;
    rig.processSeconds (0.1);
    EXPECT (std::abs (rig.engine.getLastModulatedModulators().routeAmounts[0] - 1.0f) < 1.0e-6f);
}

TEST_CASE ("modulating modulation: LFO phase wraps around, width and glide are clamped")
{
    ChainTestRig rig;
    makeConstantSource (rig, stepsAlwaysUp);
    rig.lfos[0].phaseDegrees = 350.0;
    rig.lfos[1].pulseWidth = 0.5;
    rig.settings.stepModulators[2].glide = 0.9;
    const int source = firstStepModulatorSource + stepsAlwaysUp;
    connect (rig, 0, source, lfoSettingTarget (0, LfoSettingTarget::phase), 10); // +36°
    connect (rig, 1, source, lfoSettingTarget (1, LfoSettingTarget::width), 25); // a quarter of the range
    connect (rig, 2, source, stepSettingTarget (2, StepSettingTarget::glide), 30);
    rig.processSeconds (0.1);

    const auto& modulated = rig.engine.getLastModulatedModulators();
    EXPECT (std::abs (modulated.lfos[0].phaseDegrees - 26.0) < 1.0e-3);
    EXPECT (std::abs (modulated.lfos[1].pulseWidth - 0.725) < 1.0e-6);
    EXPECT (std::abs (modulated.stepModulators[2].glide - 1.0) < 1.0e-9);
    // The user's settings are untouched.
    EXPECT (rig.lfos[0].phaseDegrees == 350.0);
}

TEST_CASE ("modulating modulation: rates move the note value or the Hz knob")
{
    LfoSettings free;
    free.isTempoSynced = false;
    free.rateHz = 1.0;
    EXPECT (std::abs (normalisedLfoRate (free) - 0.5f) < 1.0e-4f); // 1 Hz is the middle of the knob
    setLfoRateFromNormalised (free, 1.0f);
    EXPECT (std::abs (free.rateHz - 20.0) < 1.0e-6);
    setLfoRateFromNormalised (free, 0.0f);
    EXPECT (std::abs (free.rateHz - 0.01) < 1.0e-9);

    ChainTestRig rig;
    makeConstantSource (rig, stepsAlwaysUp);
    rig.settings.stepModulators[1].divisionIndex = 2;
    connect (rig, 0, firstStepModulatorSource + stepsAlwaysUp, lfoSettingTarget (0, LfoSettingTarget::rate), 20);
    connect (rig, 1, firstStepModulatorSource + stepsAlwaysUp, stepSettingTarget (1, StepSettingTarget::rate), -50);
    rig.processSeconds (0.1);
    const auto& modulated = rig.engine.getLastModulatedModulators();
    EXPECT (modulated.lfos[0].divisionIndex == defaultLfoDivisionIndex + 3); // 20 % of 15 steps, rounded
    EXPECT (modulated.stepModulators[1].divisionIndex == 0);                 // clamped at the first value
    EXPECT (modulated.isLfoRateModulated[0] && modulated.isStepRateModulated[1]);
}

TEST_CASE ("modulating modulation: a modulated rate changes speed smoothly and locks to the bar on restart")
{
    ChainTestRig rig;
    startTransport (rig);
    rig.lfos[0].waveform = LfoWaveform::SawUp;
    rig.lfos[0].divisionIndex = defaultLfoDivisionIndex; // 1 bar
    makeConstantSource (rig, stepsAlwaysUp);
    connect (rig, 0, 0, modulationTargetIndex (0, 0), 1); // keeps the modulation running
    rig.processSeconds (1.0);                             // 2 quarter notes: half way through the bar
    const double phaseOnGrid = rig.engine.getLastLfoPhases()[0];
    EXPECT (std::abs (phaseOnGrid - 0.5) < 0.02);

    // Now the rate is modulated (faster note value): no jump, it continues from where it was.
    connect (rig, 1, firstStepModulatorSource + stepsAlwaysUp, lfoSettingTarget (0, LfoSettingTarget::rate), 20);
    rig.processBlock();
    const double phaseAfterChange = rig.engine.getLastLfoPhases()[0];
    EXPECT (phaseAfterChange > phaseOnGrid && phaseAfterChange - phaseOnGrid < 0.05);

    // It runs faster than the bar grid from here on (1 bar → 1/4D: under half a bar per 1.5 quarters).
    double previousPhase = phaseAfterChange;
    double cycles = 0.0;
    for (int block = 0; block < 40; ++block)
    {
        rig.processBlock();
        const double phase = rig.engine.getLastLfoPhases()[0];
        const double step = phase >= previousPhase ? phase - previousPhase : phase + 1.0 - previousPhase;
        EXPECT (step < 0.1); // never a jump
        cycles += step;
        previousPhase = phase;
    }
    const double barsOnGrid = 40.0 * testBlockSize / (rig.context.samplesPerBeat() * 4.0);
    EXPECT (cycles > barsOnGrid * 1.5);

    // Stopping and starting the transport locks it onto the bar grid again.
    rig.settings.routes[1] = {};
    rig.context.isPlaying = false;
    rig.processBlock();
    startTransport (rig, 0.0);
    rig.processBlock();
    EXPECT (! rig.engine.getLastModulatedModulators().isLfoRateModulated[0]);
    EXPECT (rig.engine.getLastLfoPhases()[0] < 0.02);
}

TEST_CASE ("modulating modulation: feedback loops stay stable and in range")
{
    ChainTestRig rig;
    startTransport (rig);
    rig.loadModule (0, ModuleType::Transpose);
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 3.0;
    rig.lfos[1].waveform = LfoWaveform::Square;
    // LFO 1 → LFO 2 rate, LFO 2 → LFO 1 rate, LFO 1 → its own rate and phase, route amounts in a circle.
    connect (rig, 0, 0, lfoSettingTarget (1, LfoSettingTarget::rate), 80);
    connect (rig, 1, 1, lfoSettingTarget (0, LfoSettingTarget::rate), 80);
    connect (rig, 2, 0, lfoSettingTarget (0, LfoSettingTarget::rate), 50);
    connect (rig, 3, 1, lfoSettingTarget (0, LfoSettingTarget::phase), 100);
    connect (rig, 4, 0, routeAmountTarget (5), 100);
    connect (rig, 5, 1, routeAmountTarget (4), 100);
    connect (rig, 11, 0, modulationTargetIndex (0, 0), 50); // route 12 exists and works
    for (int block = 0; block < 2000; ++block)
    {
        rig.processBlock ({ noteOnAt (0, 60) });
        rig.processBlock ({ noteOffAt (0, 60) });
        const auto& modulated = rig.engine.getLastModulatedModulators();
        for (int lfo = 0; lfo < numLfos; ++lfo)
        {
            EXPECT (std::isfinite (modulated.lfos[lfo].rateHz) && modulated.lfos[lfo].rateHz >= 0.01 &&
                    modulated.lfos[lfo].rateHz <= 20.0);
            EXPECT (modulated.lfos[lfo].divisionIndex >= 0 && modulated.lfos[lfo].divisionIndex < numLfoDivisions);
            EXPECT (modulated.lfos[lfo].phaseDegrees >= 0.0 && modulated.lfos[lfo].phaseDegrees < 360.0);
            EXPECT (std::abs (rig.engine.getLastLfoValues()[lfo]) <= 1.0f);
        }
        for (float amount : modulated.routeAmounts)
            EXPECT (amount >= -1.0f && amount <= 1.0f);
        if (rig.notesStillSounding().size() > 1)
            break;
    }
    EXPECT (rig.notesStillSounding().empty());
}
