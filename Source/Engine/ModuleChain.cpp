/**
 * @file ModuleChain.cpp
 * @brief Implementation of the slot chain. See ModuleChain.h for the note-consistency rules.
 */
#include "ModuleChain.h"

namespace midiforge
{

namespace
{
constexpr size_t reservedEventsPerBuffer = 8192;
constexpr int allNotesOffController = 123;
} // namespace

ModuleChain::ModuleChain()
{
    for (auto& modules : modulesBySlot)
        for (int type = 1; type < numModuleTypes; ++type) // index 0 (Empty) stays nullptr
            modules[(size_t) type] = createModule ((ModuleType) type);

    for (auto* buffer : { &slotInput, &slotOutput, &moduleOutput, &panicEvents })
        buffer->reserve (reservedEventsPerBuffer);
}

void ModuleChain::prepare (double /*sampleRate*/)
{
    reset();
}

void ModuleChain::reset()
{
    for (auto& modules : modulesBySlot)
        for (auto& module : modules)
            if (module != nullptr)
                module->reset();

    activeModuleType.fill (0);
    for (auto& tracker : notesAtSlotInput)
        tracker.clear();
    for (auto& tracker : notesAtSlotOutput)
        tracker.clear();
    for (auto& orphans : isOrphanedAtSlotInput)
        orphans.fill (false);
    notesAtChainOutput.clear();
    samplesProcessed = 0;
}

void ModuleChain::handlePanic (MidiEventList& events)
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const auto slot = (size_t) slotIndex;
        if (activeModuleType[slot] > 0)
            modulesBySlot[slot][(size_t) activeModuleType[slot]]->reset();

        // Keys still held by the player must not produce note-offs for notes that no longer exist.
        for (size_t key = 0; key < numChannelNoteCombinations; ++key)
            if (notesAtSlotInput[slot].isSounding (key))
                isOrphanedAtSlotInput[slot][key] = true;

        notesAtSlotInput[slot].clear();
        notesAtSlotOutput[slot].clear();
    }

    for (int channel = 0; channel < numMidiChannels; ++channel)
        events.push_back (MidiEvent::controlChange (0, channel, allNotesOffController, 0));

    // Also send explicit note-offs: not every synth honours All Notes Off.
    for (size_t key = 0; key < numChannelNoteCombinations; ++key)
        if (notesAtChainOutput.isSounding (key))
            events.push_back (MidiEvent::noteOff (0, (int) key / numMidiNotes, (int) key % numMidiNotes));
    notesAtChainOutput.clear();
}

void ModuleChain::restartSlot (int slotIndex, MidiEventList& output)
{
    const auto slot = (size_t) slotIndex;
    if (activeModuleType[slot] > 0)
        modulesBySlot[slot][(size_t) activeModuleType[slot]]->reset();

    // Release everything the slot was sending; the note-offs flow on to the downstream slots.
    for (size_t key = 0; key < numChannelNoteCombinations; ++key)
        if (notesAtSlotOutput[slot].isSounding (key))
            output.push_back (MidiEvent::noteOff (0, (int) key / numMidiNotes, (int) key % numMidiNotes));

    // The notes held at the input have now been released; swallow their note-offs when they arrive.
    for (size_t key = 0; key < numChannelNoteCombinations; ++key)
        isOrphanedAtSlotInput[slot][key] = notesAtSlotInput[slot].isSounding (key);
}

void ModuleChain::removeOrphanedNoteOffs (int slotIndex, MidiEventList& events)
{
    auto& isOrphaned = isOrphanedAtSlotInput[(size_t) slotIndex];
    size_t numKept = 0;
    for (const auto& event : events)
    {
        if (event.isNoteOnOrOff())
        {
            auto& orphaned = isOrphaned[channelNoteIndex (event.channel(), event.noteNumber())];
            if (orphaned)
            {
                orphaned = false; // a new note-on also ends the orphan state: the key is in use again
                if (event.isNoteOff())
                    continue;
            }
        }
        events[numKept++] = event;
    }
    events.resize (numKept);
}

void ModuleChain::process (MidiEventList& events, BlockContext context, const SlotSettings* slotSettings)
{
    context.blockStartSample = samplesProcessed;
    samplesProcessed += context.numSamples;

    panicEvents.clear();
    if (isPanicRequested.exchange (false))
        handlePanic (panicEvents);

    slotInput.assign (events.begin(), events.end());
    MidiEventList* currentEvents = &slotInput;
    MidiEventList* nextEvents = &slotOutput;

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const auto slot = (size_t) slotIndex;
        const auto& settings = slotSettings[slotIndex];
        const int wantedType = settings.isEnabled ? std::clamp (settings.moduleType, 0, numModuleTypes - 1) : 0;

        nextEvents->clear();
        if (wantedType != activeModuleType[slot])
        {
            restartSlot (slotIndex, *nextEvents);
            activeModuleType[slot] = wantedType;
            if (wantedType > 0)
                modulesBySlot[slot][(size_t) wantedType]->reset();
        }

        notesAtSlotInput[slot].track (*currentEvents);
        removeOrphanedNoteOffs (slotIndex, *currentEvents);

        if (wantedType > 0)
        {
            moduleOutput.clear();
            modulesBySlot[slot][(size_t) wantedType]->process (*currentEvents, moduleOutput, context,
                                                               settings.parameterValues);
            nextEvents->insert (nextEvents->end(), moduleOutput.begin(), moduleOutput.end());
        }
        else
        {
            nextEvents->insert (nextEvents->end(), currentEvents->begin(), currentEvents->end());
        }

        sortBySamplePosition (*nextEvents);
        notesAtSlotOutput[slot].track (*nextEvents);
        std::swap (currentEvents, nextEvents);
    }

    notesAtChainOutput.track (*currentEvents);
    events.clear();
    events.insert (events.end(), panicEvents.begin(), panicEvents.end());
    events.insert (events.end(), currentEvents->begin(), currentEvents->end());
}

} // namespace midiforge
