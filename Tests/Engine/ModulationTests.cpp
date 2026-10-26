/**
 * @file ModulationTests.cpp
 * @brief LFO waveforms and timing, modulation routes, and LFO-driven modules in the engine.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace testing;

namespace
{
constexpr float tolerance = 1.0e-4f;

bool isClose (double actual, double expected, double allowedDifference = tolerance)
{
    return std::abs (actual - expected) <= allowedDifference;
}

/** A context of the given length; transport stopped unless a host position is given. */
BlockContext makeContext (int numSamples, double tempoBpm = 120.0)
{
    BlockContext context;
    context.sampleRate = testSampleRate;
    context.tempoBpm = tempoBpm;
    context.numSamples = numSamples;
    return context;
}

BlockContext makeHostContext (double ppqPosition, int numSamples = 0, double tempoBpm = 120.0)
{
    BlockContext context = makeContext (numSamples, tempoBpm);
    context.isPlaying = true;
    context.hasPpqPosition = true;
    context.ppqPositionAtBlockStart = ppqPosition;
    return context;
}

int indexOfDivision (const char* name)
{
    for (int index = 0; index < numLfoDivisions; ++index)
        if (std::string (lfoDivisions[index].name) == name)
            return index;
    return -1;
}
} // namespace

//==============================================================================
// Waveforms

TEST_CASE ("lfo: periodic waveforms have the documented shape")
{
    const Lfo lfo;
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Sine, 0.0, 0.5), 0.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Sine, 0.25, 0.5), 1.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Sine, 0.75, 0.5), -1.0));

    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Triangle, 0.0, 0.5), 0.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Triangle, 0.25, 0.5), 1.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Triangle, 0.5, 0.5), 0.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Triangle, 0.75, 0.5), -1.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Triangle, 0.125, 0.5), 0.5));

    EXPECT (isClose (lfo.waveformValue (LfoWaveform::SawUp, 0.0, 0.5), -1.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::SawUp, 0.75, 0.5), 0.5));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::SawDown, 0.0, 0.5), 1.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::SawDown, 0.75, 0.5), -0.5));

    // Waves repeat every cycle, also for negative positions
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::Sine, 3.25, 0.5), 1.0));
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::SawUp, -0.25, 0.5), 0.5));
}

TEST_CASE ("lfo: square wave follows the pulse width")
{
    const Lfo lfo;
    EXPECT (lfo.waveformValue (LfoWaveform::Square, 0.2, 0.25) == 1.0f);
    EXPECT (lfo.waveformValue (LfoWaveform::Square, 0.3, 0.25) == -1.0f);
    EXPECT (lfo.waveformValue (LfoWaveform::Square, 0.7, 0.75) == 1.0f);
    EXPECT (lfo.waveformValue (LfoWaveform::Square, 0.8, 0.75) == -1.0f);
    EXPECT (lfo.waveformValue (LfoWaveform::Square, 0.49, 0.5) == 1.0f);
    EXPECT (lfo.waveformValue (LfoWaveform::Square, 0.51, 0.5) == -1.0f);
}

TEST_CASE ("lfo: sample and hold keeps one random value per cycle")
{
    const Lfo lfo (123);
    std::vector<float> valuePerCycle;
    for (int cycle = 0; cycle < 50; ++cycle)
    {
        const float atStart = lfo.waveformValue (LfoWaveform::SampleAndHold, cycle + 0.01, 0.5);
        const float atEnd = lfo.waveformValue (LfoWaveform::SampleAndHold, cycle + 0.99, 0.5);
        EXPECT (atStart == atEnd);
        EXPECT (atStart >= -1.0f && atStart <= 1.0f);
        valuePerCycle.push_back (atStart);
    }
    std::sort (valuePerCycle.begin(), valuePerCycle.end());
    EXPECT (std::unique (valuePerCycle.begin(), valuePerCycle.end()) - valuePerCycle.begin() > 40); // varies
    EXPECT (valuePerCycle.front() < -0.5f && valuePerCycle.back() > 0.5f); // uses the whole range

    // Same seed, same sequence; different seed, different sequence
    const Lfo sameSeed (123);
    const Lfo otherSeed (456);
    EXPECT (sameSeed.waveformValue (LfoWaveform::SampleAndHold, 7.5, 0.5) ==
            lfo.waveformValue (LfoWaveform::SampleAndHold, 7.5, 0.5));
    EXPECT (otherSeed.waveformValue (LfoWaveform::SampleAndHold, 7.5, 0.5) !=
            lfo.waveformValue (LfoWaveform::SampleAndHold, 7.5, 0.5));
}

