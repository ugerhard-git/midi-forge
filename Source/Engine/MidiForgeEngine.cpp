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

    for (auto* buffer : { &blockInput, &segmentEvents, &blockOutput })
        buffer->reserve (reservedEventsPerBuffer);
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
    isKeyHeld.fill (false);
    numKeysHeld = 0;
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

void MidiForgeEngine::restartLfosIfFirstKeyPressed (const MidiEventList& segmentInput,
                                                    const BlockContext& segmentContext, const EngineSettings& settings)
{
    for (const auto& event : segmentInput)
    {
        if (! event.isNoteOnOrOff())
            continue;

        bool& isHeld = isKeyHeld[channelNoteIndex (event.channel(), event.noteNumber())];
        if (event.isNoteOn() && ! isHeld)
        {
            if (numKeysHeld == 0)
                for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
                    if (settings.lfos[lfoIndex].restartsOnFirstKey)
                        lfos[lfoIndex].restart (segmentContext);
            isHeld = true;
            ++numKeysHeld;
        }
        else if (event.isNoteOff() && isHeld)
        {
            isHeld = false;
            --numKeysHeld;
        }
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
    restartLfosIfFirstKeyPressed (segmentEvents, segmentContext, settings);

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
        lfoValues[lfoIndex] = lfos[lfoIndex].valueAt (settings.lfos[lfoIndex], segmentContext);

    if (isModulationActive)
        applyModulationRoutes (lfoValues, settings.routes, settings.slots, modulatedSlots);
    else
        std::copy (std::begin (settings.slots), std::end (settings.slots), modulatedSlots);

    moduleChain.process (segmentEvents, segmentContext, modulatedSlots);

    for (const auto& event : segmentEvents)
    {
        MidiEvent shifted = event;
        shifted.samplePosition += segmentStart;
        blockOutput.push_back (shifted);
    }

    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
        lfos[lfoIndex].advance (settings.lfos[lfoIndex], segmentContext);
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
}

} // namespace midiforge
