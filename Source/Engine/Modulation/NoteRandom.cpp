/**
 * @file NoteRandom.cpp
 * @brief Implementation of NoteRandom.
 */
#include "NoteRandom.h"

#include <algorithm>
#include <cmath>

namespace midiforge
{

void NoteRandom::reset() noexcept
{
    currentValue = 0.0f;
    hasRolled = false;
    timeOfLastRoll = 0;
}

double NoteRandom::nextUniform() noexcept
{
    // SplitMix64 step: fast, good distribution, no allocation.
    randomState += 0x9E3779B97F4A7C15ull;
    uint64_t mixed = randomState;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
    mixed ^= mixed >> 31;
    return (double) (mixed >> 11) / 9007199254740992.0; // 53 bits -> [0, 1)
}

float NoteRandom::valueFromUniform (double uniform, int numSteps) noexcept
{
    uniform = std::clamp (uniform, 0.0, std::nextafter (1.0, 0.0));
    if (numSteps < minimumNoteRandomSteps)
        return (float) (uniform * 2.0 - 1.0);

    // numSteps evenly spaced values from -1 to +1, each equally likely.
    const int steps = std::min (numSteps, maximumNoteRandomSteps);
    const int stepIndex = std::min ((int) (uniform * steps), steps - 1);
    return (float) (-1.0 + 2.0 * stepIndex / (steps - 1));
}

bool NoteRandom::noteStarted (int64_t sampleTime, bool isFirstKeyOfPhrase, const NoteRandomSettings& settings,
                              double sampleRate) noexcept
{
    if (settings.trigger == NoteRandomTrigger::FirstNoteOfPhrase && ! isFirstKeyOfPhrase)
        return false;

    // Notes within the chord window after the note that rolled belong to the same chord.
    const double windowMilliseconds =
        std::clamp (settings.chordWindowMilliseconds, 0.0, maximumChordWindowMilliseconds);
    const auto windowInSamples = (int64_t) std::llround (windowMilliseconds * sampleRate / 1000.0);
    if (hasRolled && sampleTime >= timeOfLastRoll && sampleTime - timeOfLastRoll <= windowInSamples)
        return false;

    currentValue = valueFromUniform (nextUniform(), settings.numSteps);
    hasRolled = true;
    timeOfLastRoll = sampleTime;
    return true;
}

} // namespace midiforge
