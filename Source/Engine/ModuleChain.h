/**
 * @file ModuleChain.h
 * @brief The chain of eight slots that every block of MIDI flows through, from slot 1 to slot 8.
 */
#pragma once

#include "MidiModule.h"

#include <atomic>

namespace midiforge
{

constexpr int numSlots = 8;

/** The settings of one slot for one block, read from the host parameters by the plug-in. */
struct SlotSettings
{
    int moduleType = 0;                             ///< A ModuleType value.
    bool isEnabled = true;                          ///< A disabled slot passes everything through.
    float parameterValues[numParametersPerSlot] {}; ///< Normalised values of parameters A to H.
};

/**
 * @brief Note state of a MIDI stream as a synthesizer sees it: a note-on turns a key on,
 *        any note-off for that key turns it off.
 */
class NoteStateTracker
{
public:
    void clear() noexcept { noteOnCount.fill (0); }

    /** Updates the state with every note event in the list. */
    void track (const MidiEventList& events) noexcept
    {
        for (const auto& event : events)
        {
            auto& count = noteOnCount[channelNoteIndex (event.channel(), event.noteNumber())];
            if (event.isNoteOn() && count < UINT16_MAX)
                ++count;
            else if (event.isNoteOff())
                count = 0;
        }
    }

    bool isSounding (size_t channelNoteIndex) const noexcept { return noteOnCount[channelNoteIndex] > 0; }

private:
    std::array<uint16_t, numChannelNoteCombinations> noteOnCount {};
};

/**
 * @brief The last pitch bend of every channel in a MIDI stream (centre until one arrives).
 *
 * Lets the chain undo a bend that a module (Pitch Slide) left behind when its slot restarts.
 */
class PitchBendTracker
{
public:
    PitchBendTracker() noexcept { clear(); }
    void clear() noexcept { lastBend.fill ((uint16_t) MidiEvent::pitchBendCentre); }

    /** Updates the state with every pitch bend in the list. */
    void track (const MidiEventList& events) noexcept
    {
        for (const auto& event : events)
            if (event.isPitchBend())
                lastBend[(size_t) event.channel()] = (uint16_t) event.pitchBendValue();
    }

    /** @return The last pitch bend (0..16383) on a channel. */
    int bendOf (int channel) const noexcept { return lastBend[(size_t) (channel & 0x0F)]; }

private:
    std::array<uint16_t, numMidiChannels> lastBend {};
};

/**
 * @brief Runs MIDI through the eight slots and keeps notes consistent when slots change.
 *
 * When a slot changes (another module type, switched on or off) while notes are playing:
 *  - every note sounding at the slot's output gets a note-off, which travels through the downstream
 *    slots so they can close the notes they made from it;
 *  - note-offs that arrive later for notes that were sounding at the slot's input are swallowed,
 *    because those notes have already been released ("orphaned" notes).
 *
 * A module may also send events past the rest of the chain (Note Split, see MidiModule::processWithBypass).
 * Those events skip all slots below it and are merged into the chain's output at the end. When such a
 * slot restarts, the notes it sent past the chain are released too.
 *
 * Likewise, if a restarting slot's output has a different pitch bend than its input (a Pitch Slide in the
 * middle of a slide), the input's bend is sent, so the synth is not left out of tune.
 *
 * process() is real-time safe: all modules and buffers are allocated in the constructor.
 */
class ModuleChain
{
public:
    ModuleChain();

    /** Call before playback starts or when the sample rate changes. */
    void prepare (double sampleRate);

    /** Forgets all notes and scheduled events without sending anything. */
    void reset();

    /**
     * Runs one block of events through the chain, in place.
     * @param events        Input events (sorted by sample position); replaced by the output events.
     * @param context       Block information; blockStartSample is filled in by the chain.
     * @param slotSettings  numSlots entries describing the current slot configuration.
     */
    void process (MidiEventList& events, BlockContext context, const SlotSettings* slotSettings);

    /** Thread safe: the next process() call releases every sounding note and sends All Notes Off on all channels. */
    void requestPanic() noexcept { isPanicRequested.store (true); }

private:
    void handlePanic (MidiEventList& panicEvents);
    void restartSlot (int slotIndex, MidiEventList& slotOutput);
    void removeOrphanedNoteOffs (int slotIndex, MidiEventList& events);

    using ModulesOfOneSlot = std::array<std::unique_ptr<MidiModule>, numModuleTypes>;

    /** Every slot owns one module of every type, so switching type never allocates on the audio thread. */
    std::array<ModulesOfOneSlot, numSlots> modulesBySlot;
    std::array<int, numSlots> activeModuleType {}; ///< 0 means empty or disabled.
    std::array<NoteStateTracker, numSlots> notesAtSlotInput;
    std::array<NoteStateTracker, numSlots> notesAtSlotOutput;
    std::array<std::array<bool, numChannelNoteCombinations>, numSlots> isOrphanedAtSlotInput {};
    NoteStateTracker notesAtChainOutput;
    std::array<NoteStateTracker, numSlots> notesBypassedBySlot; ///< Notes each slot sent past the chain
    std::array<PitchBendTracker, numSlots> bendsAtSlotInput, bendsAtSlotOutput;

    MidiEventList slotInput, slotOutput, moduleOutput, moduleBypassOutput, bypassedEvents, panicEvents;
    int64_t samplesProcessed = 0;
    std::atomic<bool> isPanicRequested { false };
};

} // namespace midiforge
