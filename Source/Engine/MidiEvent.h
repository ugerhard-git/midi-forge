/**
 * @file MidiEvent.h
 * @brief The small MIDI event type that flows through the module chain, plus the per-block host context.
 *
 * The engine (everything in Source/Engine) is plain C++17 without any JUCE dependency, so it can be
 * unit tested on any machine and reasoned about in isolation from the plug-in wrapper.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace midiforge
{

/** MIDI status nibbles (upper four bits of the status byte) of the channel messages we care about. */
namespace MidiStatus
{
constexpr uint8_t noteOff = 0x80;
constexpr uint8_t noteOn = 0x90;
constexpr uint8_t polyPressure = 0xA0;
constexpr uint8_t controlChange = 0xB0;
constexpr uint8_t programChange = 0xC0;
constexpr uint8_t channelPressure = 0xD0;
constexpr uint8_t pitchBend = 0xE0;
constexpr uint8_t firstSystemMessage = 0xF0;
} // namespace MidiStatus

constexpr int numMidiChannels = 16;
constexpr int numMidiNotes = 128;
constexpr int numChannelNoteCombinations = numMidiChannels * numMidiNotes;

/** Maps a (channel, note) pair to a unique index in [0, numChannelNoteCombinations). */
constexpr size_t channelNoteIndex (int channel, int noteNumber) noexcept
{
    return (size_t) (channel & 0x0F) * (size_t) numMidiNotes + (size_t) (noteNumber & 0x7F);
}

/**
 * @brief One short MIDI message (1 to 3 bytes) at a sample position inside the current audio block.
 *
 * SysEx and other long messages never enter the engine; the plug-in wrapper passes them through untouched.
 * Channels are zero based here (0..15), as on the wire.
 */
struct MidiEvent
{
    int samplePosition = 0; ///< Offset from the start of the current block, in samples.
    uint8_t status = 0;     ///< Status byte: message type in the upper nibble, channel in the lower nibble.
    uint8_t data1 = 0;      ///< First data byte (note number, controller number, ...).
    uint8_t data2 = 0;      ///< Second data byte (velocity, controller value, ...).
    uint8_t numBytes = 3;   ///< Length of the message on the wire (1..3).

    /** @return The message type, i.e. the status byte without the channel (e.g. MidiStatus::noteOn). */
    uint8_t messageType() const noexcept { return (uint8_t) (status & 0xF0); }
    /** @return The zero based MIDI channel (0..15). Only meaningful for channel messages. */
    int channel() const noexcept { return status & 0x0F; }
    /** @return The note number of a note or poly pressure message. */
    int noteNumber() const noexcept { return data1; }
    /** @return The velocity of a note message. */
    int velocity() const noexcept { return data2; }

    bool isChannelMessage() const noexcept { return status >= 0x80 && status < MidiStatus::firstSystemMessage; }
    /** A note-on with velocity 0 counts as a note-off, as the MIDI specification demands. */
    bool isNoteOn() const noexcept { return messageType() == MidiStatus::noteOn && data2 > 0; }
    bool isNoteOff() const noexcept
    {
        return messageType() == MidiStatus::noteOff || (messageType() == MidiStatus::noteOn && data2 == 0);
    }
    bool isNoteOnOrOff() const noexcept
    {
        return messageType() == MidiStatus::noteOn || messageType() == MidiStatus::noteOff;
    }
    bool isPolyPressure() const noexcept { return messageType() == MidiStatus::polyPressure; }
    bool isControlChange() const noexcept { return messageType() == MidiStatus::controlChange; }
    bool isProgramChange() const noexcept { return messageType() == MidiStatus::programChange; }
    bool isChannelPressure() const noexcept { return messageType() == MidiStatus::channelPressure; }
    bool isPitchBend() const noexcept { return messageType() == MidiStatus::pitchBend; }

    /** The centre (no bend) of the 14-bit pitch bend range 0..16383. */
    static constexpr int pitchBendCentre = 8192;
    static constexpr int maximumPitchBend = 16383;

    /** @return The 14-bit value (0..16383, centre 8192) of a pitch bend message. */
    int pitchBendValue() const noexcept { return (data1 & 0x7F) | ((data2 & 0x7F) << 7); }

    /** Moves a channel message to another zero based channel. */
    void setChannel (int newChannel) noexcept { status = (uint8_t) ((status & 0xF0) | (newChannel & 0x0F)); }

    /** Creates a note-on. The velocity is clamped to 1..127 so it can never turn into an implicit note-off. */
    static MidiEvent noteOn (int samplePosition, int channel, int noteNumber, int velocity) noexcept
    {
        return { samplePosition, (uint8_t) (MidiStatus::noteOn | (channel & 0x0F)), (uint8_t) (noteNumber & 0x7F),
                 (uint8_t) std::clamp (velocity, 1, 127), 3 };
    }

    /** Creates an explicit note-off message (status 0x80). */
    static MidiEvent noteOff (int samplePosition, int channel, int noteNumber, int releaseVelocity = 0) noexcept
    {
        return { samplePosition, (uint8_t) (MidiStatus::noteOff | (channel & 0x0F)), (uint8_t) (noteNumber & 0x7F),
                 (uint8_t) std::clamp (releaseVelocity, 0, 127), 3 };
    }

    /** Creates a pitch bend message. The value is clamped to 0..16383 (centre 8192). */
    static MidiEvent pitchBend (int samplePosition, int channel, int value) noexcept
    {
        const int clamped = std::clamp (value, 0, maximumPitchBend);
        return { samplePosition, (uint8_t) (MidiStatus::pitchBend | (channel & 0x0F)), (uint8_t) (clamped & 0x7F),
                 (uint8_t) (clamped >> 7), 3 };
    }

    /** Creates a control change message. */
    static MidiEvent controlChange (int samplePosition, int channel, int controllerNumber, int value) noexcept
    {
        return { samplePosition, (uint8_t) (MidiStatus::controlChange | (channel & 0x0F)),
                 (uint8_t) (controllerNumber & 0x7F), (uint8_t) (value & 0x7F), 3 };
    }
};

/** A block's worth of events. Kept sorted by samplePosition between modules. */
using MidiEventList = std::vector<MidiEvent>;

/** Sorts by sample position while keeping the original order of events that share a position. */
inline void sortBySamplePosition (MidiEventList& events)
{
    std::stable_sort (events.begin(), events.end(), [] (const MidiEvent& first, const MidiEvent& second)
                      { return first.samplePosition < second.samplePosition; });
}

/**
 * @brief Everything a module needs to know about the current audio block and the host transport.
 */
struct BlockContext
{
    double sampleRate = 44100.0;
    double tempoBpm = 120.0;
    bool isPlaying = false;               ///< True while the host transport runs.
    bool hasPpqPosition = false;          ///< True if the host reported a musical position.
    double ppqPositionAtBlockStart = 0.0; ///< Host position in quarter notes at sample 0 of this block.
    int numSamples = 0;                   ///< Length of the block.
    int64_t blockStartSample = 0;         ///< Running sample counter, maintained by the ModuleChain.

    /** @return How many samples one quarter note lasts at the current tempo. */
    double samplesPerBeat() const noexcept { return sampleRate * 60.0 / std::max (1.0, tempoBpm); }

    /** @return True if tempo-synced modules should follow the host's musical position. */
    bool followsHostTransport() const noexcept { return isPlaying && hasPpqPosition; }
};

} // namespace midiforge
