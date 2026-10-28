/**
 * @file TransformModules.cpp
 * @brief Modules that change events: Transpose, Scale Quantize, Velocity Curve, Channel Remap,
 *        Note Dispatcher, Groove, Diatonic Harmonizer.
 */
#include "TransformModules.h"

#include <cmath>
#include <initializer_list>
#include <limits>

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

/**
 * Sends incoming notes to a range of channels, picking a free channel for every note (or every chord).
 * A channel is free when no note this module sent to it is still held. When all are busy, the channel
 * whose notes started longest ago is freed (its notes end) and reused.
 *
 * Every input key remembers the channel its note went to, so its note-off and poly pressure follow, even
 * if the knobs change while it is held. Other channel messages of the dispatched input are copied to every
 * target channel, so all synths follow the wheels and pedals.
 */
class NoteDispatcher final : public MidiModule
{
public:
    NoteDispatcher() : MidiModule (ModuleType::NoteDispatcher) { reset(); }

    void reset() override
    {
        channelOfInputKey.fill (notMapped);
        numNotesOnChannel.fill (0);
        startOrderOfChannel.fill (0);
        nextStartOrder = 1;
        lastPosition = -1;
        isPingPongGoingDown = false;
        chordStartTime = std::numeric_limits<int64_t>::min() / 2;
        chordChannel = -1;
        soundingNotes.clear();
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const int inputSetting = integerParameter (parameterValues, inputChannel); // 0 = all
        for (const auto& event : input)
        {
            const bool isFromDispatchedInput =
                event.isChannelMessage() && (inputSetting == 0 || event.channel() == inputSetting - 1);
            if (event.isNoteOn())
            {
                // A key played again ends whatever its previous note-on started (a synth would do the same).
                endKey (event.channel(), event.noteNumber(), event.samplePosition, output);
                if (isFromDispatchedInput)
                {
                    dispatchNoteOn (event, context.blockStartSample + event.samplePosition, context, parameterValues,
                                    output);
                }
                else
                {
                    keyState (event) = passedThrough;
                    output.push_back (event);
                }
            }
            else if (event.isNoteOff())
            {
                // Note-offs follow their note-on, whatever the Input knob says now.
                handleNoteOff (event, output);
            }
            else if (event.isPolyPressure())
            {
                handlePolyPressure (event, output);
            }
            else if (isFromDispatchedInput)
            {
                copyToEveryChannel (event, parameterValues, output);
            }
            else
            {
                output.push_back (event);
            }
        }
    }

private:
    enum Parameter
    {
        algorithm,
        dispatchMode,
        firstChannel,
        numberOfChannels,
        inputChannel,   ///< page 2
        chordWindowInMs ///< page 2
    };
    enum Algorithm
    {
        roundRobin,
        random,
        pingPong
    };
    enum DispatchMode
    {
        singleNotes,
        chords
    };

    // What happened to an input key's note (otherwise: the zero-based channel it was dispatched to).
    static constexpr int16_t notMapped = -1;     ///< Nothing known (a note from before this module): note-off passes
    static constexpr int16_t wasStolen = -2;     ///< Its note was ended to free the channel: its note-off is dropped
    static constexpr int16_t passedThrough = -3; ///< Not on the Input channel: passed unchanged, and so is its note-off

    int16_t& keyState (const MidiEvent& event) noexcept
    {
        return channelOfInputKey[channelNoteIndex (event.channel(), event.noteNumber())];
    }

    /** Ends the note an input key is still sounding (dispatched or passed through), before it plays again. */
    void endKey (int channelOfKey, int noteNumber, int samplePosition, MidiEventList& output)
    {
        auto& state = channelOfInputKey[channelNoteIndex (channelOfKey, noteNumber)];
        if (state >= 0)
            endNote (noteNumber, state, samplePosition, output);
        else if (state == passedThrough)
            output.push_back (MidiEvent::noteOff (samplePosition, channelOfKey, noteNumber));
        state = notMapped;
    }