TEST_CASE ("lfo: smooth random glides without jumps between the held values")
{
    const Lfo lfo (99);
    float previous = lfo.waveformValue (LfoWaveform::SmoothRandom, 0.0, 0.5);
    float largestStep = 0.0f;
    for (int step = 1; step <= 10000; ++step)
    {
        const float value = lfo.waveformValue (LfoWaveform::SmoothRandom, step / 1000.0, 0.5);
        EXPECT (value >= -1.0f && value <= 1.0f);
        largestStep = std::max (largestStep, std::abs (value - previous));
        previous = value;
    }
    EXPECT (largestStep < 0.01f); // 1000 steps per cycle: a jump would be up to 2.0

    // At each cycle start it arrives exactly at that cycle's S&H value... of the previous cycle
    EXPECT (isClose (lfo.waveformValue (LfoWaveform::SmoothRandom, 5.0, 0.5),
                     lfo.waveformValue (LfoWaveform::SampleAndHold, 4.5, 0.5)));
}

//==============================================================================
// Timing

TEST_CASE ("lfo: free rate in Hz")
{
    Lfo lfo;
    LfoSettings settings;
    settings.isTempoSynced = false;
    settings.rateHz = 2.0;

    lfo.advance (settings, makeContext ((int) (testSampleRate / 8))); // 1/8 s at 2 Hz = 1/4 cycle
    EXPECT (isClose (lfo.valueAt (settings, makeContext (0)), 1.0, 1.0e-3));
    EXPECT (isClose (lfo.cyclePositionAt (settings, makeContext (0)), 0.25, 1.0e-6));
}

TEST_CASE ("lfo: tempo sync follows the host position and the bar grid")
{
    Lfo lfo;
    LfoSettings settings;
    settings.waveform = LfoWaveform::SawUp;
    settings.divisionIndex = indexOfDivision ("1 Bar");

    EXPECT (isClose (lfo.cyclePositionAt (settings, makeHostContext (0.0)), 0.0));
    EXPECT (isClose (lfo.cyclePositionAt (settings, makeHostContext (2.0)), 0.5)); // half a bar
    EXPECT (isClose (lfo.cyclePositionAt (settings, makeHostContext (13.0)), 3.25));
    EXPECT (isClose (lfo.valueAt (settings, makeHostContext (2.0)), 0.0));

    settings.divisionIndex = indexOfDivision ("1/8T");
    EXPECT (isClose (lfo.cyclePositionAt (settings, makeHostContext (1.0)), 3.0)); // 3 triplet eighths per beat
}

TEST_CASE ("lfo: tempo-synced LFO runs at project tempo while the transport is stopped")
{
    Lfo lfo;
    LfoSettings settings;
    settings.divisionIndex = indexOfDivision ("1/4");
    lfo.advance (settings, makeContext ((int) (testSampleRate * 0.125), 120.0)); // 1/4 beat at 120 bpm
    EXPECT (isClose (lfo.cyclePositionAt (settings, makeContext (0)), 0.25, 1.0e-6));
}

TEST_CASE ("lfo: continues smoothly when the transport stops")
{
    Lfo lfo;
    LfoSettings settings;
    settings.divisionIndex = indexOfDivision ("1 Bar");
    const int blockLength = 480;
    lfo.advance (settings, makeHostContext (6.0, blockLength)); // following the host...

    const double positionAfterHostBlock = (6.0 + blockLength / (testSampleRate / 2.0)) / 4.0;
    EXPECT (isClose (lfo.cyclePositionAt (settings, makeContext (0)), positionAfterHostBlock, 1.0e-6)); // ...stopped
}

TEST_CASE ("lfo: phase offset shifts the wave")
{
    Lfo lfo;
    LfoSettings settings;
    settings.phaseDegrees = 90.0;
    EXPECT (isClose (lfo.valueAt (settings, makeHostContext (0.0)), 1.0)); // sine starting at its top
    settings.phaseDegrees = 180.0;
    EXPECT (isClose (lfo.valueAt (settings, makeHostContext (1.0)), -1.0)); // 1/4 bar + half a cycle
}

