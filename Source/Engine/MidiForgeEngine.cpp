/**
 * @file MidiForgeEngine.cpp
 * @brief Implementation of MidiForgeEngine: segmenting blocks, running the LFOs, applying routes.
 */
#include "MidiForgeEngine.h"

#include <cmath>

namespace midiforge
{

namespace
{
constexpr size_t reservedEventsPerBuffer = 8192;

bool anyRouteIsActive (const EngineSettings& settings) noexcept
{
    for (const auto& route : settings.routes)
        if (route.isActive())
            return true;
    return false;
}
} // namespace

MidiForgeEngine::MidiForgeEngine()
{
    for (auto& lfo : lfos)
        lfo = Lfo (takeNextRandomSeed());
    for (auto& noteRandom : noteRandoms)
        noteRandom = NoteRandom (takeNextRandomSeed());

    for (auto* buffer : { &blockInput, &segmentEvents, &blockOutput })
        buffer->reserve (reservedEventsPerBuffer);
    std::fill (std::begin (slotSwitchStates), std::end (slotSwitchStates), true);
}

void MidiForgeEngine::prepare (double sampleRate)
{
    moduleChain.prepare (sampleRate);
    reset();
}

void MidiForgeEngine::reset()
{
    moduleChain.reset();
    for (auto& lfo : lfos)
        lfo.reset();
    for (auto& noteRandom : noteRandoms)
        noteRandom.reset();
    for (auto& stepModulator : stepModulators)
        stepModulator.reset();
    samplesProcessed = 0;
    isKeyHeld.fill (false);
    numKeysHeld = 0;
    std::fill (std::begin (slotSwitchStates), std::end (slotSwitchStates), true);
}

BlockContext MidiForgeEngine::contextForSegment (const BlockContext& blockContext, int segmentStart,
                                                 int segmentLength) const
{
    BlockContext segmentContext = blockContext;
    segmentContext.numSamples = segmentLength;
    if (blockContext.hasPpqPosition)
        segmentContext.ppqPositionAtBlockStart += segmentStart / blockContext.samplesPerBeat();
    return segmentContext;
}

void MidiForgeEngine::handleNotesForModulators (const MidiEventList& segmentInput, const BlockContext& segmentContext,
                                                int64_t segmentStartSample, const EngineSettings& settings)
{
    for (const auto& event : segmentInput)
    {
        if (! event.isNoteOnOrOff())
            continue;

        bool& isHeld = isKeyHeld[channelNoteIndex (event.channel(), event.noteNumber())];
        if (event.isNoteOn())
        {
            const bool isFirstKeyOfPhrase = numKeysHeld == 0;
            if (isFirstKeyOfPhrase)
            {
                for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
                    if (settings.lfos[lfoIndex].restartsOnFirstKey)
                        lfos[lfoIndex].restart (segmentContext);
                for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
                    if (settings.stepModulators[stepIndex].restartsOnFirstKey)
                        stepModulators[stepIndex].restart (segmentContext);
            }

            // Every played note-on is a note event for the Note Random modulators (also a repeated key).
            for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
                noteRandoms[randomIndex].noteStarted (segmentStartSample + event.samplePosition, isFirstKeyOfPhrase,
                                                      settings.noteRandoms[randomIndex], segmentContext.sampleRate);

            if (! isHeld)
            {
                isHeld = true;
                ++numKeysHeld;
            }
        }
        else if (isHeld)
        {
            isHeld = false;
            --numKeysHeld;
        }
    }
}

void MidiForgeEngine::computeSourceValues (const BlockContext& segmentContext, const EngineSettings& settings)
{
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        lfoValues[lfoIndex] = lfos[lfoIndex].valueAt (settings.lfos[lfoIndex], segmentContext);
        sourceValues.bipolar[lfoIndex] = lfoValues[lfoIndex];
        sourceValues.unipolar[lfoIndex] = lfos[lfoIndex].unipolarValueAt (settings.lfos[lfoIndex], segmentContext);
    }
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
    {
        const float value = noteRandoms[randomIndex].getValue();
        sourceValues.bipolar[firstNoteRandomSource + randomIndex] = value;
        sourceValues.unipolar[firstNoteRandomSource + randomIndex] = (value + 1.0f) * 0.5f;
    }
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        const float value = stepModulators[stepIndex].valueAt (settings.stepModulators[stepIndex], segmentContext);
        sourceValues.bipolar[firstStepModulatorSource + stepIndex] = value;
        sourceValues.unipolar[firstStepModulatorSource + stepIndex] = (value + 1.0f) * 0.5f;
    }
}

