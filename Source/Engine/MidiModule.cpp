/**
 * @file MidiModule.cpp
 * @brief Shared behaviour of NoteMappingModule and SchedulingModule, random seeding and the module factory.
 */
#include "MidiModule.h"

#include "Modules/FilterModules.h"
#include "Modules/GeneratorModules.h"
#include "Modules/RandomizerModules.h"
#include "Modules/TransformModules.h"

namespace midiforge
{

//==============================================================================
// Random seeding

namespace
{
unsigned seedForNextModule = 0x5EEDu;
}

void setRandomSeedForNewModules (unsigned seed) noexcept
{
    seedForNextModule = seed;
}

unsigned MidiModule::createSeed() noexcept
{
    // Linear congruential step, so every module created after one seed gets its own sequence.
    seedForNextModule = seedForNextModule * 1664525u + 1013904223u;
    return seedForNextModule;
}

//==============================================================================
// NoteMappingModule

void NoteMappingModule::reset()
{
    mappingTable.clear();
    soundingNotes.clear();
}

void NoteMappingModule::releaseOutputNotes (NoteMappingTable::OutputNotes& outputNotes, int samplePosition,
                                            int releaseVelocity, MidiEventList& output)
{
    for (int index = 0; index < outputNotes.count; ++index)
    {
        const int channel = outputNotes.channels[index];
        const int noteNumber = outputNotes.noteNumbers[index];
        if (soundingNotes.noteEnded (channel, noteNumber))
            output.push_back (MidiEvent::noteOff (samplePosition, channel, noteNumber, releaseVelocity));
    }
    outputNotes.count = 0;
}

void NoteMappingModule::process (const MidiEventList& input, MidiEventList& output, const BlockContext& /*context*/,
                                 const float* parameterValues)
{
    for (const auto& event : input)
    {
        if (event.isNoteOn())
        {
            auto& outputNotes = mappingTable.forInputNote (event.channel(), event.noteNumber());

            // A second note-on without a note-off in between: end what the first one started.
            if (outputNotes.count > 0)
                releaseOutputNotes (outputNotes, event.samplePosition, 0, output);

            MidiEvent newNoteOns[NoteMappingTable::maxOutputNotesPerInput];
            const int numNewNotes = std::clamp (createNoteOns (event, newNoteOns, parameterValues), 0,
                                                NoteMappingTable::maxOutputNotesPerInput);

            for (int index = 0; index < numNewNotes; ++index)
            {
                MidiEvent noteOn = newNoteOns[index];
                noteOn.samplePosition = event.samplePosition;
                output.push_back (noteOn);
                soundingNotes.noteStarted (noteOn.channel(), noteOn.noteNumber());
                outputNotes.channels[outputNotes.count] = (uint8_t) noteOn.channel();
                outputNotes.noteNumbers[outputNotes.count] = (uint8_t) noteOn.noteNumber();
                ++outputNotes.count;
            }
        }
        else if (event.isNoteOff())
        {
            auto& outputNotes = mappingTable.forInputNote (event.channel(), event.noteNumber());
            if (outputNotes.count == 0)
            {
                output.push_back (event); // unknown note: let it through so it can be released downstream
            }
            else
            {
                const int releaseVelocity = event.messageType() == MidiStatus::noteOff ? event.velocity() : 0;
                releaseOutputNotes (outputNotes, event.samplePosition, releaseVelocity, output);
            }
        }
        else if (event.isPolyPressure() && mappingTable.forInputNote (event.channel(), event.noteNumber()).count > 0)
        {
            // Follow the note: send the pressure to every note this key produced.
            const auto& outputNotes = mappingTable.forInputNote (event.channel(), event.noteNumber());
            for (int index = 0; index < outputNotes.count; ++index)
            {
                MidiEvent pressure = event;
                pressure.setChannel (outputNotes.channels[index]);
                pressure.data1 = outputNotes.noteNumbers[index];
                output.push_back (pressure);
            }
        }
        else if (! event.isChannelMessage())
        {
            output.push_back (event); // clock, start/stop and other system messages always pass
        }
        else
        {
            MidiEvent copy = event;
            if (transformOtherEvent (copy, parameterValues))
                output.push_back (copy);
        }
    }
}

//==============================================================================
// SchedulingModule

SchedulingModule::SchedulingModule (ModuleType type) : MidiModule (type)
{
    // Note-offs are always accepted (refusing them could leave notes stuck), note-ons only up to
    // maxScheduledEvents. Reserving twice that keeps the audio thread free of allocations.
    scheduledEvents.reserve (maxScheduledEvents * 2);
    dueEvents.reserve (maxScheduledEvents * 2);
}

void SchedulingModule::reset()
{
    scheduledEvents.clear();
    soundingNotes.clear();
}

void SchedulingModule::scheduleEvent (int64_t sampleTime, const MidiEvent& event)
{
    if (event.isNoteOn() && scheduledEvents.size() >= maxScheduledEvents)
        return; // queue full: skip this extra note rather than allocating on the audio thread

    scheduledEvents.push_back ({ sampleTime, nextSequenceNumber++, event });
}

void SchedulingModule::sendEvent (const MidiEvent& event, MidiEventList& output)
{
    if (event.isNoteOn())
    {
        soundingNotes.noteStarted (event.channel(), event.noteNumber());
        output.push_back (event);
    }
    else if (event.isNoteOff())
    {
        if (soundingNotes.noteEnded (event.channel(), event.noteNumber()))
            output.push_back (event);
    }
    else
    {
        output.push_back (event);
    }
}

void SchedulingModule::sendScheduledEventsUpTo (int64_t sampleTime, const BlockContext& context, MidiEventList& output)
{
    // Move due events out of the queue, keeping the rest in place.
    dueEvents.clear();
    size_t numKept = 0;
    for (const auto& scheduled : scheduledEvents)
    {
        if (scheduled.sampleTime <= sampleTime)
            dueEvents.push_back (scheduled);
        else
            scheduledEvents[numKept++] = scheduled;
    }
    scheduledEvents.resize (numKept);

    if (dueEvents.empty())
        return;

    std::sort (dueEvents.begin(), dueEvents.end(),
               [] (const ScheduledEvent& first, const ScheduledEvent& second)
               {
                   if (first.sampleTime != second.sampleTime)
                       return first.sampleTime < second.sampleTime;
                   return first.sequenceNumber < second.sequenceNumber;
               });

    const int lastSampleInBlock = std::max (0, context.numSamples - 1);
    for (const auto& due : dueEvents)
    {
        MidiEvent event = due.event;
        event.samplePosition =
            (int) std::clamp<int64_t> (due.sampleTime - context.blockStartSample, 0, lastSampleInBlock);
        sendEvent (event, output);
    }
}

//==============================================================================

std::unique_ptr<MidiModule> createModule (ModuleType type)
{
    switch (getModuleDescription (type).category)
    {
        case ModuleCategory::Filter: return createFilterModule (type);
        case ModuleCategory::Transform: return createTransformModule (type);
        case ModuleCategory::Generator: return createGeneratorModule (type);
        case ModuleCategory::Randomizer: return createRandomizerModule (type);
        case ModuleCategory::None: break;
    }
    return nullptr;
}

} // namespace midiforge
