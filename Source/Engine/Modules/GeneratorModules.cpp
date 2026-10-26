/**
 * @file GeneratorModules.cpp
 * @brief Modules that create new notes: Chord, Note Echo and Arpeggiator.
 */
#include "GeneratorModules.h"

#include <cmath>

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
        echoes.delayInSamples = std::max<int64_t> (1, std::llround (division.lengthInBeats * context.samplesPerBeat()));

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

} // namespace

std::unique_ptr<MidiModule> createGeneratorModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Arpeggiator: return std::make_unique<Arpeggiator>();
        case ModuleType::Chord: return std::make_unique<Chord>();
        case ModuleType::NoteEcho: return std::make_unique<NoteEcho>();
        default: return nullptr;
    }
}

} // namespace midiforge
