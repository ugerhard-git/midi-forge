/**
 * @file ModuleChainTests.cpp
 * @brief The chain as a whole: slot changes while playing, panic, and a randomized stuck-note hunt.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <algorithm>
#include <random>

using namespace testing;

TEST_CASE ("chain: empty chain passes everything through unchanged")
{
    ChainTestRig rig;
    const MidiEvent sysexLikeShortMessage { 3, 0xF8, 0, 0, 1 };
    const auto output =
        rig.processBlock ({ noteOnAt (0, 60), MidiEvent::controlChange (1, 2, 74, 10), sysexLikeShortMessage });
    EXPECT (output.size() == 3);
    EXPECT (output[0].isNoteOn() && output[1].isControlChange() && output[2].status == 0xF8);
}

TEST_CASE ("chain: switching a module while notes sound releases them")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Chord, { 0, 0, 0, 100 });
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.loadModule (0, ModuleType::Transpose, { 5, 0 });
    rig.processBlock();
    EXPECT (rig.notesStillSounding().empty());

    // The key's later note-off is swallowed instead of reaching the new module.
    const auto output = rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (output.empty());
}

TEST_CASE ("chain: switching a slot off and on while notes sound leaves nothing stuck")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 12, 0 });
    rig.loadModule (1, ModuleType::ScaleQuantize, { 0, 1, 0 });
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64) });
    rig.slots[1].isEnabled = false;
    rig.processBlock ({ noteOnAt (0, 66) });
    rig.slots[1].isEnabled = true;
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64) });
    rig.processBlock ({ noteOffAt (0, 66) });
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("chain: modules run in slot order")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 1, 0 });        // C -> C#
    rig.loadModule (3, ModuleType::ScaleQuantize, { 0, 0, 1 }); // C# -> D (snap up)
    const auto output = rig.processBlock ({ noteOnAt (0, 60) });
    EXPECT (output.size() == 1 && output[0].noteNumber() == 62);
}

TEST_CASE ("chain: panic releases every note and sends All Notes Off on all channels")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Chord, { 8, 0, 0, 100 });
    rig.loadModule (1, ModuleType::NoteEcho, { 11, 8, 90, 0 });
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (3.0);
    rig.engine.requestPanic();
    const auto output = rig.processBlock();

    int numAllNotesOff = 0;
    for (const auto& event : output)
        numAllNotesOff += event.isControlChange() && event.data1 == 123;
    EXPECT (numAllNotesOff == 16);
    EXPECT (rig.notesStillSounding().empty());

    // Echoes that were still scheduled must not come back.
    rig.processSeconds (10.0);
    EXPECT (rig.notesStillSounding().empty());
    // The held key's note-off after a panic is swallowed.
    EXPECT (rig.processBlock ({ noteOffAt (0, 60) }).empty());
}

/**
 * Random chains, random parameter changes, random LFO modulation, random playing and random slot switching.
 * Once every key is released and the longest tail has finished, no note may still sound.
 */
