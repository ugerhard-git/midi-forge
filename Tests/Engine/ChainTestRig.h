/**
 * @file ChainTestRig.h
 * @brief Drives the engine (module chain plus LFOs) block by block, like a host would, and records
 *        everything it outputs.
 */
#pragma once

#include "Engine/MidiForgeEngine.h"

#include <initializer_list>
#include <map>

namespace testing
{

using namespace midiforge;

constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 256;

class ChainTestRig
{
public:
    ChainTestRig()
    {
        engine.prepare (testSampleRate);
        context.sampleRate = testSampleRate;
        context.tempoBpm = 120.0;
        context.numSamples = testBlockSize;
    }

    /**
     * Loads a module into a slot. Parameters not listed keep the module's defaults.
     * @param plainValues  Plain (not normalised) values for parameters A, B, ... in order.
     */
    void loadModule (int slotIndex, ModuleType type, std::initializer_list<float> plainValues = {})
    {
        auto& slot = slots[slotIndex];
        slot.moduleType = (int) type;
        slot.isEnabled = true;
        const auto& description = getModuleDescription (type);
        for (int index = 0; index < numParametersPerSlot; ++index)
            slot.parameterValues[index] = getNormalisedDefault (description.parameters[index]);

        int parameterIndex = 0;
        for (float plainValue : plainValues)
        {
            slot.parameterValues[parameterIndex] =
                plainValueToNormalised (description.parameters[parameterIndex], plainValue);
            ++parameterIndex;
        }
    }

    /** Changes one parameter of a loaded module, using a plain value. */
    void setParameter (int slotIndex, int parameterIndex, float plainValue)
    {
        const auto& description = getModuleDescription (slots[slotIndex].moduleType);
        slots[slotIndex].parameterValues[parameterIndex] =
            plainValueToNormalised (description.parameters[parameterIndex], plainValue);
    }

    /** Runs one block with the given input events (positions relative to the block) and returns its output. */
    MidiEventList processBlock (MidiEventList input = {})
    {
        sortBySamplePosition (input);
        engine.process (input, context, settings);

        for (const auto& event : input)
        {
            MidiEvent withAbsoluteTime = event;
            withAbsoluteTime.samplePosition = (int) (samplesElapsed + event.samplePosition);
            everyOutputEvent.push_back (withAbsoluteTime);
        }

        samplesElapsed += testBlockSize;
        if (context.isPlaying)
            context.ppqPositionAtBlockStart += testBlockSize / context.samplesPerBeat();
        return input;
    }

    /** Runs empty blocks for (at least) the given time. */
    void processSeconds (double seconds)
    {
        const int numBlocks = (int) (seconds * testSampleRate / testBlockSize);
        for (int block = 0; block < numBlocks; ++block)
            processBlock();
    }

    /** @return Notes still sounding as a synth would see it, keyed by channel * 128 + note. */
    std::map<int, int> notesStillSounding() const
    {
        std::map<int, int> soundingCountByKey;
        for (const auto& event : everyOutputEvent)
        {
            const int key = event.channel() * numMidiNotes + event.noteNumber();
            if (event.isNoteOn())
                soundingCountByKey[key]++;
            else if (event.isNoteOff())
                soundingCountByKey.erase (key);
        }
        return soundingCountByKey;
    }

    /** @return The note numbers of all note-ons sent so far, in order. */
    std::vector<int> playedNoteNumbers() const
    {
        std::vector<int> noteNumbers;
        for (const auto& event : everyOutputEvent)
            if (event.isNoteOn())
                noteNumbers.push_back (event.noteNumber());
        return noteNumbers;
    }

    /**
     * Routes a modulation source to a slot parameter.
     * @param sourceIndex    0..3 for LFO 1..4, 4..7 for Random 1..4
     * @param amountPercent  -100..+100
     */
    void routeLfo (int routeIndex, int sourceIndex, int slotIndex, int parameterIndex, float amountPercent,
                   bool isUnipolar = false)
    {
        settings.routes[routeIndex].sourceIndex = sourceIndex;
        settings.routes[routeIndex].isUnipolar = isUnipolar;
        settings.routes[routeIndex].targetIndex = modulationTargetIndex (slotIndex, parameterIndex);
        settings.routes[routeIndex].amount = amountPercent / 100.0f;
    }

    MidiForgeEngine engine;
    EngineSettings settings;
    SlotSettings (&slots)[numSlots] = settings.slots;                         ///< Shortcut to settings.slots
    LfoSettings (&lfos)[numLfos] = settings.lfos;                             ///< Shortcut to settings.lfos
    NoteRandomSettings (&noteRandoms)[numNoteRandoms] = settings.noteRandoms; ///< Shortcut
    BlockContext context;
    MidiEventList everyOutputEvent; ///< samplePosition holds the absolute time in samples
    int64_t samplesElapsed = 0;
};

/** Convenience: a note-on on channel 1 (index 0). */
inline MidiEvent noteOnAt (int samplePosition, int noteNumber, int velocity = 100, int channel = 0)
{
    return MidiEvent::noteOn (samplePosition, channel, noteNumber, velocity);
}

/** Convenience: a note-off on channel 1 (index 0). */
inline MidiEvent noteOffAt (int samplePosition, int noteNumber, int channel = 0)
{
    return MidiEvent::noteOff (samplePosition, channel, noteNumber);
}

} // namespace testing