TEST_CASE ("lfo: restart sets the cycle back to its start, in both clock modes")
{
    Lfo freeLfo;
    LfoSettings freeSettings;
    freeSettings.isTempoSynced = false;
    freeSettings.rateHz = 3.3;
    freeLfo.advance (freeSettings, makeContext (12345));
    freeLfo.restart (makeContext (0));
    EXPECT (isClose (freeLfo.cyclePositionAt (freeSettings, makeContext (0)), 0.0));

    Lfo syncedLfo;
    LfoSettings syncedSettings;
    syncedLfo.restart (makeHostContext (5.5));
    EXPECT (isClose (syncedLfo.cyclePositionAt (syncedSettings, makeHostContext (5.5)), 0.0));
    EXPECT (isClose (syncedLfo.cyclePositionAt (syncedSettings, makeHostContext (7.5)), 0.5));
}

//==============================================================================
// Routes

TEST_CASE ("routes: add LFO times amount to the knob value, sum up and clamp")
{
    SlotSettings base[numSlots];
    base[2].parameterValues[1] = 0.5f;
    base[5].parameterValues[3] = 0.9f;

    ModulationRoute routes[numModulationRoutes];
    routes[0] = { 0, modulationTargetIndex (2, 1), 0.25f }; // +0.25 * LFO 1
    routes[1] = { 1, modulationTargetIndex (2, 1), -0.5f }; // -0.5 * LFO 2, same target
    routes[2] = { 0, modulationTargetIndex (5, 3), 1.0f };  // would exceed 1.0
    routes[3] = { 0, 0, 1.0f };                             // no target: ignored
    routes[4] = { 2, modulationTargetIndex (0, 0), 0.0f };  // zero amount: inactive

    const float lfoValues[numLfos] = { 1.0f, 0.5f, -1.0f, 0.0f };
    SlotSettings modulated[numSlots];
    applyModulationRoutes (lfoValues, routes, base, modulated);

    EXPECT (isClose (modulated[2].parameterValues[1], 0.5 + 0.25 - 0.25));
    EXPECT (modulated[5].parameterValues[3] == 1.0f);
    EXPECT (modulated[0].parameterValues[0] == 0.0f);
    EXPECT (! routes[3].isActive() && ! routes[4].isActive());
    EXPECT (base[2].parameterValues[1] == 0.5f); // the user's values are never changed
}

TEST_CASE ("routes: target numbering covers every slot parameter")
{
    ModulationRoute route;
    route.targetIndex = modulationTargetIndex (7, 3);
    EXPECT (route.targetIndex == numModulationTargets - 1);
    EXPECT (route.targetSlotIndex() == 7 && route.targetParameterIndex() == 3);
    route.targetIndex = modulationTargetIndex (0, 0);
    EXPECT (route.targetIndex == 1 && route.targetSlotIndex() == 0 && route.targetParameterIndex() == 0);
}

//==============================================================================
// LFOs driving modules

TEST_CASE ("engine: a square LFO on Transpose alternates the pitch, and note-offs still match")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
    rig.lfos[0].waveform = LfoWaveform::Square;
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 1.0;
    rig.routeLfo (0, 0, 0, 0, 12.5f); // +-12.5 % of the 48-step range = +-6 semitones

    std::vector<int> transposedNotes;
    for (int note = 0; note < 8; ++note)
    {
        const auto output = rig.processBlock ({ noteOnAt (0, 60) });
        EXPECT (output.size() == 1);
        if (! output.empty())
            transposedNotes.push_back (output[0].noteNumber());
        rig.processSeconds (0.24);
        rig.processBlock ({ noteOffAt (0, 60) });
        rig.processSeconds (0.24);
    }
    EXPECT (std::count (transposedNotes.begin(), transposedNotes.end(), 66) >= 3);
    EXPECT (std::count (transposedNotes.begin(), transposedNotes.end(), 54) >= 3);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("engine: notes see the LFO value at their exact sample position")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
    rig.context.isPlaying = true;
    rig.context.hasPpqPosition = true;
    rig.lfos[0].waveform = LfoWaveform::SawUp;
    rig.lfos[0].divisionIndex = indexOfDivision ("1/32"); // 0.125 beats = 3000 samples per cycle
    rig.routeLfo (0, 0, 0, 0, 50.0f);                     // +-24 semitones over one cycle

    // Within one 256-sample block the saw rises by 256/3000 of its range: ~4 semitones.
    const auto output = rig.processBlock ({ noteOnAt (0, 60), noteOnAt (250, 61) });
    EXPECT (output.size() == 2);
    if (output.size() == 2)
        EXPECT (output[1].noteNumber() - output[0].noteNumber() >= 4); // 1 from the key + ~4 from the LFO
}

