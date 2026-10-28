/**
 * @file GeneratorModules.cpp
 * @brief Modules that create new notes: Chord, Note Echo, Arpeggiator, Repeater, Strum and Ghost Notes.
 */
#include "GeneratorModules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace midiforge
{
namespace
{

//==============================================================================
/** Plays a chord on top of every incoming note. */
class Chord final : public NoteMappingModule
{
public:
    Chord() : NoteMappingModule (ModuleType::Chord) {}

private:
    enum Parameter
    {
        chordType,
        inversion,
        voicing,
        addedNoteVelocity
    };
    enum Voicing
    {
        close,
        open,
        extraBassNote
    };

    static constexpr int maxChordNotes = 6; // largest shape (5 notes) plus an extra bass note

    int createNoteOns (const MidiEvent& noteOn, MidiEvent* outputNoteOns, const float* parameterValues) override
    {
        const auto& shape =
            chordShapes[std::clamp (integerParameter (parameterValues, chordType), 0, numChordShapes - 1)];

        int noteNumbers[maxChordNotes];
        bool isPlayedNote[maxChordNotes];
        int numNotes = shape.numNotes;
        for (int index = 0; index < numNotes; ++index)
        {
            noteNumbers[index] = noteOn.noteNumber() + shape.semitoneOffsets[index];
            isPlayedNote[index] = index == 0;
        }

        // Each inversion moves the currently lowest note up an octave.
        for (int step = 0; step < integerParameter (parameterValues, inversion) && numNotes > 1; ++step)
            noteNumbers[indexOfLowest (noteNumbers, numNotes, -1)] += 12;

        const int selectedVoicing = integerParameter (parameterValues, voicing);
        if (selectedVoicing == open && numNotes >= 3)
        {
            // Open voicing: raise the second-lowest note an octave to spread the chord.
            const int lowest = indexOfLowest (noteNumbers, numNotes, -1);
            noteNumbers[indexOfLowest (noteNumbers, numNotes, lowest)] += 12;
        }
        else if (selectedVoicing == extraBassNote)
        {
            noteNumbers[numNotes] = noteOn.noteNumber() - 12;
            isPlayedNote[numNotes] = false;
            ++numNotes;
        }

        const float velocityScale = (float) integerParameter (parameterValues, addedNoteVelocity) / 100.0f;
        int numOutputNotes = 0;
        for (int index = 0; index < numNotes && numOutputNotes < NoteMappingTable::maxOutputNotesPerInput; ++index)
        {
            const int noteNumber = noteNumbers[index];
            if (noteNumber < 0 || noteNumber > 127 || containsNote (outputNoteOns, numOutputNotes, noteNumber))
                continue;
            const int velocity = isPlayedNote[index]
                                     ? noteOn.velocity()
                                     : std::max (1, (int) std::lround ((float) noteOn.velocity() * velocityScale));
            outputNoteOns[numOutputNotes++] =
                MidiEvent::noteOn (noteOn.samplePosition, noteOn.channel(), noteNumber, velocity);
        }
        return numOutputNotes;
    }

    /** @return The index of the lowest note, ignoring the index excludedIndex. */
    static int indexOfLowest (const int* noteNumbers, int numNotes, int excludedIndex)
    {
        int lowest = excludedIndex == 0 ? 1 : 0;
        for (int index = 0; index < numNotes; ++index)
            if (index != excludedIndex && noteNumbers[index] < noteNumbers[lowest])
                lowest = index;
        return lowest;
    }

    static bool containsNote (const MidiEvent* noteOns, int numNoteOns, int noteNumber)
    {
        for (int index = 0; index < numNoteOns; ++index)
            if (noteOns[index].noteNumber() == noteNumber)
                return true;
        return false;
    }
};

//==============================================================================
/**
 * Repeats every note at a tempo-synced interval, with decaying velocity and an optional pitch step.
 * The original note passes through; each echo keeps the original note's length.
 */
class NoteEcho final : public SchedulingModule
{
public:
    NoteEcho() : SchedulingModule (ModuleType::NoteEcho) {}

    void reset() override
    {
        SchedulingModule::reset();
        for (auto& echoes : echoesByInputNote)
            echoes.isActive = false;
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        for (const auto& event : input)
        {
            const int64_t eventTime = context.blockStartSample + event.samplePosition;
            sendScheduledEventsUpTo (eventTime, context, output);

            if (event.isNoteOn())
                startEchoes (event, eventTime, context, output, parameterValues);
            else if (event.isNoteOff())
                stopEchoes (event, eventTime, output);
            else
                sendEvent (event, output);
        }
        sendScheduledEventsInBlock (context, output);
    }

private:
    enum Parameter
    {
        delayTime,
        numRepeats,
        feedback,
        pitchStep
    };
    static constexpr int maxRepeats = 16;

    /** The echoes one held input note has scheduled. */
    struct EchoSeries
    {
        bool isActive = false;
        int numEchoes = 0;
        uint8_t channel = 0;
        int64_t delayInSamples = 1;
        uint8_t noteNumbers[maxRepeats] {};
    };

    void startEchoes (const MidiEvent& noteOn, int64_t noteOnTime, const BlockContext& context, MidiEventList& output,
                      const float* parameterValues)
    {
        auto& echoes = echoesByInputNote[channelNoteIndex (noteOn.channel(), noteOn.noteNumber())];
        if (echoes.isActive) // retriggered without a note-off: end the previous note and its echoes first
        {
            sendEvent (MidiEvent::noteOff (noteOn.samplePosition, noteOn.channel(), noteOn.noteNumber()), output);
            scheduleEchoNoteOffs (echoes, noteOnTime);
        }

        sendEvent (noteOn, output);

        const auto& division =
            echoDivisions[std::clamp (integerParameter (parameterValues, delayTime), 0, numEchoDivisions - 1)];
        echoes = {};
        echoes.isActive = true;
        echoes.channel = (uint8_t) noteOn.channel();
        echoes.delayInSamples = std::max<int64_t> (
            1, std::llround (divisionLengthInBeats (division, context.timeSignature) * context.samplesPerBeat()));

        const int repeats = std::min (integerParameter (parameterValues, numRepeats), maxRepeats);
        const float velocityFactor = (float) integerParameter (parameterValues, feedback) / 100.0f;
        const int semitonesPerEcho = integerParameter (parameterValues, pitchStep);
        float echoVelocity = (float) noteOn.velocity();

        for (int echoNumber = 1; echoNumber <= repeats; ++echoNumber)
        {
            echoVelocity *= velocityFactor;
            const int echoNote = noteOn.noteNumber() + echoNumber * semitonesPerEcho;
            if (echoVelocity < 1.0f || echoNote < 0 || echoNote > 127)
                break;

            scheduleEvent (noteOnTime + echoNumber * echoes.delayInSamples,
                           MidiEvent::noteOn (0, noteOn.channel(), echoNote, (int) std::lround (echoVelocity)));
            echoes.noteNumbers[echoes.numEchoes++] = (uint8_t) echoNote;
        }
    }

    void stopEchoes (const MidiEvent& noteOff, int64_t noteOffTime, MidiEventList& output)
    {
        sendEvent (noteOff, output);
        auto& echoes = echoesByInputNote[channelNoteIndex (noteOff.channel(), noteOff.noteNumber())];
        if (echoes.isActive)
            scheduleEchoNoteOffs (echoes, noteOffTime);
    }

    /** Each echo ends the same distance after the original note-off as it started after the note-on. */
    void scheduleEchoNoteOffs (EchoSeries& echoes, int64_t originalNoteOffTime)
    {
        for (int index = 0; index < echoes.numEchoes; ++index)
            scheduleEvent (originalNoteOffTime + (index + 1) * echoes.delayInSamples,
                           MidiEvent::noteOff (0, echoes.channel, echoes.noteNumbers[index]));
        echoes.isActive = false;
    }

    std::array<EchoSeries, numChannelNoteCombinations> echoesByInputNote {};
};

//==============================================================================
/**
 * Plays the held notes one after another at a tempo-synced rate.
 * Follows the host's bar grid while the transport runs, otherwise runs on its own clock that starts
 * with the first key pressed.
 */
class Arpeggiator final : public SchedulingModule
{
public:
    Arpeggiator() : SchedulingModule (ModuleType::Arpeggiator)
    {
        heldNotes.reserve (maxHeldNotes);
        notesInPitchOrder.reserve (maxHeldNotes);
        pattern.reserve (maxHeldNotes * 8);
        stepPositionsInBlock.reserve (1024);
    }

    void reset() override
    {
        SchedulingModule::reset();
        heldNotes.clear();
        nextPatternIndex = 0;
        nextPressOrder = 0;
        nextFreeRunningStepTime = 0;
        lastHostStepNumber = INT64_MIN;
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const double stepLengthInBeats =
            arpeggiatorDivisions[std::clamp (integerParameter (parameterValues, rate), 0, numArpeggiatorDivisions - 1)]
                .lengthInBeats;
        const int64_t stepLengthInSamples =
            std::max<int64_t> (2, std::llround (stepLengthInBeats * context.samplesPerBeat()));
        // The gate is capped one sample short of the step, so a note always ends before the next one starts.
        const int64_t noteLengthInSamples = std::clamp<int64_t> (
            std::llround ((double) stepLengthInSamples * integerParameter (parameterValues, gate) / 100.0), 1,
            stepLengthInSamples - 1);

        const bool followHost = context.followsHostTransport();
        if (followHost)
            findHostStepsInBlock (context, stepLengthInBeats);
        else if (nextFreeRunningStepTime < context.blockStartSample)
            nextFreeRunningStepTime = context.blockStartSample; // our clock is stale (e.g. transport just stopped)

        // Walk through the block, handling input events and steps in time order.
        // At equal positions input comes first, so a chord pressed exactly on a step is played complete.
        constexpr int noMoreEvents = INT32_MAX;
        size_t nextInputIndex = 0;
        size_t nextHostStepIndex = 0;
        const int64_t blockEndSample = context.blockStartSample + context.numSamples;

        for (;;)
        {
            int nextStepPosition = noMoreEvents;
            if (followHost)
            {
                if (nextHostStepIndex < stepPositionsInBlock.size())
                    nextStepPosition = stepPositionsInBlock[nextHostStepIndex];
            }
            else if (! heldNotes.empty() && nextFreeRunningStepTime < blockEndSample)
            {
                nextStepPosition = (int) (nextFreeRunningStepTime - context.blockStartSample);
            }

            const int nextInputPosition =
                nextInputIndex < input.size() ? input[nextInputIndex].samplePosition : noMoreEvents;
            if (nextStepPosition == noMoreEvents && nextInputPosition == noMoreEvents)
                break;

            if (nextInputPosition <= nextStepPosition)
            {
                const auto& event = input[nextInputIndex++];
                sendScheduledEventsUpTo (context.blockStartSample + event.samplePosition, context, output);
                handleInputEvent (event, context, followHost, output);
            }
            else
            {
                sendScheduledEventsUpTo (context.blockStartSample + nextStepPosition, context, output);
                playStep (nextStepPosition, context, noteLengthInSamples, parameterValues, output);
                if (followHost)
                    ++nextHostStepIndex;
                else
                    nextFreeRunningStepTime += stepLengthInSamples;
            }
        }
        sendScheduledEventsInBlock (context, output);
    }

private:
    enum Parameter
    {
        rate,
        patternType,
        octaveRange,
        gate
    };
    enum PatternType
    {
        up,
        down,
        upDown,
        random,
        asPlayed
    };
    static constexpr int maxHeldNotes = 32;

    struct HeldNote
    {
        uint8_t noteNumber;
        uint8_t channel;
        uint8_t velocity;
        uint32_t pressOrder; ///< Increases with every key press, for the "As Played" pattern.
    };

    /** Fills stepPositionsInBlock with the sample positions where the host's step grid falls in this block. */
    void findHostStepsInBlock (const BlockContext& context, double stepLengthInBeats)
    {
        stepPositionsInBlock.clear();
        const double blockStartBeat = context.ppqPositionAtBlockStart;
        const double samplesPerBeat = context.samplesPerBeat();
        const int64_t firstStepNumber = (int64_t) std::ceil (blockStartBeat / stepLengthInBeats - 1.0e-9);

        for (int64_t stepNumber = firstStepNumber;; ++stepNumber)
        {
            const double stepBeat = (double) stepNumber * stepLengthInBeats;
            const int64_t position = std::max<int64_t> (0, std::llround ((stepBeat - blockStartBeat) * samplesPerBeat));
            if (position >= context.numSamples || stepPositionsInBlock.size() == stepPositionsInBlock.capacity())
                break;
            // Rounding can put the same step at the end of one block and the start of the next; play it once.
            if (stepNumber != lastHostStepNumber)
                stepPositionsInBlock.push_back ((int) position);
            lastHostStepNumber = stepNumber;
        }
    }

    void handleInputEvent (const MidiEvent& event, const BlockContext& context, bool followHost, MidiEventList& output)
    {
        if (event.isNoteOn())
        {
            if (heldNotes.empty())
            {
                nextPatternIndex = 0;
                if (! followHost)
                    nextFreeRunningStepTime = context.blockStartSample + event.samplePosition; // start right away
            }
            for (auto& held : heldNotes)
            {
                if (held.noteNumber == event.noteNumber() && held.channel == event.channel())
                {
                    held.velocity = (uint8_t) event.velocity();
                    return;
                }
            }
            if ((int) heldNotes.size() < maxHeldNotes)
                heldNotes.push_back ({ (uint8_t) event.noteNumber(), (uint8_t) event.channel(),
                                       (uint8_t) event.velocity(), nextPressOrder++ });
        }
        else if (event.isNoteOff())
        {
            for (size_t index = 0; index < heldNotes.size(); ++index)
            {
                if (heldNotes[index].noteNumber == event.noteNumber() && heldNotes[index].channel == event.channel())
                {
                    heldNotes.erase (heldNotes.begin() + (std::ptrdiff_t) index);
                    return;
                }
            }
            sendEvent (event, output); // a note that started before the arpeggiator was switched on
        }
        else
        {
            sendEvent (event, output);
        }
    }

    void playStep (int samplePosition, const BlockContext& context, int64_t noteLengthInSamples,
                   const float* parameterValues, MidiEventList& output)
    {
        if (heldNotes.empty())
            return;

        buildPattern (integerParameter (parameterValues, patternType), integerParameter (parameterValues, octaveRange));
        if (pattern.empty())
            return;

        const HeldNote& step = integerParameter (parameterValues, patternType) == random
                                   ? pattern[(size_t) randomInteger (0, (int) pattern.size() - 1)]
                                   : pattern[nextPatternIndex++ % pattern.size()];

        sendEvent (MidiEvent::noteOn (samplePosition, step.channel, step.noteNumber, step.velocity), output);
        scheduleEvent (context.blockStartSample + samplePosition + noteLengthInSamples,
                       MidiEvent::noteOff (0, step.channel, step.noteNumber));
    }

    /** Builds the sequence of notes the arpeggiator walks through from the held notes. */
    void buildPattern (int selectedPattern, int numOctaves)
    {
        notesInPitchOrder = heldNotes;
        if (selectedPattern == asPlayed)
            std::sort (notesInPitchOrder.begin(), notesInPitchOrder.end(),
                       [] (const HeldNote& first, const HeldNote& second)
                       { return first.pressOrder < second.pressOrder; });
        else
            std::sort (notesInPitchOrder.begin(), notesInPitchOrder.end(),
                       [] (const HeldNote& first, const HeldNote& second)
                       { return first.noteNumber < second.noteNumber; });

        pattern.clear();
        for (int octave = 0; octave < numOctaves; ++octave)
            for (const auto& held : notesInPitchOrder)
                if (held.noteNumber + 12 * octave <= 127)
                    pattern.push_back ({ (uint8_t) (held.noteNumber + 12 * octave), held.channel, held.velocity, 0 });

        if (selectedPattern == down)
        {
            std::reverse (pattern.begin(), pattern.end());
        }
        else if (selectedPattern == upDown && pattern.size() > 2)
        {
            // Walk back down without repeating the top and bottom notes: C E G -> C E G E
            for (size_t index = pattern.size() - 2; index >= 1; --index)
                pattern.push_back (pattern[index]);
        }
    }

    std::vector<HeldNote> heldNotes, notesInPitchOrder, pattern;
    std::vector<int> stepPositionsInBlock;
    size_t nextPatternIndex = 0;
    uint32_t nextPressOrder = 0;
    int64_t nextFreeRunningStepTime = 0;
    int64_t lastHostStepNumber = INT64_MIN;
};

//==============================================================================
/**
 * Retriggers every held note on a tempo grid ("ratchets"). All held notes repeat together.
 *
 * - A pressed key sounds immediately; its repeats follow on the grid.
 * - The grid follows the host's musical position while the transport runs. Otherwise it runs at
 *   project tempo on its own clock, which restarts with the first key pressed (like the Arpeggiator).
 * - Swing delays every second (odd) step; accents make selected steps louder.
 * - Repeats use the velocity of the last key pressed, or the aftertouch (poly pressure for that note
 *   if the keyboard sends it, otherwise channel pressure; the note's own velocity until pressure arrives).
 * - Each hit lasts the set note length (a share of a step) but always ends before the next grid step,
 *   so repeats never overlap. A key pressed less than half a step before a grid step skips that step,
 *   so it does not double-trigger.
 */
class Repeater final : public SchedulingModule
{
public:
    Repeater() : SchedulingModule (ModuleType::Repeater) { heldNotes.reserve (maxHeldNotes); }

    void reset() override
    {
        SchedulingModule::reset();
        heldNotes.clear();
        lastPressedVelocity = 100;
        polyPressure.fill (noPressure);
        channelPressure.fill (noPressure);
        lastStepNumber = noStepYet;
        ownClockBeat = 0.0;
        wasFollowingHost = false;
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const double stepLength =
            repeaterDivisions[std::clamp (integerParameter (parameterValues, rate), 0, numRepeaterDivisions - 1)]
                .lengthInBeats;
        // Swing 50 % = straight, 75 % = the second step of each pair starts three quarters into the pair.
        const double swingDelayInSteps = 2.0 * integerParameter (parameterValues, swingPercent) / 100.0 - 1.0;
        const double samplesPerBeat = context.samplesPerBeat();
        const bool followHost = context.followsHostTransport();
        const double noteLengthShare = integerParameter (parameterValues, noteLengthPercent) / 100.0;
        const auto noteLengthInSamples =
            std::max<int64_t> (1, std::llround (noteLengthShare * stepLength * samplesPerBeat) - 1);

        double blockStartBeat = followHost ? context.ppqPositionAtBlockStart : ownClockBeat;
        resyncGridIfNeeded (blockStartBeat, stepLength, swingDelayInSteps, followHost);

        // Walk through the block, handling input events and grid steps in time order.
        // At equal positions input comes first, so a chord pressed exactly on a step is complete.
        size_t nextInputIndex = 0;
        for (;;)
        {
            const int nextStepPosition = positionOfStep (lastStepNumber + 1, blockStartBeat, stepLength,
                                                         swingDelayInSteps, samplesPerBeat, context.numSamples);
            const int nextInputPosition =
                nextInputIndex < input.size() ? input[nextInputIndex].samplePosition : noMoreEvents;
            if (nextStepPosition == noMoreEvents && nextInputPosition == noMoreEvents)
                break;

            if (nextInputPosition <= nextStepPosition)
            {
                const auto& event = input[nextInputIndex++];
                const int64_t eventTime = context.blockStartSample + event.samplePosition;
                sendScheduledEventsUpTo (eventTime, context, output);

                if (event.isNoteOn() && heldNotes.empty() && ! followHost)
                {
                    // Free running: beat 0 of our clock is this key press (step 0); repeats start at step 1.
                    ownClockBeat = -event.samplePosition / samplesPerBeat;
                    blockStartBeat = ownClockBeat;
                    lastStepNumber = 0;
                }
                // Where the next two grid steps are, for the first hit's length and the double-trigger guard.
                const auto timeOfStep = [&] (int64_t stepNumber)
                {
                    return context.blockStartSample +
                           (int64_t) std::ceil (
                               (stepBeat (stepNumber, stepLength, swingDelayInSteps) - blockStartBeat) *
                                   samplesPerBeat -
                               1.0e-6);
                };
                handleInputEvent (event, eventTime, noteLengthInSamples, timeOfStep (lastStepNumber + 1),
                                  timeOfStep (lastStepNumber + 2), output);
            }
            else
            {
                const int64_t stepTime = context.blockStartSample + nextStepPosition;
                sendScheduledEventsUpTo (stepTime, context, output);
                ++lastStepNumber;
                const auto samplesToNextStep =
                    (int64_t) std::llround ((stepBeat (lastStepNumber + 1, stepLength, swingDelayInSteps) -
                                             stepBeat (lastStepNumber, stepLength, swingDelayInSteps)) *
                                            samplesPerBeat);
                playStep (lastStepNumber, stepTime, stepLength, std::min (noteLengthInSamples, samplesToNextStep - 1),
                          parameterValues);
            }
        }

        sendScheduledEventsInBlock (context, output);
        ownClockBeat = blockStartBeat + context.numSamples / samplesPerBeat;
        wasFollowingHost = followHost;
    }

private:
    enum Parameter
    {
        rate,
        velocitySource,
        swingPercent,
        accentPattern,
        accentBoost,       ///< page 2
        noteLengthPercent, ///< page 2
        chancePercent      ///< page 2 (added in 0.9.0)
    };
    enum VelocitySource
    {
        lastNote,
        aftertouch
    };
    enum AccentPattern
    {
        noAccent,
        everySecondStep,
        everyThirdStep,
        everyFourthStep,
        onTheBeat
    };

    static constexpr int maxHeldNotes = 32;
    static constexpr int noPressure = -1;
    static constexpr int noMoreEvents = INT32_MAX;
    static constexpr int64_t noStepYet = std::numeric_limits<int64_t>::min();

    struct HeldNote
    {
        uint8_t noteNumber;
        uint8_t channel;
        uint8_t ownVelocity;
        int64_t stepsBlockedUntil; ///< Grid steps before this absolute sample time are skipped for this key
    };

    /** Beat position of grid step n: n step lengths, odd steps delayed by the swing. */
    static double stepBeat (int64_t stepNumber, double stepLength, double swingDelayInSteps) noexcept
    {
        const bool isOddStep = (stepNumber % 2) != 0;
        return ((double) stepNumber + (isOddStep ? swingDelayInSteps : 0.0)) * stepLength;
    }

    /** @return The sample position of a step in this block, or noMoreEvents if it is not in this block. */
    static int positionOfStep (int64_t stepNumber, double blockStartBeat, double stepLength, double swingDelayInSteps,
                               double samplesPerBeat, int numSamples) noexcept
    {
        const double position =
            (stepBeat (stepNumber, stepLength, swingDelayInSteps) - blockStartBeat) * samplesPerBeat;
        if (position >= (double) numSamples)
            return noMoreEvents;
        return (int) std::max (0.0, std::ceil (position - 1.0e-6));
    }

    /**
     * Picks the next grid step after a start, a switch between host and own clock, or a jump of the host
     * position, so steps are neither replayed nor fired in a burst to catch up.
     */
    void resyncGridIfNeeded (double blockStartBeat, double stepLength, double swingDelayInSteps, bool followHost)
    {
        const bool needsResync =
            lastStepNumber == noStepYet || followHost != wasFollowingHost ||
            stepBeat (lastStepNumber, stepLength, swingDelayInSteps) > blockStartBeat + stepLength ||
            stepBeat (lastStepNumber + 1, stepLength, swingDelayInSteps) < blockStartBeat - stepLength;
        if (needsResync)
            lastStepNumber = (int64_t) std::ceil (blockStartBeat / stepLength - 1.0e-9) - 1;
    }

    /**
     * @param nextStepTime, stepAfterNextTime  Absolute times of the next two grid steps (for the first hit).
     */
    void handleInputEvent (const MidiEvent& event, int64_t eventTime, int64_t noteLengthInSamples, int64_t nextStepTime,
                           int64_t stepAfterNextTime, MidiEventList& output)
    {
        if (event.isNoteOn())
        {
            lastPressedVelocity = event.velocity();
            polyPressure[channelNoteIndex (event.channel(), event.noteNumber())] = noPressure;

            HeldNote* held = findHeld (event.channel(), event.noteNumber());
            if (held == nullptr && (int) heldNotes.size() < maxHeldNotes)
            {
                heldNotes.push_back (
                    { (uint8_t) event.noteNumber(), (uint8_t) event.channel(), (uint8_t) event.velocity(), 0 });
                held = &heldNotes.back();
            }
            if (held != nullptr)
            {
                held->ownVelocity = (uint8_t) event.velocity();

                // The first hit sounds right away. Pressed less than half a step before the next grid step,
                // it takes that step's place (the step is skipped); either way it ends before the step after.
                const int64_t halfStep = (stepAfterNextTime - nextStepTime) / 2;
                const bool replacesNextStep = nextStepTime - eventTime < halfStep;
                const int64_t endOfFirstHit = replacesNextStep ? stepAfterNextTime : nextStepTime;
                const int64_t firstHitLength =
                    std::clamp<int64_t> (endOfFirstHit - eventTime - 1, 1, noteLengthInSamples);
                hit (*held, eventTime, event.velocity(), firstHitLength);
                held->stepsBlockedUntil = replacesNextStep ? nextStepTime + 1 : eventTime + firstHitLength;
            }
            return;
        }

        if (event.isNoteOff())
        {
            for (size_t index = 0; index < heldNotes.size(); ++index)
            {
                if (heldNotes[index].noteNumber == event.noteNumber() && heldNotes[index].channel == event.channel())
                {
                    // The last hit ends on its own; the key's note-off itself is not passed on.
                    heldNotes.erase (heldNotes.begin() + (std::ptrdiff_t) index);
                    if (heldNotes.empty())
                        channelPressure.fill (noPressure);
                    return;
                }
            }
            sendEvent (event, output); // a note that started before the Repeater was switched on
            return;
        }

        if (event.isPolyPressure())
            polyPressure[channelNoteIndex (event.channel(), event.noteNumber())] = event.data2;
        else if (event.isChannelPressure())
            channelPressure[(size_t) event.channel()] = event.data1;
        sendEvent (event, output); // aftertouch and everything else passes through
    }

    void playStep (int64_t stepNumber, int64_t stepTime, double stepLength, int64_t noteLengthInSamples,
                   const float* parameterValues)
    {
        const bool isAccented =
            isAccentedStep (stepNumber, stepLength, integerParameter (parameterValues, accentPattern));
        const bool usesAftertouch = integerParameter (parameterValues, velocitySource) == aftertouch;

        // Chance: one decision per step for all held keys, so a chord plays or rests as a whole.
        // Only when a key is due, so the random sequence doesn't depend on steps without notes.
        const int chance = integerParameter (parameterValues, chancePercent);
        const bool anyKeyIsDue = std::any_of (heldNotes.begin(), heldNotes.end(), [stepTime] (const HeldNote& held)
                                              { return stepTime >= held.stepsBlockedUntil; });
        if (anyKeyIsDue && chance < 100 && randomUnitValue() * 100.0f >= (float) chance)
            return; // this step rests

        for (auto& held : heldNotes)
        {
            if (stepTime < held.stepsBlockedUntil)
                continue; // the key's first hit took this step's place
            const int velocity = (usesAftertouch ? aftertouchVelocity (held) : lastPressedVelocity) +
                                 (isAccented ? integerParameter (parameterValues, accentBoost) : 0);
            hit (held, stepTime, velocity, noteLengthInSamples);
            held.stepsBlockedUntil = stepTime + noteLengthInSamples;
        }
    }

    /** Plays a held key at the given time for the given length. */
    void hit (const HeldNote& held, int64_t time, int velocity, int64_t noteLengthInSamples)
    {
        scheduleEvent (time, MidiEvent::noteOn (0, held.channel, held.noteNumber, std::clamp (velocity, 1, 127)));
        scheduleEvent (time + noteLengthInSamples, MidiEvent::noteOff (0, held.channel, held.noteNumber));
    }

    int aftertouchVelocity (const HeldNote& held) const
    {
        const int notePressure = polyPressure[channelNoteIndex (held.channel, held.noteNumber)];
        if (notePressure != noPressure)
            return notePressure;
        if (channelPressure[held.channel] != noPressure)
            return channelPressure[held.channel];
        return held.ownVelocity;
    }

    static bool isAccentedStep (int64_t stepNumber, double stepLength, int pattern)
    {
        const auto isMultipleOf = [stepNumber] (int64_t divisor)
        { return ((stepNumber % divisor) + divisor) % divisor == 0; };
        switch (pattern)
        {
            case everySecondStep: return isMultipleOf (2);
            case everyThirdStep: return isMultipleOf (3);
            case everyFourthStep: return isMultipleOf (4);
            case onTheBeat:
            {
                const double beat = (double) stepNumber * stepLength;
                return std::abs (beat - std::round (beat)) < 1.0e-6;
            }
            default: return false;
        }
    }

    HeldNote* findHeld (int channel, int noteNumber)
    {
        for (auto& held : heldNotes)
            if (held.noteNumber == noteNumber && held.channel == channel)
                return &held;
        return nullptr;
    }

    std::vector<HeldNote> heldNotes;
    int lastPressedVelocity = 100;
    std::array<int, numChannelNoteCombinations> polyPressure {};
    std::array<int, numMidiChannels> channelPressure {};
    int64_t lastStepNumber = noStepYet;
    double ownClockBeat = 0.0; ///< Our own clock, in beats, at the start of the next block
    bool wasFollowingHost = false;
};

//==============================================================================
/**
 * Rolls chords. Notes arriving within Window are collected; then they start Speed apart in the chosen order.
 * Everything is delayed by Window (the host is told, see MidiForgeProcessor::reportLatencyOfModules), so the
 * first note of a strum lands where the chord was played. A note-off is delayed as much as its note-on.
 */
class Strum final : public SchedulingModule
{
public:
    Strum() : SchedulingModule (ModuleType::Strum) { reset(); }

    void reset() override
    {
        SchedulingModule::reset();
        delayOfHeldNote.fill (noDelayStored);
        numCollected = 0;
        lastStrumTime = std::numeric_limits<int64_t>::min() / 2;
        nextAlternateGoesUp = true;
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const int64_t window = millisecondsToSamples (integerParameter (parameterValues, windowMilliseconds), context);
        for (const auto& event : input)
        {
            const int64_t eventTime = context.blockStartSample + event.samplePosition;
            if (numCollected > 0 && eventTime > collectionStart + window)
                playCollectedChord (context, parameterValues, window);
            sendScheduledEventsUpTo (eventTime, context, output);

            if (event.isNoteOn())
            {
                auto& storedDelay = delayOfHeldNote[keyOf (event)];
                if (storedDelay != noDelayStored) // played again: end the previous note first
                    scheduleEvent (eventTime + storedDelay,
                                   MidiEvent::noteOff (0, event.channel(), event.noteNumber()));
                storedDelay = noDelayStored;
                if (numCollected == 0)
                    collectionStart = eventTime;
                // A key already waiting in this chord is not added twice (one note-off ends both).
                if (numCollected < maxChordNotes && ! isCollected (event))
                    collected[(size_t) numCollected++] = { event, eventTime };
                continue;
            }

            if (event.isNoteOff())
            {
                // A note released while its chord is still being collected: play the chord first.
                if (isCollected (event))
                    playCollectedChord (context, parameterValues, window);
                auto& storedDelay = delayOfHeldNote[keyOf (event)];
                if (storedDelay != noDelayStored)
                    scheduleEvent (eventTime + storedDelay, event);
                else
                    sendEvent (event, output); // a note that started before this module was active
                storedDelay = noDelayStored;
                continue;
            }

            // Everything else keeps its place relative to the notes.
            if (window > 0)
                scheduleEvent (eventTime + window, event);
            else
                sendEvent (event, output);
        }

        // Window 0: only notes at the very same time form a chord, so it can be played at the end of the block.
        const int64_t blockEnd = context.blockStartSample + context.numSamples;
        if (numCollected > 0 && blockEnd > collectionStart + window)
            playCollectedChord (context, parameterValues, window);
        sendScheduledEventsInBlock (context, output);
    }

private:
    enum Parameter
    {
        speedMilliseconds,
        direction,
        velocityTilt,
        windowMilliseconds
    };
    enum Direction
    {
        up,
        down,
        alternate,
        random
    };
    struct CollectedNote
    {
        MidiEvent noteOn;
        int64_t arrivalTime;
    };

    static constexpr int maxChordNotes = 16;
    static constexpr int64_t noDelayStored = -1;
    static constexpr double phrasePauseSeconds = 0.5; ///< Alternate starts low to high again after this pause

    static size_t keyOf (const MidiEvent& event) noexcept
    {
        return channelNoteIndex (event.channel(), event.noteNumber());
    }
    static int64_t millisecondsToSamples (int milliseconds, const BlockContext& context) noexcept
    {
        return std::llround (milliseconds / 1000.0 * context.sampleRate);
    }

    bool isCollected (const MidiEvent& noteOff) const noexcept
    {
        for (int index = 0; index < numCollected; ++index)
            if (keyOf (collected[(size_t) index].noteOn) == keyOf (noteOff))
                return true;
        return false;
    }

    /** Schedules the collected notes as a strum and remembers each note's delay for its note-off. */
    void playCollectedChord (const BlockContext& context, const float* parameterValues, int64_t window)
    {
        const auto begin = collected.begin();
        const auto end = collected.begin() + numCollected;
        std::sort (begin, end, [] (const CollectedNote& first, const CollectedNote& second)
                   { return first.noteOn.noteNumber() < second.noteOn.noteNumber(); });

        bool goesUp = true;
        switch (integerParameter (parameterValues, direction))
        {
            case down: goesUp = false; break;
            case alternate:
                if (collectionStart - lastStrumTime >
                    millisecondsToSamples ((int) (phrasePauseSeconds * 1000), context))
                    nextAlternateGoesUp = true; // a new phrase starts low to high
                goesUp = nextAlternateGoesUp;
                nextAlternateGoesUp = ! nextAlternateGoesUp;
                break;
            case random:
                for (int index = numCollected - 1; index > 0; --index)
                    std::swap (collected[(size_t) index], collected[(size_t) randomInteger (0, index)]);
                break;
            default: break;
        }
        if (! goesUp)
            std::reverse (begin, end);

        const int64_t speed = millisecondsToSamples (integerParameter (parameterValues, speedMilliseconds), context);
        const int tilt = integerParameter (parameterValues, velocityTilt);
        for (int index = 0; index < numCollected; ++index)
        {
            const auto& note = collected[(size_t) index];
            const int64_t startTime = collectionStart + window + (int64_t) index * speed;
            const int velocityChange =
                numCollected > 1 ? (int) std::lround ((double) tilt * index / (numCollected - 1)) : 0;
            MidiEvent noteOn = note.noteOn;
            noteOn.data2 = (uint8_t) std::clamp (note.noteOn.velocity() + velocityChange, 1, 127);
            scheduleEvent (startTime, noteOn);
            delayOfHeldNote[keyOf (note.noteOn)] = startTime - note.arrivalTime;
        }
        lastStrumTime = collectionStart;
        numCollected = 0;
    }

    std::array<int64_t, numChannelNoteCombinations> delayOfHeldNote {};
    std::array<CollectedNote, maxChordNotes> collected {};
    int numCollected = 0;
    int64_t collectionStart = 0;
    int64_t lastStrumTime = 0;
    bool nextAlternateGoesUp = true;
};

//==============================================================================
/**
 * Adds short, quiet copies of notes ("ghost notes") Distance apart. After: scheduled from the note. Before:
 * a plug-in cannot look ahead, so every note is remembered for a bar, and ghosts are played just before the
 * place it fell one bar earlier (while the DAW plays). The chance is decided once per note.
 */
class GhostNotes final : public SchedulingModule
{
public:
    GhostNotes() : SchedulingModule (ModuleType::GhostNotes) { reset(); }

    void reset() override
    {
        SchedulingModule::reset();
        numRemembered = 0;
        isKeyHeld.fill (false);
    }

    void process (const MidiEventList& input, MidiEventList& output, const BlockContext& context,
                  const float* parameterValues) override
    {
        const int position = integerParameter (parameterValues, ghostPosition);
        const bool wantsAfter = position == after || position == both;
        const bool wantsBefore = (position == before || position == both) && context.followsHostTransport();
        const double distanceInBeats =
            ghostDistances[std::clamp (integerParameter (parameterValues, distanceIndex), 0, 3)];
        const int count = std::clamp (integerParameter (parameterValues, ghostCount), 1, 3);
        const int64_t ghostLength = std::max<int64_t> (1, std::llround (ghostLengthInBeats * context.samplesPerBeat()));

        if (wantsBefore)
            scheduleGhostsBeforeRememberedNotes (context, distanceInBeats, count, ghostLength);
        else if (! context.followsHostTransport())
            numRemembered = 0;

        for (const auto& event : input)
        {
            const int64_t eventTime = context.blockStartSample + event.samplePosition;
            sendScheduledEventsUpTo (eventTime, context, output);

            // A key played again before its note-off (e.g. two notes upstream landing on one pitch, ended by
            // one note-off): end the earlier note first, so the one note-off that follows ends this one.
            if (event.isNoteOnOrOff())
            {
                auto& isHeld = isKeyHeld[channelNoteIndex (event.channel(), event.noteNumber())];
                if (event.isNoteOn() && isHeld)
                    sendEvent (MidiEvent::noteOff (event.samplePosition, event.channel(), event.noteNumber()), output);
                isHeld = event.isNoteOn();
            }
            sendEvent (event, output); // the original always passes on time

            if (! event.isNoteOn())
                continue;
            const bool getsGhosts =
                randomUnitValue() * 100.0f < (float) integerParameter (parameterValues, chancePercent);
            const int velocity = std::max (
                1, (int) std::lround (event.velocity() * integerParameter (parameterValues, velocityPercent) / 100.0));

            if (getsGhosts && wantsAfter)
                for (int ghost = 1; ghost <= count; ++ghost)
                    scheduleGhost (eventTime + std::llround (ghost * distanceInBeats * context.samplesPerBeat()), event,
                                   velocity, ghostLength);

            if (context.followsHostTransport())
                remember (event, context.beatAtSample (event.samplePosition),
                          context.timeSignature.barLengthInQuarters(), getsGhosts, velocity);
        }
        sendScheduledEventsInBlock (context, output);
    }

private:
    enum Parameter
    {
        chancePercent,
        ghostPosition,
        distanceIndex,
        ghostCount,
        velocityPercent ///< page 2
    };
    enum Position
    {
        after,
        before,
        both
    };
    /** A note of the last bar, for the ghost notes before it in the next bar. */
    struct RememberedNote
    {
        double beat;
        double barLength; ///< Length of its bar in quarter notes, so "one bar later" follows the time signature
        int channel;
        int noteNumber;
        int ghostVelocity;
        bool getsGhosts;
        int ghostsPlayed; ///< Bit per ghost (1 = closest) already scheduled in the next bar
    };

    static constexpr double ghostDistances[] = { 0.125, 1.0 / 6.0, 0.25, 0.5 }; // 1/32, 1/16T, 1/16, 1/8
    static constexpr double ghostLengthInBeats = 0.0625;                        // 1/64
    static constexpr int maxRememberedNotes = 128;

    void scheduleGhost (int64_t time, const MidiEvent& original, int velocity, int64_t length)
    {
        scheduleEvent (time, MidiEvent::noteOn (0, original.channel(), original.noteNumber(), velocity));
        scheduleEvent (time + length, MidiEvent::noteOff (0, original.channel(), original.noteNumber()));
    }

    void remember (const MidiEvent& noteOn, double beat, double barLength, bool getsGhosts, int ghostVelocity)
    {
        if (numRemembered == maxRememberedNotes)
        {
            std::move (remembered.begin() + 1, remembered.end(), remembered.begin()); // drop the oldest
            --numRemembered;
        }
        remembered[(size_t) numRemembered++] = {
            beat, barLength, noteOn.channel(), noteOn.noteNumber(), ghostVelocity, getsGhosts, 0
        };
    }

    /** Plays the ghosts that fall into this block before the places where notes fell one bar ago. */
    void scheduleGhostsBeforeRememberedNotes (const BlockContext& context, double distanceInBeats, int count,
                                              int64_t ghostLength)
    {
        const double blockStartBeat = context.ppqPositionAtBlockStart;
        const double blockEndBeat = context.beatAtSample (context.numSamples);
        int kept = 0;
        for (int index = 0; index < numRemembered; ++index)
        {
            auto& note = remembered[(size_t) index];
            const double nextBarBeat = note.beat + note.barLength;
            if (nextBarBeat < blockStartBeat) // its place has passed (or the host jumped): forget it
                continue;
            for (int ghost = 1; ghost <= count && note.getsGhosts; ++ghost)
            {
                const double ghostBeat = nextBarBeat - ghost * distanceInBeats;
                const int ghostBit = 1 << ghost;
                if ((note.ghostsPlayed & ghostBit) != 0 || ghostBeat < blockStartBeat || ghostBeat >= blockEndBeat)
                    continue;
                note.ghostsPlayed |= ghostBit;
                const int64_t time =
                    context.blockStartSample + std::llround ((ghostBeat - blockStartBeat) * context.samplesPerBeat());
                scheduleGhost (time, MidiEvent::noteOn (0, note.channel, note.noteNumber, note.ghostVelocity),
                               note.ghostVelocity, ghostLength);
            }
            remembered[(size_t) kept++] = note;
        }
        numRemembered = kept;
    }

    std::array<RememberedNote, maxRememberedNotes> remembered {};
    int numRemembered = 0;
    std::array<bool, numChannelNoteCombinations> isKeyHeld {}; ///< Original notes sounding (not the ghosts)
};

} // namespace

std::unique_ptr<MidiModule> createGeneratorModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Arpeggiator: return std::make_unique<Arpeggiator>();
        case ModuleType::Chord: return std::make_unique<Chord>();
        case ModuleType::NoteEcho: return std::make_unique<NoteEcho>();
        case ModuleType::Repeater: return std::make_unique<Repeater>();
        case ModuleType::Strum: return std::make_unique<Strum>();
        case ModuleType::GhostNotes: return std::make_unique<GhostNotes>();
        default: return nullptr;
    }
}

} // namespace midiforge
