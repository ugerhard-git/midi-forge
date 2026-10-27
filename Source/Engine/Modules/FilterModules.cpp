/**
 * @file FilterModules.cpp
 * @brief Modules that only decide whether an event passes: Note Range, Velocity Range, Channel Filter, Event Blocker.
 */
#include "FilterModules.h"

namespace midiforge
{
namespace
{

/** Passes or blocks notes depending on whether their key lies between Low and High. */
class NoteRangeFilter final : public NoteMappingModule
{
public:
    NoteRangeFilter() : NoteMappingModule (ModuleType::NoteRange) {}

private:
    enum Parameter
    {
        lowNote,
        highNote,
        mode
    };
    enum Mode
    {
        passInside,
        blockInside
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        const int low = integerParameter (parameterValues, lowNote);
        const int high = integerParameter (parameterValues, highNote);
        const bool isInside =
            noteOn.noteNumber() >= std::min (low, high) && noteOn.noteNumber() <= std::max (low, high);
        const bool passes = integerParameter (parameterValues, mode) == passInside ? isInside : ! isInside;
        if (! passes)
            return 0;
        outputNoteOns[0] = noteOn;
        return 1;
    }
};

/** Passes or blocks notes depending on whether their velocity lies between Min and Max. */
class VelocityRangeFilter final : public NoteMappingModule
{
public:
    VelocityRangeFilter() : NoteMappingModule (ModuleType::VelocityRange) {}

private:
    enum Parameter
    {
        minimumVelocity,
        maximumVelocity,
        mode
    };
    enum Mode
    {
        passInside,
        blockInside
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        const int low = integerParameter (parameterValues, minimumVelocity);
        const int high = integerParameter (parameterValues, maximumVelocity);
        const bool isInside = noteOn.velocity() >= std::min (low, high) && noteOn.velocity() <= std::max (low, high);
        const bool passes = integerParameter (parameterValues, mode) == passInside ? isInside : ! isInside;
        if (! passes)
            return 0;
        outputNoteOns[0] = noteOn;
        return 1;
    }
};

/** Keeps only one MIDI channel ("Only this") or removes it ("All except"). Applies to all channel messages. */
class ChannelFilter final : public NoteMappingModule
{
public:
    ChannelFilter() : NoteMappingModule (ModuleType::ChannelFilter) {}

private:
    enum Parameter
    {
        selectedChannel,
        mode
    };
    enum Mode
    {
        onlyThisChannel,
        allExceptThisChannel
    };

    bool channelPasses (int channel, const float* parameterValues) const
    {
        const bool isSelected = channel == integerParameter (parameterValues, selectedChannel) - 1; // shown 1-based
        return integerParameter (parameterValues, mode) == onlyThisChannel ? isSelected : ! isSelected;
    }

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        if (! channelPasses (noteOn.channel(), parameterValues))
            return 0;
        outputNoteOns[0] = noteOn;
        return 1;
    }

    bool transformOtherEvent (MidiEvent& event, const float* parameterValues) override
    {
        return channelPasses (event.channel(), parameterValues);
    }
};

/** Blocks whole classes of events: notes, control changes, pitch bend, aftertouch and program changes. */
class EventBlocker final : public NoteMappingModule
{
public:
    EventBlocker() : NoteMappingModule (ModuleType::EventBlocker) {}

private:
    enum Parameter
    {
        notes,
        controlChanges,
        pitchBend,
        aftertouchAndProgramChange
    };
    static constexpr int pass = 0;

    bool passes (const float* parameterValues, Parameter eventClass) const
    {
        return integerParameter (parameterValues, eventClass) == pass;
    }

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        if (! passes (parameterValues, notes))
            return 0;
        outputNoteOns[0] = noteOn;
        return 1;
    }

    bool transformOtherEvent (MidiEvent& event, const float* parameterValues) override
    {
        if (event.isControlChange())
            return passes (parameterValues, controlChanges);
        if (event.isPitchBend())
            return passes (parameterValues, pitchBend);
        if (event.isPolyPressure())
            return passes (parameterValues, notes) && passes (parameterValues, aftertouchAndProgramChange);
        if (event.isChannelPressure() || event.isProgramChange())
            return passes (parameterValues, aftertouchAndProgramChange);
        return true;
    }
};

