/**
 * @file TransformModules.cpp
 * @brief Modules that change events: Transpose, Scale Quantize, Velocity Curve, Channel Remap.
 */
#include "TransformModules.h"

#include <cmath>

namespace midiforge
{

int quantizeToScale (int noteNumber, int rootPitchClass, unsigned short semitoneMask, int snapDirection)
{
    enum SnapDirection
    {
        nearest,
        up,
        down
    };

    const auto isInScale = [&] (int candidate)
    {
        const int semitonesAboveRoot = ((candidate - rootPitchClass) % 12 + 12) % 12;
        return ((semitoneMask >> semitonesAboveRoot) & 1u) != 0;
    };

    if (semitoneMask == 0 || isInScale (noteNumber))
        return noteNumber;

    for (int distance = 1; distance < 12; ++distance)
    {
        const bool aboveFits = isInScale (noteNumber + distance);
        const bool belowFits = isInScale (noteNumber - distance);

        if (snapDirection == up)
        {
            if (aboveFits)
                return noteNumber + distance;
        }
        else if (snapDirection == down)
        {
            if (belowFits)
                return noteNumber - distance;
        }
        else if (belowFits) // nearest; on a tie the lower note wins
        {
            return noteNumber - distance;
        }
        else if (aboveFits)
        {
            return noteNumber + distance;
        }
    }
    return noteNumber;
}

int shapeVelocity (int velocity, float curve, int outputMinimum, int outputMaximum, int fixedVelocity)
{
    if (fixedVelocity > 0)
        return fixedVelocity;

    // Map 1..127 to 0..1, bend it with an exponent between 1/4 (curve -1) and 4 (curve +1),
    // then scale into the output range.
    const float position = (float) (std::clamp (velocity, 1, 127) - 1) / 126.0f;
    const float bent = std::pow (position, std::pow (4.0f, curve));
    const float scaled = (float) outputMinimum + bent * (float) (outputMaximum - outputMinimum);
    return std::clamp ((int) std::lround (scaled), 1, 127);
}

namespace
{

/** Shifts notes by a number of semitones plus octaves. Notes pushed outside 0..127 are dropped. */
class Transpose final : public NoteMappingModule
{
public:
    Transpose() : NoteMappingModule (ModuleType::Transpose) {}

private:
    enum Parameter
    {
        semitones,
        octaves
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        const int shift =
            integerParameter (parameterValues, semitones) + 12 * integerParameter (parameterValues, octaves);
        const int transposed = noteOn.noteNumber() + shift;
        if (transposed < 0 || transposed > 127)
            return 0;
        outputNoteOns[0] = noteOn;
        outputNoteOns[0].data1 = (uint8_t) transposed;
        return 1;
    }
};

/** Moves every note into the selected key and scale. */
class ScaleQuantize final : public NoteMappingModule
{
public:
    ScaleQuantize() : NoteMappingModule (ModuleType::ScaleQuantize) {}

private:
    enum Parameter
    {
        rootNote,
        scale,
        snapDirection
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        const int scaleIndex = std::clamp (integerParameter (parameterValues, scale), 0, numScales - 1);
        int quantized =
            quantizeToScale (noteOn.noteNumber(), integerParameter (parameterValues, rootNote),
                             scales[scaleIndex].semitoneMask, integerParameter (parameterValues, snapDirection));
        // Snapping can step over the MIDI range at the extremes; fold back by an octave.
        if (quantized > 127)
            quantized -= 12;
        if (quantized < 0)
            quantized += 12;

        outputNoteOns[0] = noteOn;
        outputNoteOns[0].data1 = (uint8_t) std::clamp (quantized, 0, 127);
        return 1;
    }
};

/** Reshapes note-on velocities (curve and output range) or replaces them with a fixed value. */
class VelocityCurve final : public NoteMappingModule
{
public:
    VelocityCurve() : NoteMappingModule (ModuleType::VelocityCurve) {}

private:
    enum Parameter
    {
        curve,
        outputMinimum,
        outputMaximum,
        fixedVelocity
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        outputNoteOns[0] = noteOn;
        outputNoteOns[0].data2 = (uint8_t) shapeVelocity (
            noteOn.velocity(), (float) integerParameter (parameterValues, curve) / 100.0f,
            integerParameter (parameterValues, outputMinimum), integerParameter (parameterValues, outputMaximum),
            integerParameter (parameterValues, fixedVelocity));
        return 1;
    }
};

/** Moves channel messages from one channel ("From", or all channels) to another ("To"). */
class ChannelRemap final : public NoteMappingModule
{
public:
    ChannelRemap() : NoteMappingModule (ModuleType::ChannelRemap) {}

private:
    enum Parameter
    {
        sourceChannel,
        targetChannel
    };
    static constexpr int allChannels = 0;

    void remap (MidiEvent& event, const float* parameterValues) const
    {
        const int source = integerParameter (parameterValues, sourceChannel); // 1-based, 0 = all
        if (source == allChannels || event.channel() == source - 1)
            event.setChannel (integerParameter (parameterValues, targetChannel) - 1);
    }

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        outputNoteOns[0] = noteOn;
        remap (outputNoteOns[0], parameterValues);
        return 1;
    }

    bool transformOtherEvent (MidiEvent& event, const float* parameterValues) override
    {
        remap (event, parameterValues);
        return true;
    }
};

} // namespace

std::unique_ptr<MidiModule> createTransformModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Transpose: return std::make_unique<Transpose>();
        case ModuleType::ScaleQuantize: return std::make_unique<ScaleQuantize>();
        case ModuleType::VelocityCurve: return std::make_unique<VelocityCurve>();
        case ModuleType::ChannelRemap: return std::make_unique<ChannelRemap>();
        default: return nullptr;
    }
}

} // namespace midiforge