    /** The channels in use: [first, first + count), zero based, never past channel 16. */
    struct ChannelRange
    {
        int first;
        int count;
    };
    ChannelRange channelRange (const float* parameterValues) const noexcept
    {
        const int first = std::clamp (integerParameter (parameterValues, firstChannel) - 1, 0, numMidiChannels - 1);
        const int count = std::clamp (integerParameter (parameterValues, numberOfChannels), 1, numMidiChannels - first);
        return { first, count };
    }

    void dispatchNoteOn (const MidiEvent& noteOn, int64_t eventTime, const BlockContext& context,
                         const float* parameterValues, MidiEventList& output)
    {
        auto& keyChannel = keyState (noteOn); // notMapped: endKey() ran before

        const auto range = channelRange (parameterValues);
        const bool isChordMode = integerParameter (parameterValues, dispatchMode) == chords;
        const auto chordWindow =
            (int64_t) (integerParameter (parameterValues, chordWindowInMs) / 1000.0 * context.sampleRate);
        const bool joinsChord = isChordMode && chordChannel >= 0 && eventTime - chordStartTime <= chordWindow;

        int channel = joinsChord ? chordChannel
                                 : pickChannel (range, integerParameter (parameterValues, algorithm),
                                                noteOn.samplePosition, output);
        if (! joinsChord)
        {
            chordStartTime = eventTime;
            chordChannel = channel;
            startOrderOfChannel[(size_t) channel] = nextStartOrder++;
        }

        keyChannel = (int16_t) channel;
        ++numNotesOnChannel[(size_t) channel];
        auto dispatched = noteOn;
        dispatched.setChannel (channel);
        soundingNotes.noteStarted (channel, noteOn.noteNumber());
        output.push_back (dispatched);
    }

    /** Picks a free channel with the chosen algorithm, or frees the one used longest ago. */
    int pickChannel (const ChannelRange& range, int chosenAlgorithm, int samplePosition, MidiEventList& output)
    {
        const auto isFree = [&] (int position)
        { return numNotesOnChannel[(size_t) range.first + (size_t) position] == 0; };
        int position = -1;

        if (chosenAlgorithm == random)
        {
            int freePositions[numMidiChannels];
            int numFree = 0;
            for (int candidate = 0; candidate < range.count; ++candidate)
                if (isFree (candidate))
                    freePositions[numFree++] = candidate;
            if (numFree > 0)
                position = freePositions[randomInteger (0, numFree - 1)];
        }
        else if (chosenAlgorithm == pingPong)
        {
            // Walk up and down from the last position, turning at the ends without repeating them.
            int candidate = std::clamp (lastPosition, -1, range.count - 1);
            bool goingDown = isPingPongGoingDown;
            for (int step = 0; step < 2 * range.count && position < 0; ++step)
            {
                if (range.count == 1)
                {
                    candidate = 0;
                }
                else if (goingDown && candidate <= 0)
                {
                    goingDown = false; // bottom reached: turn up
                    candidate = 1;
                }
                else if (! goingDown && candidate >= range.count - 1)
                {
                    goingDown = true; // top reached: turn down
                    candidate = range.count - 2;
                }
                else
                {
                    candidate += goingDown ? -1 : 1;
                }
                if (isFree (candidate))
                    position = candidate;
            }
            if (position >= 0)
                isPingPongGoingDown = goingDown;
        }
        else
        {
            for (int step = 1; step <= range.count && position < 0; ++step)
            {
                const int candidate = (std::max (lastPosition, -1) + step) % range.count;
                if (isFree (candidate))
                    position = candidate;
            }
        }

        if (position < 0)
            position = stealOldestChannel (range, samplePosition, output);
        lastPosition = position;
        return range.first + position;
    }

    /** Ends every note on the channel whose notes started first, and returns its position in the range. */
    int stealOldestChannel (const ChannelRange& range, int samplePosition, MidiEventList& output)
    {
        int oldestPosition = 0;
        for (int position = 1; position < range.count; ++position)
            if (startOrderOfChannel[(size_t) range.first + (size_t) position] <
                startOrderOfChannel[(size_t) range.first + (size_t) oldestPosition])
                oldestPosition = position;

        const int channel = range.first + oldestPosition;
        for (size_t key = 0; key < channelOfInputKey.size(); ++key)
        {
            if (channelOfInputKey[key] == channel)
            {
                endNote ((int) key % numMidiNotes, channel, samplePosition, output);
                channelOfInputKey[key] = wasStolen; // its note-off is swallowed when it comes
            }
        }
        return oldestPosition;
    }