/**
 * Splits the chain: notes in the selected range continue to the slots below; all other notes skip the
 * rest of the chain and go straight to the plug-in's output, unchanged. Everything that is not a note
 * (controllers, pitch bend, channel aftertouch, program changes, system messages) continues down the chain.
 *
 * The path is decided at note-on and remembered per key, so the note-off and poly aftertouch always
 * follow their note, even if the range changes (by hand or by an LFO) while the key is held.
 */
class NoteSplit final : public MidiModule
{
public:
    NoteSplit() : MidiModule (ModuleType::NoteSplit) {}

    void reset() override { pathOfHeldKey.fill (Path::none); }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        // Only reached if someone calls process() directly: then nothing can bypass, everything continues.
        static_cast<void> (context);
        static_cast<void> (parameterValues);
        output.insert (output.end(), input.begin(), input.end());
    }

    void processWithBypass (const MidiEventList& input, MidiEventList& output, MidiEventList& bypassOutput,
                            const BlockContext& /*context*/, const float* parameterValues) override
    {
        const int lowNote = integerParameter (parameterValues, lowNoteParameter);
        const int highNote = integerParameter (parameterValues, highNoteParameter);
        const bool everyOctave = integerParameter (parameterValues, octavesParameter) == everyOctaveChoice;

        for (const auto& event : input)
        {
            if (! event.isNoteOnOrOff() && ! event.isPolyPressure())
            {
                output.push_back (event); // non-note messages always continue down the chain
                continue;
            }

            auto& path = pathOfHeldKey[channelNoteIndex (event.channel(), event.noteNumber())];

            if (event.isNoteOn())
            {
                // Retriggered while held, and the key now goes the other way: end the old note first.
                const Path newPath = isNoteSelectedByNoteSplit (event.noteNumber(), lowNote, highNote, everyOctave)
                                         ? Path::chain
                                         : Path::bypass;
                if (path != Path::none && path != newPath)
                    sendNoteOff (path, MidiEvent::noteOff (event.samplePosition, event.channel(), event.noteNumber()),
                                 output, bypassOutput);
                path = newPath;
                (path == Path::chain ? output : bypassOutput).push_back (event);
            }
            else if (event.isNoteOff())
            {
                // Unknown keys (pressed before this module was active) continue down the chain (rule 1).
                sendNoteOff (path == Path::none ? Path::chain : path, event, output, bypassOutput);
                path = Path::none;
            }
            else // poly aftertouch follows its note
            {
                (path == Path::bypass ? bypassOutput : output).push_back (event);
            }
        }
    }

private:
    enum Parameter
    {
        lowNoteParameter,
        highNoteParameter,
        octavesParameter
    };
    static constexpr int everyOctaveChoice = 1;

    enum class Path : uint8_t
    {
        none,
        chain,
        bypass
    };

    /** Bypassed notes pass exactly as played, so their note-offs pass unchanged too (no counting). */
    static void sendNoteOff (Path path, const MidiEvent& noteOff, MidiEventList& output, MidiEventList& bypassOutput)
    {
        (path == Path::bypass ? bypassOutput : output).push_back (noteOff);
    }

    std::array<Path, numChannelNoteCombinations> pathOfHeldKey {};
};

} // namespace

bool isNoteSelectedByNoteSplit (int noteNumber, int lowNote, int highNote, bool everyOctave) noexcept
{
    const int low = std::min (lowNote, highNote);
    const int high = std::max (lowNote, highNote);
    if (! everyOctave)
        return noteNumber >= low && noteNumber <= high;

    const int span = high - low;
    if (span >= 11)
        return true; // the range covers a whole octave
    const int semitonesAboveLow = ((noteNumber - low) % 12 + 12) % 12;
    return semitonesAboveLow <= span;
}

std::unique_ptr<MidiModule> createFilterModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::NoteRange: return std::make_unique<NoteRangeFilter>();
        case ModuleType::VelocityRange: return std::make_unique<VelocityRangeFilter>();
        case ModuleType::ChannelFilter: return std::make_unique<ChannelFilter>();
        case ModuleType::EventBlocker: return std::make_unique<EventBlocker>();
        case ModuleType::NoteSplit: return std::make_unique<NoteSplit>();
        default: return nullptr;
    }
}

} // namespace midiforge
