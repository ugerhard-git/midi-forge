/**
 * @file FactoryPresets.cpp
 * @brief Preset definitions. Choice values are indices into the label lists in ModuleDescriptions.cpp
 *        and Lfo.cpp.
 */
#include "FactoryPresets.h"

#include <cmath>

namespace midiforge
{

const float keepDefault = NAN;

namespace
{
/** A tempo-synced LFO. */
LfoSettings syncedLfo (LfoWaveform waveform, int divisionIndex)
{
    LfoSettings settings;
    settings.waveform = waveform;
    settings.isTempoSynced = true;
    settings.divisionIndex = divisionIndex;
    return settings;
}
/** A Note Random modulator. */
NoteRandomSettings noteRandom (double chordWindowMilliseconds, NoteRandomTrigger trigger, int numSteps)
{
    NoteRandomSettings settings;
    settings.chordWindowMilliseconds = chordWindowMilliseconds;
    settings.trigger = trigger;
    settings.numSteps = numSteps;
    return settings;
}
/** A step modulator with the given step values (-100..+100); the number of steps is the number of values. */
StepModulatorSettings steps (int divisionIndex, std::initializer_list<float> valuesPercent)
{
    StepModulatorSettings settings;
    settings.divisionIndex = divisionIndex;
    settings.numSteps = (int) valuesPercent.size();
    int step = 0;
    for (float value : valuesPercent)
        settings.stepValues[step++] = value / 100.0f;
    return settings;
}

/** The same with some steps (1-based numbers) tied to the step before. */
StepModulatorSettings stepsWithTies (int divisionIndex, std::initializer_list<float> valuesPercent,
                                     std::initializer_list<int> tiedStepNumbers)
{
    auto settings = steps (divisionIndex, valuesPercent);
    for (int stepNumber : tiedStepNumbers)
        if (stepNumber >= 2 && stepNumber <= maximumSteps)
            settings.isTied[stepNumber - 1] = true;
    return settings;
}
} // namespace

const std::vector<FactoryPreset>& getFactoryPresets()
{
    // Choice indices, named so the presets below read like the manual
    constexpr float major7Chord = 6, minor7Chord = 7, upDownPattern = 2, sixteenthRate = 3;
    constexpr float rootC = 0, rootD = 2, minorScale = 1, dorianScale = 2, minorPentatonicScale = 10;
    constexpr float dottedEighth = 6, quarterTriplet = 7, octavesMode = 1;
    constexpr int oneBar = 3, twoBars = 2;
    const float keep = keepDefault;

    // Parameter positions (A..D) used by the routes
    constexpr int chordInversion = 1, arpeggiatorGate = 3, echoPitch = 3, repeaterRate = 0;
    constexpr float repeaterSixteenth = 7, repeaterLastNote = 0, repeaterOnTheBeat = 4;
    constexpr int halfBar = 4;
    constexpr float closedHiHat = 42, thisOctave = 0; // General MIDI closed hi-hat: F#1
    constexpr int random1 = firstNoteRandomSource, random2 = firstNoteRandomSource + 1, velocityOutMaximum = 2;
    constexpr int steps1 = firstStepModulatorSource, steps2 = firstStepModulatorSource + 1,
                  steps3 = firstStepModulatorSource + 2;
    constexpr int sixteenthStep = 2, barStep = 12; // indices into stepDivisions
    constexpr float echoEighth = 5;

    static const std::vector<FactoryPreset> presets = {
        { "Init", {}, {}, {}, {} },

        { "Chord Arp",
          { { ModuleType::Chord, { minor7Chord, 0, 0, 90 } },
            { ModuleType::Arpeggiator, { sixteenthRate, upDownPattern, 2, 60 } },
            { ModuleType::Humanize, { 4, 8, keep, keep } } },
          {},
          {},
          {} },

        { "Scale Lock (C Minor)",
          { { ModuleType::ScaleQuantize, { rootC, minorScale, 0, keep } },
            { ModuleType::VelocityCurve, { -30, 20, 120, 0 } } },
          {},
          {},
          {} },

        { "Echo Cascade",
          { { ModuleType::NoteEcho, { dottedEighth, 6, 65, 7 } },
            { ModuleType::ScaleQuantize, { rootC, minorPentatonicScale, 0, keep } } },
          {},
          {},
          {} },

        { "Generative Dorian",
          { { ModuleType::Probability, { 60, keep, keep, keep } },
            { ModuleType::RandomPitch, { 12, 40, octavesMode, keep } },
            { ModuleType::ScaleQuantize, { rootD, dorianScale, 0, keep } },
            { ModuleType::NoteEcho, { quarterTriplet, 3, 70, 0 } },
            { ModuleType::Humanize, { 8, 12, keep, keep } } },
          {},
          {},
          {} },

        { "Bass Split to Ch 2",
          { { ModuleType::NoteRange, { 0, 59, 0, keep } },
            { ModuleType::ChannelRemap, { 0, 2, keep, keep } },
            { ModuleType::VelocityCurve, { 0, 1, 127, 100 } } },
          {},
          {},
          {} },

        // LFO presets ----------------------------------------------------------------------------
        { "Breathing Arp",
          { { ModuleType::Chord, { major7Chord, 0, 0, 85 } },
            { ModuleType::Arpeggiator, { sixteenthRate, upDownPattern, 2, 50 } } },
          { { 0, syncedLfo (LfoWaveform::Triangle, twoBars) }, { 1, syncedLfo (LfoWaveform::SampleAndHold, oneBar) } },
          { { 0, 1, arpeggiatorGate, 45.0f },  // gate swells over two bars
            { 1, 0, chordInversion, 40.0f } }, // a new inversion every bar
          {} },

        { "Wandering Echo",
          { { ModuleType::NoteEcho, { dottedEighth, 5, 70, 0 } },
            { ModuleType::ScaleQuantize, { rootC, minorPentatonicScale, 0, keep } } },
          { { 0, syncedLfo (LfoWaveform::SmoothRandom, oneBar) } },
          { { 0, 0, echoPitch, 25.0f } }, // echo pitch step drifts up and down
          {} },

        // Added in 0.4.0 -------------------------------------------------------------------------
        { "Ratchet Chords",
          { { ModuleType::Chord, { minor7Chord, 0, 1, 80 } },
            { ModuleType::Repeater, { repeaterSixteenth, repeaterLastNote, 58, repeaterOnTheBeat } } },
          { { 0, syncedLfo (LfoWaveform::SampleAndHold, halfBar) } },
          { { 0, 1, repeaterRate, 12.0f } }, // the ratchet speed jumps every half bar
          {} },

        // Added in 0.5.0 -------------------------------------------------------------------------
        { "Hi-Hat Ratchet (Drums)",
          { { ModuleType::NoteSplit, { closedHiHat, closedHiHat, thisOctave, keep } }, // F#1: closed hi-hat
            { ModuleType::Repeater, { repeaterSixteenth, repeaterLastNote, 56, repeaterOnTheBeat } },
            { ModuleType::Humanize, { 3, 10, keep, keep } } },
          {},
          {},
          {} }, // kick, snare and everything else pass untouched

        // Added in 0.6.0 -------------------------------------------------------------------------
        { "Random Voicings",
          { { ModuleType::Chord, { major7Chord, 0, 0, 85 } },
            { ModuleType::VelocityCurve, { 0, 40, 100, 0 } },
            { ModuleType::Humanize, { 6, 6, keep, keep } } },
          {},
          { { random1, 0, chordInversion, 100.0f, true },       // every chord gets one of four inversions
            { random2, 1, velocityOutMaximum, 30.0f, false } }, // and its own loudness
          { { 0, noteRandom (30.0, NoteRandomTrigger::EveryNote, 4) },
            { 1, noteRandom (30.0, NoteRandomTrigger::EveryNote, 0) } } },

        // Added in 0.14.0 ------------------------------------------------------------------------
        { "Breathing Steps",
          { { ModuleType::Arpeggiator, { sixteenthRate, upDownPattern, 2, 55 } },
            { ModuleType::VelocityCurve, { 0, 40, 105, 0 } },
            { ModuleType::NoteEcho, { echoEighth, 3, 60, 0 } } },
          {},
          { { steps1, 0, arpeggiatorGate, 40.0f },             // the gate breathes over 16 sixteenths
            { steps2, 1, velocityOutMaximum, 25.0f },          // accents in a 7-step cycle against the bar
            { steps3, 2, slotSwitchParameterIndex, 100.0f } }, // the echo only in every 4th bar
          {},
          { { 0,
              stepsWithTies (sixteenthStep, { 60, -20, 20, -40, 80, -20, 0, -60, 60, -20, 30, -30, 90, -10, -50, 100 },
                             { 2, 6, 10 }) }, // long-short-short: steps 2, 6 and 10 are tied
            { 1, steps (sixteenthStep, { 100, -40, 20, -80, 50, 0, -30 }) },
            { 2, steps (barStep, { -100, -100, -100, 100 }) } } },
    };
    return presets;
}

} // namespace midiforge
