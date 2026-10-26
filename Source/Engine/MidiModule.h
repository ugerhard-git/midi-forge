/**
 * @file MidiModule.h
 * @brief Base classes shared by all modules.
 *
 * Two rules keep the chain free of stuck notes, whatever the user does while playing:
 *  1. A module that receives a note-off it does not recognise passes it through unchanged, so notes
 *     that started before the module was inserted or re-configured still get released downstream.
 *  2. A module counts how often every output note is sounding and only sends the note-off once the
 *     last holder of that note ends (see SoundingNoteCounter).
 */
#pragma once

#include "MidiEvent.h"
#include "ModuleDescriptions.h"

#include <array>
#include <memory>
#include <random>

namespace midiforge
{

/**
 * @brief Counts how many times each (channel, note) is currently sounding at a module's output.
 *
 * Two different inputs can produce the same output note (two keys quantized to one scale note, an echo
 * landing on a held key). The note must stay on until both have ended, so only the last note-off is sent.
 */
class SoundingNoteCounter
{
public:
    void clear() noexcept { soundingCount.fill (0); }

    /** Records that a note-on for this note was sent. */
    void noteStarted (int channel, int noteNumber) noexcept
    {
        auto& count = soundingCount[channelNoteIndex (channel, noteNumber)];
        if (count < UINT16_MAX)
            ++count;
    }

    /**
     * Records that one holder of this note ended.
     * @return True if the note-off should actually be sent, i.e. this was the last holder,
     *         or the note was unknown (unknown note-offs always pass, see rule 1).
     */
    bool noteEnded (int channel, int noteNumber) noexcept
    {
        auto& count = soundingCount[channelNoteIndex (channel, noteNumber)];
        if (count == 0)
            return true;
        --count;
        return count == 0;
    }

    /** Calls releaseNote (channel, noteNumber) for every sounding note, then forgets them all. */
    template <typename ReleaseFunction> void releaseAll (ReleaseFunction&& releaseNote)
    {
        for (size_t index = 0; index < soundingCount.size(); ++index)
        {
            if (soundingCount[index] > 0)
            {
                releaseNote ((int) index / numMidiNotes, (int) index % numMidiNotes);
                soundingCount[index] = 0;
            }
        }
    }

private:
    std::array<uint16_t, numChannelNoteCombinations> soundingCount {};
};

/**
 * @brief Base class of every module in the chain.
 *
 * Modules run on the audio thread: process() must not allocate, lock or block. All memory a module
 * needs is reserved in its constructor.
 */
class MidiModule
{
public:
    explicit MidiModule (ModuleType type) : description (getModuleDescription (type)) {}
    virtual ~MidiModule() = default;

    MidiModule (const MidiModule&) = delete;
    MidiModule& operator= (const MidiModule&) = delete;

    /** Forgets all state (held notes, scheduled events) without sending anything. */
    virtual void reset() = 0;

    /**
     * Processes one block.
     * @param input            Events arriving at this module, sorted by sample position.
     * @param output           Receives this module's events. Does not need to be sorted.
     * @param context          Tempo, transport and timing of the block.
     * @param parameterValues  The four normalised (0..1) parameter values of the slot, A to D.
     */
    virtual void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                          const float* parameterValues) = 0;

    /** @return The description (name, parameters) of this module's type. */
    const ModuleDescription& getDescription() const noexcept { return description; }

protected:
    /** Reads parameter A..D (index 0..3) as a whole number in the parameter's plain range. */
    int integerParameter (const float* parameterValues, int parameterIndex) const noexcept
    {
        return normalisedToInteger (description.parameters[parameterIndex], parameterValues[parameterIndex]);
    }

    /** Reads parameter A..D (index 0..3) as a real number in the parameter's plain range. */
    float continuousParameter (const float* parameterValues, int parameterIndex) const noexcept
    {
        return normalisedToPlainValue (description.parameters[parameterIndex], parameterValues[parameterIndex]);
    }

    /** @return A random number in [0, 1). */
    float randomUnitValue() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (randomGenerator); }
    /** @return A random whole number in [lowest, highest]. */
    int randomInteger (int lowest, int highest)
    {
        return std::uniform_int_distribution<int> (lowest, highest) (randomGenerator);
    }

    const ModuleDescription& description;