void MidiForgeEngine::processSegment (int segmentStart, int segmentLength, const BlockContext& blockContext,
                                      const EngineSettings& settings, bool isModulationActive)
{
    // Collect this segment's input events, with positions relative to the segment.
    segmentEvents.clear();
    for (const auto& event : blockInput)
    {
        if (event.samplePosition >= segmentStart && event.samplePosition < segmentStart + segmentLength)
        {
            MidiEvent shifted = event;
            shifted.samplePosition -= segmentStart;
            segmentEvents.push_back (shifted);
        }
    }

    const BlockContext segmentContext = contextForSegment (blockContext, segmentStart, segmentLength);
    handleNotesForModulators (segmentEvents, segmentContext, samplesProcessed + segmentStart, settings);
    computeSourceValues (segmentContext, settings);

    if (isModulationActive)
    {
        applyModulationRoutes (sourceValues, settings.routes, settings.slots, modulatedSlots, slotSwitchStates);
    }
    else
    {
        std::copy (std::begin (settings.slots), std::end (settings.slots), modulatedSlots);
        for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
            slotSwitchStates[slotIndex] = settings.slots[slotIndex].isEnabled;
    }

    moduleChain.process (segmentEvents, segmentContext, modulatedSlots);

    for (const auto& event : segmentEvents)
    {
        MidiEvent shifted = event;
        shifted.samplePosition += segmentStart;
        blockOutput.push_back (shifted);
    }

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
        lfos[lfoIndex].advance (settings.lfos[lfoIndex], segmentContext);
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
        stepModulators[stepIndex].advance (settings.stepModulators[stepIndex], segmentContext);
}

void MidiForgeEngine::process (MidiEventList& events, const BlockContext& context, const EngineSettings& settings)
{
    blockInput.assign (events.begin(), events.end());
    blockOutput.clear();

    const int blockLength = std::max (1, context.numSamples);
    const bool isModulationActive = anyRouteIsActive (settings);

    if (! isModulationActive)
    {
        // Nothing to modulate: one segment, exactly as if there were no LFOs.
        processSegment (0, blockLength, context, settings, false);
    }
    else
    {
        size_t nextEventIndex = 0;
        int segmentStart = 0;
        while (segmentStart < blockLength)
        {
            while (nextEventIndex < blockInput.size() && blockInput[nextEventIndex].samplePosition <= segmentStart)
                ++nextEventIndex;

            int segmentEnd = std::min (segmentStart + maxSegmentLength, blockLength);
            if (nextEventIndex < blockInput.size())
                segmentEnd = std::min (segmentEnd, blockInput[nextEventIndex].samplePosition);

            processSegment (segmentStart, segmentEnd - segmentStart, context, settings, true);
            segmentStart = segmentEnd;
        }
    }

    updateLfoReadouts (contextForSegment (context, blockLength, 0), settings);
    samplesProcessed += blockLength;
    events.assign (blockOutput.begin(), blockOutput.end());
}

void MidiForgeEngine::updateLfoReadouts (const BlockContext& endOfBlockContext, const EngineSettings& settings)
{
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        const double cyclePosition = lfos[lfoIndex].cyclePositionAt (settings.lfos[lfoIndex], endOfBlockContext);
        lfoPhases[lfoIndex] = cyclePosition - std::floor (cyclePosition);
        lfoValues[lfoIndex] = lfos[lfoIndex].valueAt (settings.lfos[lfoIndex], endOfBlockContext);
    }
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        const auto& stepSettings = settings.stepModulators[stepIndex];
        stepModulatorValues[stepIndex] = stepModulators[stepIndex].valueAt (stepSettings, endOfBlockContext);
        currentSteps[stepIndex] = stepModulators[stepIndex].currentStepAt (stepSettings, endOfBlockContext);
    }
}

} // namespace midiforge
