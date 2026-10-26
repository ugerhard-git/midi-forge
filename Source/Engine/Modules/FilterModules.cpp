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

} // namespace

std::unique_ptr<MidiModule> createFilterModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::NoteRange: return std::make_unique<NoteRangeFilter>();
        case ModuleType::VelocityRange: return std::make_unique<VelocityRangeFilter>();
        case ModuleType::ChannelFilter: return std::make_unique<ChannelFilter>();
        case ModuleType::EventBlocker: return std::make_unique<EventBlocker>();
        default: return nullptr;
    }
}

} // namespace midiforge