private:
    std::minstd_rand randomGenerator { createSeed() };
    static unsigned createSeed() noexcept;
};

/**
 * Seeds the random generators of all modules created afterwards. The plug-in seeds from the clock;
 * the tests use fixed seeds so failures can be reproduced.
 */
void setRandomSeedForNewModules (unsigned seed) noexcept;

//==============================================================================
/**
 * @brief Remembers which output notes every incoming (channel, note) produced, so its note-off
 *        can release exactly those, even if the module's parameters changed in between.
 */
class NoteMappingTable
{
public:
    static constexpr int maxOutputNotesPerInput = 8;

    /** The output notes one incoming note produced. */
    struct OutputNotes
    {
        uint8_t count = 0;
        uint8_t channels[maxOutputNotesPerInput] {};
        uint8_t noteNumbers[maxOutputNotesPerInput] {};
    };

    OutputNotes& forInputNote (int channel, int noteNumber) noexcept
    {
        return table[channelNoteIndex (channel, noteNumber)];
    }
    void clear() noexcept { table.fill (OutputNotes {}); }

private:
    std::array<OutputNotes, numChannelNoteCombinations> table {};
};

/**
 * @brief Base for modules that decide everything at note-on time: filters, transforms, chord, randomizers.
 *
 * Subclasses only implement createNoteOns() (and optionally transformOtherEvent()); this class takes
 * care of matching note-offs, poly pressure and retriggered notes.
 */
class NoteMappingModule : public MidiModule
{
public:
    using MidiModule::MidiModule;

    void reset() override;
    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override;

protected:
    /**
     * Decides which notes to play for an incoming note-on.
     * @param noteOn          The incoming note-on.
     * @param outputNoteOns   Space for up to NoteMappingTable::maxOutputNotesPerInput note-ons.
     * @return How many note-ons were written; 0 blocks the note.
     */
    virtual int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) = 0;

    /**
     * Handles every channel message that is not a note (CC, pitch bend, ...), and poly pressure for
     * notes this module does not know. May modify the event.
     * @return False to drop the event.
     */
    virtual bool transformOtherEvent (MidiEvent& /*event*/, const float* /*parameterValues*/) { return true; }

private:
    void releaseOutputNotes (NoteMappingTable::OutputNotes& outputNotes, int samplePosition, int releaseVelocity,
                             MidiEventList& output);

    NoteMappingTable mappingTable;
    SoundingNoteCounter soundingNotes;
};

/**
 * @brief Base for modules that send events in the future: arpeggiator, echo, humanize.
 *
 * Future events are kept in a queue with absolute sample times and sent once their block arrives.
 */
class SchedulingModule : public MidiModule
{
public:
    explicit SchedulingModule (ModuleType type);

    void reset() override;

protected:
    /** Queues an event to be sent at an absolute sample time (see BlockContext::blockStartSample). */
    void scheduleEvent (int64_t sampleTime, const MidiEvent& event);

    /** Sends an event now, keeping track of which notes are sounding. */
    void sendEvent (const MidiEvent& event, MidiEventList& output);

    /** Sends every queued event due at or before the absolute sample time, in time order. */
    void sendScheduledEventsUpTo (int64_t sampleTime, const BlockContext& context, MidiEventList& output);

    /** Sends every queued event that falls into the current block. */
    void sendScheduledEventsInBlock (const BlockContext& context, MidiEventList& output)
    {
        sendScheduledEventsUpTo (context.blockStartSample + context.numSamples - 1, context, output);
    }

private:
    struct ScheduledEvent
    {
        int64_t sampleTime;
        uint32_t sequenceNumber; ///< Keeps events with equal times in the order they were scheduled.
        MidiEvent event;
    };

    static constexpr size_t maxScheduledEvents = 4096; ///< Hard cap so the queue never allocates.

    std::vector<ScheduledEvent> scheduledEvents, dueEvents;
    uint32_t nextSequenceNumber = 0;
    SoundingNoteCounter soundingNotes;
};

/** Creates a module of the given type, or nullptr for ModuleType::Empty. Not real-time safe. */
std::unique_ptr<MidiModule> createModule (ModuleType type);

} // namespace midiforge