    void handleNoteOff (const MidiEvent& noteOff, MidiEventList& output)
    {
        auto& keyChannel = keyState (noteOff);
        if (keyChannel >= 0)
            endNote (noteOff.noteNumber(), keyChannel, noteOff.samplePosition, output, noteOff.velocity());
        else if (keyChannel != wasStolen)
            output.push_back (noteOff); // passed through, or from before this module was there (rule 1)
        keyChannel = notMapped;
    }

    /** Sends the note-off of a dispatched note and frees its place on the channel. */
    void endNote (int noteNumber, int channel, int samplePosition, MidiEventList& output, int releaseVelocity = 0)
    {
        auto& count = numNotesOnChannel[(size_t) channel];
        count = std::max (0, count - 1);
        if (soundingNotes.noteEnded (channel, noteNumber))
            output.push_back (MidiEvent::noteOff (samplePosition, channel, noteNumber, releaseVelocity));
    }

    void handlePolyPressure (const MidiEvent& pressure, MidiEventList& output)
    {
        const int keyChannel = keyState (pressure);
        if (keyChannel == wasStolen)
            return;
        auto followed = pressure;
        if (keyChannel >= 0)
            followed.setChannel (keyChannel);
        output.push_back (followed);
    }

    void copyToEveryChannel (const MidiEvent& event, const float* parameterValues, MidiEventList& output)
    {
        const auto range = channelRange (parameterValues);
        for (int position = 0; position < range.count; ++position)
        {
            auto copy = event;
            copy.setChannel (range.first + position);
            output.push_back (copy);
        }
    }

    std::array<int16_t, numChannelNoteCombinations> channelOfInputKey {};
    std::array<int, numMidiChannels> numNotesOnChannel {};
    std::array<uint64_t, numMidiChannels> startOrderOfChannel {}; ///< When a channel last got a note (or chord)
    uint64_t nextStartOrder = 1;
    int lastPosition = -1; ///< Position (in the range) of the channel used last
    bool isPingPongGoingDown = false;
    int64_t chordStartTime = 0;
    int chordChannel = -1;
    SoundingNoteCounter soundingNotes;
};

/**
 * Pulls notes onto a groove template. Each note snaps (by Amount) towards the groove position of its nearest
 * grid step and gets the template's accent (by Velocity). A plug-in cannot play a note before it arrives, so
 * every note is delayed by Early and pulled earlier from there; with Early 0 grooves can only drag notes.
 * The note-off of a note is delayed exactly as much as its note-on, so note lengths stay.
 */
class Groove final : public SchedulingModule
{
public:
    Groove() : SchedulingModule (ModuleType::Groove) { delayOfHeldNote.fill (noDelayStored); }

    void reset() override
    {
        SchedulingModule::reset();
        delayOfHeldNote.fill (noDelayStored);
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const int64_t earlyDelay =
            std::llround (integerParameter (parameterValues, earlyMilliseconds) / 1000.0 * context.sampleRate);
        for (const auto& event : input)
        {
            const int64_t eventTime = context.blockStartSample + event.samplePosition;
            sendScheduledEventsUpTo (eventTime, context, output);

            if (! event.isNoteOnOrOff())
            {
                // Everything else is delayed like the notes, so controllers stay in step with them.
                if (earlyDelay > 0)
                    scheduleEvent (eventTime + earlyDelay, event);
                else
                    sendEvent (event, output);
                continue;
            }

            auto& storedDelay = delayOfHeldNote[channelNoteIndex (event.channel(), event.noteNumber())];
            if (event.isNoteOn())
            {
                if (storedDelay != noDelayStored) // retriggered: end the previous note first
                    scheduleEvent (eventTime + storedDelay,
                                   MidiEvent::noteOff (0, event.channel(), event.noteNumber()));

                int velocityChange = 0;
                const int64_t shift = context.followsHostTransport()
                                          ? grooveShift (context, event.samplePosition, parameterValues, velocityChange)
                                          : 0;
                storedDelay = std::max<int64_t> (0, earlyDelay + shift);
                MidiEvent noteOn = event;
                noteOn.data2 = (uint8_t) std::clamp (event.velocity() + velocityChange, 1, 127);
                scheduleEvent (eventTime + storedDelay, noteOn);
            }
            else if (storedDelay != noDelayStored)
            {
                scheduleEvent (eventTime + storedDelay, event);
                storedDelay = noDelayStored;
            }
            else
            {
                sendEvent (event, output); // a note that started before this module was active
            }
        }
        sendScheduledEventsInBlock (context, output);
    }