TEST_CASE ("chain: randomized stress test leaves no stuck notes and no pitch bend behind")
{
    std::mt19937 random (1234);
    const auto randomInteger = [&] (int lowest, int highest)
    { return std::uniform_int_distribution<int> (lowest, highest) (random); };
    const auto randomUnit = [&] { return std::uniform_real_distribution<float> (0.0f, 1.0f) (random); };

    constexpr int numRuns = 300;
    constexpr int numBlocksPlayed = 400;
    constexpr double longestTailSeconds = 70.0; // 16 echoes of one bar at 60 bpm = 64 s

    for (int run = 0; run < numRuns; ++run)
    {
        setRandomSeedForNewModules ((unsigned) run + 1);
        ChainTestRig rig;
        rig.context.tempoBpm = 60 + randomInteger (0, 180);
        rig.context.isPlaying = rig.context.hasPpqPosition = randomInteger (0, 1) == 1;
        for (auto& slot : rig.slots)
        {
            slot.moduleType = randomInteger (0, 3) == 0 ? 0 : randomInteger (1, numModuleTypes - 1);
            slot.isEnabled = randomInteger (0, 5) != 0;
            for (float& value : slot.parameterValues)
                value = randomUnit();
        }

        // Random LFOs and routes in half of the runs, so modulation is part of the stress test.
        const auto randomizeLfo = [&] (LfoSettings& lfo)
        {
            lfo.waveform = (LfoWaveform) randomInteger (0, numLfoWaveforms - 1);
            lfo.isTempoSynced = randomInteger (0, 1) == 1;
            lfo.rateHz = 0.05 + 20.0 * randomUnit();
            lfo.divisionIndex = randomInteger (0, numLfoDivisions - 1);
            lfo.phaseDegrees = 360.0 * randomUnit();
            lfo.pulseWidth = 0.05 + 0.9 * randomUnit();
            lfo.restartsOnFirstKey = randomInteger (0, 1) == 1;
        };
        const bool usesModulation = run % 2 == 1;
        if (usesModulation)
        {
            for (auto& lfo : rig.lfos)
                randomizeLfo (lfo);
            for (auto& noteRandom : rig.noteRandoms)
            {
                noteRandom.chordWindowMilliseconds = 100.0 * randomUnit();
                noteRandom.trigger = (NoteRandomTrigger) randomInteger (0, 1);
                noteRandom.numSteps = randomInteger (0, 1) == 0 ? 0 : randomInteger (2, 12);
            }
            for (auto& route : rig.settings.routes)
            {
                route.sourceIndex = randomInteger (0, numModulationSources - 1);
                route.isUnipolar = randomInteger (0, 1) == 1;
                route.targetIndex = randomInteger (0, numModulationTargets - 1);
                route.amount = 2.0f * randomUnit() - 1.0f;
            }
        }

        // Modules that block or move pitch bends may legitimately keep a slide's bend from being undone downstream.
        bool chainMayHoldBackBends = false;
        const auto noteModulesThatMoveOrBlockBends = [&]
        {
            for (const auto& slot : rig.slots)
                chainMayHoldBackBends = chainMayHoldBackBends || slot.moduleType == (int) ModuleType::EventBlocker ||
                                        slot.moduleType == (int) ModuleType::ChannelFilter ||
                                        slot.moduleType == (int) ModuleType::ChannelRemap ||
                                        slot.moduleType == (int) ModuleType::NoteDispatcher;
        };
        noteModulesThatMoveOrBlockBends();

        std::vector<std::pair<int, int>> heldKeys; // (channel, note)
        for (int block = 0; block < numBlocksPlayed; ++block)
        {
            const int numEvents = randomInteger (0, 3);
            std::vector<int> positions;
            positions.reserve ((size_t) numEvents);
            for (int index = 0; index < numEvents; ++index)
                positions.push_back (randomInteger (0, testBlockSize - 1));
            std::sort (positions.begin(), positions.end());

            MidiEventList input;
            for (int position : positions)
            {
                if (! heldKeys.empty() && randomInteger (0, 1) == 1)
                {
                    const auto released = (size_t) randomInteger (0, (int) heldKeys.size() - 1);
                    input.push_back (noteOffAt (position, heldKeys[released].second, heldKeys[released].first));
                    heldKeys.erase (heldKeys.begin() + (std::ptrdiff_t) released);
                }
                else
                {
                    const int channel = randomInteger (0, 2);
                    const int note = randomInteger (30, 100);
                    input.push_back (noteOnAt (position, note, randomInteger (1, 127), channel));
                    heldKeys.push_back ({ channel, note });
                }
                if (randomInteger (0, 10) == 0)
                    input.push_back (MidiEvent::controlChange (position, randomInteger (0, 2), randomInteger (0, 127),
                                                               randomInteger (0, 127)));
            }

            if (randomInteger (0, 20) == 0)
                rig.slots[randomInteger (0, numSlots - 1)].parameterValues[randomInteger (0, 3)] = randomUnit();
            if (randomInteger (0, 60) == 0)
            {
                auto& slot = rig.slots[randomInteger (0, numSlots - 1)];
                if (randomInteger (0, 1) == 1)
                    slot.moduleType = randomInteger (0, numModuleTypes - 1);
                else
                    slot.isEnabled = ! slot.isEnabled;
            }
            if (randomInteger (0, 80) == 0)
                rig.context.isPlaying = rig.context.hasPpqPosition = ! rig.context.isPlaying;
            if (randomInteger (0, 300) == 0)
                rig.engine.requestPanic();
            if (usesModulation && randomInteger (0, 40) == 0)
                randomizeLfo (rig.lfos[randomInteger (0, numLfos - 1)]);
            if (usesModulation && randomInteger (0, 40) == 0)
                rig.settings.routes[randomInteger (0, numModulationRoutes - 1)].targetIndex =
                    randomInteger (0, numModulationTargets - 1);

            noteModulesThatMoveOrBlockBends();
            rig.processBlock (input);
        }

        MidiEventList releaseAllKeys;
        for (const auto& [channel, note] : heldKeys)
            releaseAllKeys.push_back (noteOffAt (0, note, channel));
        rig.processBlock (releaseAllKeys);
        rig.processSeconds (longestTailSeconds);

        const auto stuck = rig.notesStillSounding();
        if (! stuck.empty())
        {
            std::printf ("    run %d left %zu stuck note(s); chain:", run, stuck.size());
            for (const auto& slot : rig.slots)
                std::printf (" %s%s", getModuleDescription (slot.moduleType).name, slot.isEnabled ? "" : " (off)");
            std::printf ("\n");
        }
        EXPECT (stuck.empty());

        // Pitch Slides return every channel to the player's bend (here: none, so the centre).
        if (! chainMayHoldBackBends)
        {
            int lastBend[numMidiChannels];
            std::fill (std::begin (lastBend), std::end (lastBend), (int) MidiEvent::pitchBendCentre);
            for (const auto& event : rig.everyOutputEvent)
                if (event.isPitchBend())
                    lastBend[event.channel()] = event.pitchBendValue();
            for (int channel = 0; channel < numMidiChannels; ++channel)
            {
                if (lastBend[channel] != MidiEvent::pitchBendCentre)
                    std::printf ("    run %d left channel %d bent to %d\n", run, channel + 1, lastBend[channel]);
                EXPECT (lastBend[channel] == MidiEvent::pitchBendCentre);
            }
        }
    }
}