TEST_CASE ("engine: restart on first key makes every phrase start the same")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
    rig.lfos[0].waveform = LfoWaveform::SawUp;
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 0.37;
    rig.lfos[0].restartsOnFirstKey = true;
    rig.routeLfo (0, 0, 0, 0, 25.0f);

    std::vector<int> firstNoteOfPhrase;
    for (int phrase = 0; phrase < 4; ++phrase)
    {
        firstNoteOfPhrase.push_back (rig.processBlock ({ noteOnAt (17 * phrase, 60) })[0].noteNumber());
        rig.processSeconds (0.3);
        rig.processBlock ({ noteOnAt (0, 64) }); // second key while the first is held: no restart
        rig.processSeconds (0.3);
        rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64) });
        rig.processSeconds (0.77);
    }
    for (int note : firstNoteOfPhrase)
        EXPECT (note == firstNoteOfPhrase[0]);
    EXPECT (firstNoteOfPhrase[0] == 48); // saw starts at -1: -25 % of 48 steps = -12 semitones
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("engine: without active routes the output is identical to the plain chain")
{
    ChainTestRig withLfos;
    ChainTestRig reference;
    for (auto* rig : { &withLfos, &reference })
    {
        rig->loadModule (0, ModuleType::Arpeggiator, { 3, 2, 2, 50 });
        rig->loadModule (1, ModuleType::NoteEcho, { 5, 2, 70, 7 });
    }
    withLfos.lfos[0].waveform = LfoWaveform::SampleAndHold;
    withLfos.routeLfo (0, 0, 0, 0, 0.0f); // routed, but zero amount

    for (auto* rig : { &withLfos, &reference })
    {
        rig->processBlock ({ noteOnAt (3, 60), noteOnAt (3, 63), noteOnAt (3, 67) });
        rig->processSeconds (1.0);
        rig->processBlock ({ noteOffAt (0, 60), noteOffAt (0, 63), noteOffAt (0, 67) });
        rig->processSeconds (1.0);
    }
    EXPECT (withLfos.everyOutputEvent.size() == reference.everyOutputEvent.size());
    bool identical = withLfos.everyOutputEvent.size() == reference.everyOutputEvent.size();
    for (size_t index = 0; identical && index < reference.everyOutputEvent.size(); ++index)
    {
        const auto& first = withLfos.everyOutputEvent[index];
        const auto& second = reference.everyOutputEvent[index];
        identical = first.samplePosition == second.samplePosition && first.status == second.status &&
                    first.data1 == second.data1 && first.data2 == second.data2;
    }
    EXPECT (identical);
}

TEST_CASE ("engine: an LFO on the arpeggiator gate keeps the arpeggio on the grid")
{
    ChainTestRig rig;
    rig.context.isPlaying = true;
    rig.context.hasPpqPosition = true;
    rig.loadModule (0, ModuleType::Arpeggiator, { 3, 0, 1, 50 });
    rig.lfos[0].waveform = LfoWaveform::Triangle;
    rig.lfos[0].divisionIndex = indexOfDivision ("1 Bar");
    rig.routeLfo (0, 0, 0, 3, 50.0f);
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64) });
    rig.processSeconds (4.0);

    int previousNoteOn = -1;
    std::set<int> noteLengths;
    int lastOnTime = 0;
    for (const auto& event : rig.everyOutputEvent)
    {
        if (event.isNoteOn())
        {
            if (previousNoteOn >= 0)
                EXPECT (std::abs (event.samplePosition - previousNoteOn - 6000) <= 1);
            previousNoteOn = lastOnTime = event.samplePosition;
        }
        else if (event.isNoteOff())
        {
            noteLengths.insert (event.samplePosition - lastOnTime);
        }
    }
    EXPECT (noteLengths.size() > 5); // the gate really changes
}