    /** One position of a groove template: timing offset (in grid steps, + = late) and velocity accent. */
    struct GroovePoint
    {
        double offsetInSteps;
        int velocityAccent;
    };

    /**
     * @return The template's timing and accent for a grid step.
     * @param stepInBar    The grid step counted from the start of the bar.
     * @param stepsPerBeat Grid steps per quarter note (2, 4 or 8).
     * @param bar          The beat structure of the bar; Laid-back Snare delays its backbeats (2 and 4 in 4/4,
     *                     the second group in 6/8, groups 2 and 3 in 7/8 as 2+2+3).
     */
    static GroovePoint groovePoint (int grooveTemplate, int64_t stepInBar, int stepsPerBeat,
                                    const BarStructure& bar) noexcept
    {
        const auto stepInBeat = (int) (stepInBar % stepsPerBeat);
        const bool isOffStep = (stepInBar % 2) != 0;
        switch (grooveTemplate)
        {
            case swing: return { isOffStep ? 0.2 : 0.0, isOffStep ? -10 : 8 }; // 60 % swing
            case laidBackSnare:
            {
                const double positionInBar = (double) stepInBar / stepsPerBeat;
                const double countPosition = positionInBar / bar.getTimeSignature().countLengthInQuarters();
                const bool isOnCount = std::abs (countPosition - std::round (countPosition)) < 1.0e-6;
                const bool isSnareBeat = isOnCount && bar.isBackbeat (bar.countAt (positionInBar));
                return { isSnareBeat ? 0.2 : 0.0, isSnareBeat ? 12 : (isOnCount ? 4 : -4) };
            }
            case pushedOffBeats:
            {
                const bool isOffBeat = stepInBeat == stepsPerBeat / 2;
                return { isOffBeat ? -0.15 : 0.0, isOffBeat ? 10 : (stepInBeat == 0 ? 0 : -6) };
            }
            case shuffle: return { isOffStep ? 1.0 / 3.0 : 0.0, isOffStep ? -14 : 10 }; // triplet feel
            case drunk:
            {
                static constexpr GroovePoint pattern[] = { { -0.12, 10 }, { 0.18, -10 }, { -0.06, 2 }, { 0.22, -14 } };
                return pattern[stepInBar % 4];
            }
            default: return { 0.0, 0 };
        }
    }

private:
    enum Parameter
    {
        grooveTemplateIndex,
        amountPercent,
        gridIndex,
        velocityPercent,
        earlyMilliseconds ///< page 2
    };
    enum GrooveTemplate
    {
        swing,
        laidBackSnare,
        pushedOffBeats,
        shuffle,
        drunk
    };
    static constexpr int64_t noDelayStored = -1;

