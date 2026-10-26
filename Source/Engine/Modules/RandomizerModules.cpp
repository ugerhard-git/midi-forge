/**
 * @file RandomizerModules.cpp
 * @brief Modules that add controlled randomness: Humanize, Probability, Random Pitch.
 */
#include "RandomizerModules.h"

#include <cmath>

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

} // namespace

std::unique_ptr<MidiModule> createRandomizerModule (ModuleType type)
{
    switch (type)
    {
        case ModuleType::Humanize: return std::make_unique<Humanize>();
        case ModuleType::Probability: return std::make_unique<Probability>();
        case ModuleType::RandomPitch: return std::make_unique<RandomPitch>();
        default: return nullptr;
    }
}

} // namespace midiforge
