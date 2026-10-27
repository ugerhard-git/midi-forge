/**
 * @file RandomizerModules.cpp
 * @brief Modules that add controlled randomness: Humanize, Probability, Random Pitch, Pitch Slide, Mutate.
 */
#include "RandomizerModules.h"

#include <cmath>
#include <limits>

namespace midiforge
{
namespace
{

/**
 * Delays every note by a random amount (up to Timing) and varies its velocity (by up to ±Velocity).
 * A note's note-off is delayed by the same amount as its note-on, so note lengths stay intact.
 * Notes can only be delayed, never moved earlier, because a plug-in cannot see the future.
 */
class Humanize final : public SchedulingModule
{
public:
    Humanize() : SchedulingModule (ModuleType::Humanize) { delayOfHeldNote.fill (noDelayStored); }

    void reset() override
    {
        SchedulingModule::reset();
        delayOfHeldNote.fill (noDelayStored);
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const double maximumDelayInSamples =
            continuousParameter (parameterValues, timing) * context.sampleRate / 1000.0;
        const int velocityRange = integerParameter (parameterValues, velocityVariation);

        for (const auto& event : input)
        {
            const int64_t eventTime = context.blockStartSample + event.samplePosition;
            sendScheduledEventsUpTo (eventTime, context, output);

            if (! event.isNoteOnOrOff())
            {
                sendEvent (event, output);
                continue;
            }

            auto& storedDelay = delayOfHeldNote[channelNoteIndex (event.channel(), event.noteNumber())];
            if (event.isNoteOn())
            {
                if (storedDelay != noDelayStored) // retriggered: end the previous note first
                    scheduleEvent (eventTime + storedDelay,
                                   MidiEvent::noteOff (0, event.channel(), event.noteNumber()));

                storedDelay = std::llround (randomUnitValue() * maximumDelayInSamples);
                MidiEvent noteOn = event;
                const int velocityChange = velocityRange > 0 ? randomInteger (-velocityRange, velocityRange) : 0;
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

private:
    enum Parameter
    {
        timing,
        velocityVariation
    };
    static constexpr int64_t noDelayStored = -1;

    std::array<int64_t, numChannelNoteCombinations> delayOfHeldNote {};
};

/** Lets each note through with the given chance; the rest are silently dropped. */
class Probability final : public NoteMappingModule
{
public:
    Probability() : NoteMappingModule (ModuleType::Probability) {}

private:
    enum Parameter
    {
        chancePercent
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        if (randomUnitValue() * 100.0f >= (float) integerParameter (parameterValues, chancePercent))
            return 0;
        outputNoteOns[0] = noteOn;
        return 1;
    }
};

/** Shifts some notes (Chance) by a random interval within Range: any interval, octaves, or fifths and octaves. */
class RandomPitch final : public NoteMappingModule
{
public:
    RandomPitch() : NoteMappingModule (ModuleType::RandomPitch) {}

private:
    enum Parameter
    {
        rangeInSemitones,
        chancePercent,
        intervalMode
    };
    enum IntervalMode
    {
        anyInterval,
        octavesOnly,
        fifthsAndOctaves
    };

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        outputNoteOns[0] = noteOn;

        const int range = integerParameter (parameterValues, rangeInSemitones);
        const bool shouldShift = randomUnitValue() * 100.0f < (float) integerParameter (parameterValues, chancePercent);
        if (range <= 0 || ! shouldShift)
            return 1;

        const int shiftedNote =
            noteOn.noteNumber() + pickShift (range, integerParameter (parameterValues, intervalMode));
        if (shiftedNote >= 0 && shiftedNote <= 127)
            outputNoteOns[0].data1 = (uint8_t) shiftedNote;
        return 1;
    }

    /** @return A random shift in semitones, at most range away from the original note. */
    int pickShift (int range, int mode)
    {
        if (mode == anyInterval)
            return randomInteger (-range, range);

        if (mode == octavesOnly)
        {
            const int maxOctaves = range / 12;
            if (maxOctaves == 0)
                return 0;
            const int direction = randomInteger (0, 1) == 0 ? -1 : 1;
            return direction * 12 * randomInteger (1, maxOctaves);
        }

        int candidates[8];
        int numCandidates = 0;
        for (int interval : { 7, 12, 19, 24 })
        {
            if (interval <= range)
            {
                candidates[numCandidates++] = interval;
                candidates[numCandidates++] = -interval;
            }
        }
        return numCandidates > 0 ? candidates[randomInteger (0, numCandidates - 1)] : 0;
    }
};

/**
 * Slides the pitch of notes with pitch bend: Slide In starts a note at Offset and glides to its own pitch,
 * Slide Out glides from the note's pitch to Offset. Notes themselves pass through untouched and on time.
 *
 * Pitch bend acts on a whole channel, so everything is kept per channel: notes starting within
 * chordWindowMilliseconds of a slide's first note share its slide (one Chance decision per chord). A later
 * note starts a new slide (or, if it doesn't slide, ends the current one). When the last note of a channel
 * ends, the bend goes back to the player's own bend. The player's bend is always added to the slide.
 *
 * While a slide moves, a bend message is sent about every millisecond (only when the value changes).
 */
class PitchSlide final : public MidiModule
{
public:
    PitchSlide() : MidiModule (ModuleType::PitchSlide) { reset(); }

    void reset() override
    {
        channels.fill (ChannelState {});
        isKeyHeld.fill (false);
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const int bendRange = std::max (1, integerParameter (parameterValues, bendRangeInSemitones));
        for (const auto& event : input)
        {
            const int64_t eventTime = context.blockStartSample + event.samplePosition;
            sendSlideUpdatesBefore (eventTime, bendRange, context, output);
            handleEvent (event, eventTime, bendRange, context, parameterValues, output);
        }
        sendSlideUpdatesBefore (context.blockStartSample + context.numSamples, bendRange, context, output);
    }

private:
    enum Parameter
    {
        offsetInSemitones,
        slideMode,
        slideLength,
        chancePercent,
        curveAmount,          ///< page 2
        bendRangeInSemitones, ///< page 2
        slideDelay            ///< page 2
    };
    enum SlideMode
    {
        slideIn,
        slideOut
    };

    static constexpr double chordWindowMilliseconds = 30.0;
    static constexpr double updatesPerSecond = 1000.0;
    static constexpr int64_t longAgo = std::numeric_limits<int64_t>::min() / 2;

    /** The slide state of one MIDI channel. */
    struct ChannelState
    {
        int numHeldKeys = 0;
        int playerBend = MidiEvent::pitchBendCentre; ///< The last bend that arrived at the module
        int lastSentBend = MidiEvent::pitchBendCentre;
        int64_t lastSlideStartTime = longAgo; ///< When the current chord's first note arrived
        bool hasSlide = false;                ///< A slide applies to the sounding notes
        bool isMoving = false;                ///< The slide is still waiting or gliding (needs updates)
        float fromSemitones = 0.0f, toSemitones = 0.0f;
        float curveExponent = 1.0f;
        int64_t moveStartTime = 0, moveEndTime = 0, nextUpdateTime = 0;
    };

    void handleEvent (const MidiEvent& event, int64_t eventTime, int bendRange, const BlockContext& context,
                      const float* parameterValues, MidiEventList& output)
    {
        const int channelIndex = event.channel();
        auto& channel = channels[(size_t) channelIndex];

        if (event.isNoteOn())
        {
            const auto chordWindow = (int64_t) (chordWindowMilliseconds / 1000.0 * context.sampleRate);
            const bool isPartOfCurrentChord = eventTime - channel.lastSlideStartTime <= chordWindow;
            if (! isPartOfCurrentChord)
            {
                channel.lastSlideStartTime = eventTime;
                if (decidesToSlide (parameterValues))
                    startSlide (channel, eventTime, context, parameterValues);
                else
                    channel.hasSlide = channel.isMoving = false;
                // The bend must be in place before the note starts (Slide In starts the note at Offset).
                sendBend (channelIndex, channel, currentBend (channel, eventTime, bendRange), event.samplePosition,
                          output);
            }
            auto& isHeld = isKeyHeld[channelNoteIndex (channelIndex, event.noteNumber())];
            if (! isHeld)
                ++channel.numHeldKeys;
            isHeld = true;
            output.push_back (event);
            return;
        }

        if (event.isNoteOff())
        {
            output.push_back (event);
            auto& isHeld = isKeyHeld[channelNoteIndex (channelIndex, event.noteNumber())];
            if (isHeld)
                --channel.numHeldKeys;
            isHeld = false;
            if (channel.numHeldKeys == 0 && channel.hasSlide)
            {
                // The last note ended: back to the player's own bend, so the next note starts in tune.
                channel.hasSlide = channel.isMoving = false;
                sendBend (channelIndex, channel, channel.playerBend, event.samplePosition, output);
            }
            return;
        }

        if (event.isPitchBend())
        {
            channel.playerBend = event.pitchBendValue();
            sendBend (channelIndex, channel, currentBend (channel, eventTime, bendRange), event.samplePosition, output,
                      true);
            return;
        }

        output.push_back (event);
    }

    bool decidesToSlide (const float* parameterValues)
    {
        const int chance = integerParameter (parameterValues, chancePercent);
        if (chance >= 100)
            return true;
        return chance > 0 && randomUnitValue() * 100.0f < (float) chance;
    }

    void startSlide (ChannelState& channel, int64_t eventTime, const BlockContext& context,
                     const float* parameterValues)
    {
        const auto offset = (float) integerParameter (parameterValues, offsetInSemitones);
        const bool isSlideIn = integerParameter (parameterValues, slideMode) == slideIn;
        const double samplesPerBeat = context.samplesPerBeat();
        const int lengthIndex = std::clamp (integerParameter (parameterValues, slideLength), 0, numSlideDivisions - 1);
        const int delayIndex = std::clamp (integerParameter (parameterValues, slideDelay), 0, numSlideDivisions);
        const double delayInBeats = delayIndex == 0 ? 0.0 : slideDivisions[delayIndex - 1].lengthInBeats;

        channel.hasSlide = channel.isMoving = true;
        channel.fromSemitones = isSlideIn ? offset : 0.0f;
        channel.toSemitones = isSlideIn ? 0.0f : offset;
        // Curve -100..+100 maps to an exponent of 1/4..4: below 1 the slide starts fast, above 1 it starts slow.
        channel.curveExponent = (float) std::pow (2.0, 2.0 * integerParameter (parameterValues, curveAmount) / 100.0);
        channel.moveStartTime = eventTime + (int64_t) std::llround (delayInBeats * samplesPerBeat);
        channel.moveEndTime =
            channel.moveStartTime +
            std::max<int64_t> (1, std::llround (slideDivisions[lengthIndex].lengthInBeats * samplesPerBeat));
        channel.nextUpdateTime = channel.moveStartTime;
    }

    /** @return The slide's pitch offset in semitones at an absolute sample time. */
    static float slideSemitonesAt (const ChannelState& channel, int64_t time) noexcept
    {
        if (! channel.hasSlide)
            return 0.0f;
        const double progress = std::clamp (
            (double) (time - channel.moveStartTime) / (double) (channel.moveEndTime - channel.moveStartTime), 0.0, 1.0);
        const double shapedProgress = std::pow (progress, (double) channel.curveExponent);
        return channel.fromSemitones + (channel.toSemitones - channel.fromSemitones) * (float) shapedProgress;
    }

    /** @return The bend to send at a time: the player's bend plus the slide, limited to the bend range. */
    static int currentBend (const ChannelState& channel, int64_t time, int bendRange) noexcept
    {
        const float semitones = std::clamp (slideSemitonesAt (channel, time), (float) -bendRange, (float) bendRange);
        const auto slideBend = (int) std::lround (semitones / (float) bendRange * (float) MidiEvent::pitchBendCentre);
        return std::clamp (channel.playerBend + slideBend, 0, MidiEvent::maximumPitchBend);
    }

    /** Sends a bend if it differs from the last one sent (or always, when the player sent one). */
    static void sendBend (int channelIndex, ChannelState& channel, int bend, int samplePosition, MidiEventList& output,
                          bool sendEvenIfUnchanged = false)
    {
        if (bend == channel.lastSentBend && ! sendEvenIfUnchanged)
            return;
        channel.lastSentBend = bend;
        output.push_back (MidiEvent::pitchBend (samplePosition, channelIndex, bend));
    }

    /** Sends the bend updates of all moving slides that are due before an absolute sample time. */
    void sendSlideUpdatesBefore (int64_t time, int bendRange, const BlockContext& context, MidiEventList& output)
    {
        const auto updateInterval = std::max<int64_t> (1, std::llround (context.sampleRate / updatesPerSecond));
        for (int channelIndex = 0; channelIndex < numMidiChannels; ++channelIndex)
        {
            auto& channel = channels[(size_t) channelIndex];
            while (channel.isMoving && channel.nextUpdateTime < time)
            {
                const int64_t updateTime = std::max (channel.nextUpdateTime, context.blockStartSample);
                sendBend (channelIndex, channel, currentBend (channel, updateTime, bendRange),
                          (int) (updateTime - context.blockStartSample), output);
                if (channel.nextUpdateTime >= channel.moveEndTime)
                    channel.isMoving = false; // arrived; the bend stays until the notes end
                else
                    channel.nextUpdateTime = std::min (channel.nextUpdateTime + updateInterval, channel.moveEndTime);
            }
        }
    }

    std::array<ChannelState, numMidiChannels> channels {};
    std::array<bool, numChannelNoteCombinations> isKeyHeld {};
};

/**
 * Lets a looped melody drift: every time a (single) note plays, it may move one more step to a neighbouring
 * note of the scale, or back towards its original pitch, and the module remembers where each note is now.
 *
 * Memory: per position in a loop of 4, 2 or 1 bars (on a sixteenth grid, needs the DAW playing; otherwise it
 * falls back to per pitch), or per pitch. The memory holds an offset in scale steps. It is cleared by the
 * Reset button (when it turns on, also by modulation) and when the DAW starts playing.
 *
 * Chords pass untouched: notes starting within chordWindowMilliseconds of each other. Because a plug-in cannot
 * look ahead, a mutated note that turns out to be the first note of a chord is restarted at its own pitch.
 */
class Mutate final : public MidiModule
{
public:
    Mutate() : MidiModule (ModuleType::Mutate) { reset(); }

    void reset() override
    {
        clearMemory();
        outputNoteOfKey.fill (noOutputNote);
        velocityOfKey.fill (0);
        lastNoteOnTime = std::numeric_limits<int64_t>::min() / 2;
        lastKey = 0;
        lastNoteWasMutated = false;
        wasResetOn = false;
        wasPlaying = false;
        soundingNotes.clear();
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const bool isResetOn = integerParameter (parameterValues, resetButton) != 0;
        if ((isResetOn && ! wasResetOn) || (context.isPlaying && ! wasPlaying))
            clearMemory();
        wasResetOn = isResetOn;
        wasPlaying = context.isPlaying;

        for (const auto& event : input)
        {
            if (event.isNoteOn())
                handleNoteOn (event, context, parameterValues, output);
            else if (event.isNoteOff())
                handleNoteOff (event, output);
            else if (event.isPolyPressure() && outputNoteOfKey[keyOf (event)] != noOutputNote)
            {
                auto followed = event;
                followed.data1 = (uint8_t) outputNoteOfKey[keyOf (event)];
                output.push_back (followed);
            }
            else
                output.push_back (event);
        }
    }

private:
    enum Parameter
    {
        amountPercent,
        rangeInSteps,
        direction,
        returnPercent,
        rootPitchClass, ///< page 2
        scaleIndex,     ///< page 2
        memoryMode,     ///< page 2
        resetButton     ///< page 2
    };
    enum Direction
    {
        up,
        down,
        both
    };
    enum MemoryMode
    {
        fourBars,
        twoBars,
        oneBar,
        perPitch
    };

    static constexpr double chordWindowMilliseconds = 30.0;
    static constexpr int sixteenthsPerBar = 16;
    static constexpr int maximumOffset = 7; ///< Scale steps a note can drift at most (about an octave)
    static constexpr int16_t noOutputNote = -1;

    static size_t keyOf (const MidiEvent& event) noexcept
    {
        return channelNoteIndex (event.channel(), event.noteNumber());
    }

    void clearMemory() noexcept
    {
        positionOffsets.fill (0);
        pitchOffsets.fill (0);
    }

    void handleNoteOn (const MidiEvent& noteOn, const BlockContext& context, const float* parameterValues,
                       MidiEventList& output)
    {
        const size_t key = keyOf (noteOn);
        if (outputNoteOfKey[key] != noOutputNote)
            endKey (key, noOutputNote, noteOn.samplePosition, output); // played again: end its previous note

        const int64_t eventTime = context.blockStartSample + noteOn.samplePosition;
        const auto chordWindow = (int64_t) (chordWindowMilliseconds / 1000.0 * context.sampleRate);
        const bool isChordNote = eventTime - lastNoteOnTime <= chordWindow;

        int pitch = noteOn.noteNumber();
        if (isChordNote)
        {
            // The previous note turned out to start a chord: restart it at its own pitch, if it was changed.
            if (lastNoteWasMutated && outputNoteOfKey[lastKey] != noOutputNote)
            {
                endKey (lastKey, noOutputNote, noteOn.samplePosition, output);
                startNote (lastKey, (int) (lastKey % numMidiNotes), velocityOfKey[lastKey], noteOn.samplePosition,
                           output);
            }
            lastNoteWasMutated = false;
        }
        else
        {
            pitch = mutatedPitch (noteOn, eventTime, context, parameterValues);
            lastNoteWasMutated = pitch != noteOn.noteNumber();
            lastKey = key;
        }
        lastNoteOnTime = eventTime;
        velocityOfKey[key] = (uint8_t) noteOn.velocity();
        startNote (key, pitch, noteOn.velocity(), noteOn.samplePosition, output);
    }

    void handleNoteOff (const MidiEvent& noteOff, MidiEventList& output)
    {
        const size_t key = keyOf (noteOff);
        if (outputNoteOfKey[key] == noOutputNote)
        {
            output.push_back (noteOff); // a note from before this module was there (rule 1)
            return;
        }
        endKey (key, noOutputNote, noteOff.samplePosition, output, noteOff.velocity());
    }

    /** Sends the note-on for an input key and remembers the pitch it plays. */
    void startNote (size_t key, int pitch, int velocity, int samplePosition, MidiEventList& output)
    {
        const int channel = (int) (key / numMidiNotes);
        outputNoteOfKey[key] = (int16_t) pitch;
        soundingNotes.noteStarted (channel, pitch);
        output.push_back (MidiEvent::noteOn (samplePosition, channel, pitch, velocity));
    }

    /** Ends the note an input key is playing. */
    void endKey (size_t key, int16_t newState, int samplePosition, MidiEventList& output, int releaseVelocity = 0)
    {
        const int channel = (int) (key / numMidiNotes);
        const int pitch = outputNoteOfKey[key];
        outputNoteOfKey[key] = newState;
        if (soundingNotes.noteEnded (channel, pitch))
            output.push_back (MidiEvent::noteOff (samplePosition, channel, pitch, releaseVelocity));
    }

    /** Updates the remembered offset for this note (return, then maybe one more step) and applies it. */
    int mutatedPitch (const MidiEvent& noteOn, int64_t eventTime, const BlockContext& context,
                      const float* parameterValues)
    {
        int8_t& offset = memorySlotFor (noteOn, eventTime, context, integerParameter (parameterValues, memoryMode));

        if (offset != 0 && randomUnitValue() * 100.0f < (float) integerParameter (parameterValues, returnPercent))
            offset = (int8_t) (offset > 0 ? offset - 1 : offset + 1);

        if (randomUnitValue() * 100.0f < (float) integerParameter (parameterValues, amountPercent))
        {
            const int step = randomInteger (1, std::clamp (integerParameter (parameterValues, rangeInSteps), 1, 3));
            const int chosenDirection = integerParameter (parameterValues, direction);
            const bool goesUp = chosenDirection == up || (chosenDirection == both && randomInteger (0, 1) == 1);
            offset = (int8_t) std::clamp (offset + (goesUp ? step : -step), -maximumOffset, maximumOffset);
        }

        const int root = std::clamp (integerParameter (parameterValues, rootPitchClass), 0, 11);
        const auto mask =
            scales[std::clamp (integerParameter (parameterValues, scaleIndex), 0, numScales - 1)].semitoneMask;
        return moveByScaleSteps (noteOn.noteNumber(), offset, root, mask);
    }

    /** @return The memory slot of a note: its position in the loop, or its pitch. */
    int8_t& memorySlotFor (const MidiEvent& noteOn, int64_t eventTime, const BlockContext& context, int mode) noexcept
    {
        if (mode == perPitch || ! context.followsHostTransport())
            return pitchOffsets[(size_t) noteOn.noteNumber()];

        const int loopBars = mode == fourBars ? 4 : (mode == twoBars ? 2 : 1);
        const int loopSixteenths = loopBars * sixteenthsPerBar;
        const double beat = context.ppqPositionAtBlockStart +
                            (double) (eventTime - context.blockStartSample) / context.samplesPerBeat();
        const auto sixteenth = (int64_t) std::llround (beat * 4.0);
        return positionOffsets[(size_t) (((sixteenth % loopSixteenths) + loopSixteenths) % loopSixteenths)];
    }

    /**
     * @return The note `steps` scale notes above (positive) or below (negative) a note; the note itself for 0
     *         steps or when the result would leave the MIDI range.
     */
    static int moveByScaleSteps (int noteNumber, int steps, int root, unsigned short semitoneMask) noexcept
    {
        const int stepDirection = steps > 0 ? 1 : -1;
        int note = noteNumber;
        for (int stepsLeft = std::abs (steps); stepsLeft > 0;)
        {
            note += stepDirection;
            if (note < 0 || note >= numMidiNotes)
                return noteNumber;
            const int semitoneAboveRoot = ((note - root) % 12 + 12) % 12;
            if ((semitoneMask >> semitoneAboveRoot) & 1u)
                --stepsLeft;
        }
        return note;
    }

    std::array<int8_t, 4 * sixteenthsPerBar> positionOffsets {};
    std::array<int8_t, numMidiNotes> pitchOffsets {};
    std::array<int16_t, numChannelNoteCombinations> outputNoteOfKey {};
    std::array<uint8_t, numChannelNoteCombinations> velocityOfKey {};
    int64_t lastNoteOnTime = 0;
    size_t lastKey = 0;
    bool lastNoteWasMutated = false;
    bool wasResetOn = false;
    bool wasPlaying = false;
    SoundingNoteCounter soundingNotes;
};

} // namespace

std::unique_ptr<MidiModule> createRandomizerModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Humanize: return std::make_unique<Humanize>();
        case ModuleType::Probability: return std::make_unique<Probability>();
        case ModuleType::RandomPitch: return std::make_unique<RandomPitch>();
        case ModuleType::PitchSlide: return std::make_unique<PitchSlide>();
        case ModuleType::Mutate: return std::make_unique<Mutate>();
        default: return nullptr;
    }
}

} // namespace midiforge