    /** @return How many samples the groove moves a note (negative = earlier), and its velocity change. */
    int64_t grooveShift (const BlockContext& context, int samplePosition, const float* parameterValues,
                         int& velocityChange) const noexcept
    {
        static constexpr int stepsPerBeatOfGrid[] = { 2, 4, 8 }; // 1/8, 1/16, 1/32
        const int stepsPerBeat = stepsPerBeatOfGrid[std::clamp (integerParameter (parameterValues, gridIndex), 0, 2)];
        const double stepLengthInBeats = 1.0 / stepsPerBeat;

        // Steps count from the start of the bar, so the groove repeats every bar in any time signature.
        const double beat = context.beatAtSample (samplePosition);
        const auto bar = context.barPositionAtBeat (beat);
        auto stepInBar = (int64_t) std::llround (bar.positionInBar / stepLengthInBeats);
        double stepPositionInBar = (double) stepInBar * stepLengthInBeats;
        if (stepPositionInBar >= bar.barLength - 1.0e-9) // nearest step is the first one of the next bar
        {
            stepInBar = 0;
            stepPositionInBar = bar.barLength;
        }
        const BarStructure barStructure (context.timeSignature);
        const auto point = groovePoint (integerParameter (parameterValues, grooveTemplateIndex), stepInBar,
                                        stepsPerBeat, barStructure);

        const double amount = integerParameter (parameterValues, amountPercent) / 100.0;
        const double targetBeat =
            beat - bar.positionInBar + stepPositionInBar + point.offsetInSteps * stepLengthInBeats;
        const double shiftInBeats = (targetBeat - beat) * amount;
        velocityChange =
            (int) std::lround (point.velocityAccent * integerParameter (parameterValues, velocityPercent) / 100.0);
        return std::llround (shiftInBeats * context.samplesPerBeat());
    }

    std::array<int64_t, numChannelNoteCombinations> delayOfHeldNote {};
};

/**
 * Adds up to two harmony notes to every single note, taken from a scale (Root, Scale), with a share of its
 * velocity. Intervals count notes of the scale; octaves are 12 semitones. Notes starting within
 * chordWindowMilliseconds of the previous note are chord notes and pass alone; because a plug-in cannot look
 * ahead, the harmony notes of the chord's first note are ended when the second one arrives.
 */
class DiatonicHarmonizer final : public MidiModule
{
public:
    DiatonicHarmonizer() : MidiModule (ModuleType::DiatonicHarmonizer) { reset(); }

    void reset() override
    {
        mappingTable.clear();
        soundingNotes.clear();
        lastNoteOnTime = std::numeric_limits<int64_t>::min() / 2;
        lastChannel = lastNoteNumber = 0;
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        for (const auto& event : input)
        {
            if (event.isNoteOn())
                handleNoteOn (event, context, parameterValues, output);
            else if (event.isNoteOff())
            {
                auto& outputNotes = mappingTable.forInputNote (event.channel(), event.noteNumber());
                if (outputNotes.count == 0)
                    output.push_back (event); // a note from before this module was there (rule 1)
                else
                    releaseNotes (outputNotes, 0, event.samplePosition,
                                  event.messageType() == MidiStatus::noteOff ? event.velocity() : 0, output);
            }
            else if (event.isPolyPressure())
            {
                const auto& outputNotes = mappingTable.forInputNote (event.channel(), event.noteNumber());
                if (outputNotes.count == 0)
                    output.push_back (event);
                for (int index = 0; index < outputNotes.count; ++index)
                {
                    auto pressure = event;
                    pressure.data1 = outputNotes.noteNumbers[index];
                    output.push_back (pressure);
                }
            }
            else
                output.push_back (event);
        }
    }

    /**
     * @return The harmony note for a Voice knob position, or -1 for Off or when it would leave the MIDI range.
     * @param intervalIndex Index into the Voice labels: 0 = octave down, 7 = Off, 14 = octave up; in between
     *                      the number of scale notes below (index < 7) or above (index > 7).
     */
    static int harmonyNote (int noteNumber, int intervalIndex, int root, unsigned short semitoneMask) noexcept
    {
        const int steps = intervalIndex - offIntervalIndex;
        if (steps == 0)
            return -1;
        int harmony = noteNumber;
        if (steps == -octaveIntervalSteps || steps == octaveIntervalSteps)
            harmony = noteNumber + (steps > 0 ? 12 : -12);
        else
            harmony = moveByScaleSteps (noteNumber, steps, root, semitoneMask);
        return (harmony == noteNumber || harmony < 0 || harmony > 127) ? -1 : harmony;
    }

private:
    enum Parameter
    {
        voice1Interval,
        voice2Interval,
        rootPitchClass,
        scaleIndex,
        velocityPercent ///< page 2
    };
    static constexpr int offIntervalIndex = 7;
    static constexpr int octaveIntervalSteps = 7;
    static constexpr double chordWindowMilliseconds = 30.0;

    void handleNoteOn (const MidiEvent& noteOn, const BlockContext& context, const float* parameterValues,
                       MidiEventList& output)
    {
        auto& outputNotes = mappingTable.forInputNote (noteOn.channel(), noteOn.noteNumber());
        if (outputNotes.count > 0) // played again without a note-off: end what it started
            releaseNotes (outputNotes, 0, noteOn.samplePosition, 0, output);

        const int64_t eventTime = context.blockStartSample + noteOn.samplePosition;
        const auto chordWindow = (int64_t) (chordWindowMilliseconds / 1000.0 * context.sampleRate);
        const bool isChordNote = eventTime - lastNoteOnTime <= chordWindow;
        if (isChordNote) // the previous note turned out to start a chord: it plays alone, too
            releaseNotes (mappingTable.forInputNote (lastChannel, lastNoteNumber), 1, noteOn.samplePosition, 0, output);
        lastNoteOnTime = eventTime;
        lastChannel = noteOn.channel();
        lastNoteNumber = noteOn.noteNumber();

        startNote (outputNotes, noteOn.channel(), noteOn.noteNumber(), noteOn.velocity(), noteOn.samplePosition,
                   output);
        if (isChordNote)
            return;

        const int root = std::clamp (integerParameter (parameterValues, rootPitchClass), 0, 11);
        const auto mask =
            scales[std::clamp (integerParameter (parameterValues, scaleIndex), 0, numScales - 1)].semitoneMask;
        const int velocity = std::max (
            1, (int) std::lround (noteOn.velocity() * integerParameter (parameterValues, velocityPercent) / 100.0));
        for (const auto voice : { voice1Interval, voice2Interval })
        {
            const int harmony =
                harmonyNote (noteOn.noteNumber(), integerParameter (parameterValues, voice), root, mask);
            const bool isNew = harmony >= 0 && ! contains (outputNotes, harmony);
            if (isNew)
                startNote (outputNotes, noteOn.channel(), harmony, velocity, noteOn.samplePosition, output);
        }
    }

    static bool contains (const NoteMappingTable::OutputNotes& outputNotes, int noteNumber) noexcept
    {
        for (int index = 0; index < outputNotes.count; ++index)
            if (outputNotes.noteNumbers[index] == noteNumber)
                return true;
        return false;
    }

    void startNote (NoteMappingTable::OutputNotes& outputNotes, int channel, int noteNumber, int velocity,
                    int samplePosition, MidiEventList& output)
    {
        outputNotes.channels[outputNotes.count] = (uint8_t) channel;
        outputNotes.noteNumbers[outputNotes.count] = (uint8_t) noteNumber;
        ++outputNotes.count;
        soundingNotes.noteStarted (channel, noteNumber);
        output.push_back (MidiEvent::noteOn (samplePosition, channel, noteNumber, velocity));
    }

    /** Ends the output notes of an input note from index `first` on (0 = all, 1 = only the harmony notes). */
    void releaseNotes (NoteMappingTable::OutputNotes& outputNotes, int first, int samplePosition, int releaseVelocity,
                       MidiEventList& output)
    {
        for (int index = first; index < outputNotes.count; ++index)
            if (soundingNotes.noteEnded (outputNotes.channels[index], outputNotes.noteNumbers[index]))
                output.push_back (MidiEvent::noteOff (samplePosition, outputNotes.channels[index],
                                                      outputNotes.noteNumbers[index], releaseVelocity));
        outputNotes.count = (uint8_t) std::min<int> (outputNotes.count, first);
    }

    NoteMappingTable mappingTable;
    SoundingNoteCounter soundingNotes;
    int64_t lastNoteOnTime = 0;
    int lastChannel = 0;
    int lastNoteNumber = 0;
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
        case ModuleType::NoteDispatcher: return std::make_unique<NoteDispatcher>();
        case ModuleType::Groove: return std::make_unique<Groove>();
        case ModuleType::DiatonicHarmonizer: return std::make_unique<DiatonicHarmonizer>();
        default: return nullptr;
    }
}

} // namespace midiforge
