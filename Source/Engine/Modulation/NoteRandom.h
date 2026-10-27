/**
 * @file NoteRandom.h
 * @brief A modulation source that rolls a new random value when a note is played and holds it until the
 *        next note event. Notes that arrive within the chord window after the first share its value, so a
 *        slightly uneven chord counts as one event.
 */
#pragma once

#include <cstdint>

namespace midiforge
{

/** When a Note Random modulator rolls a new value. Stored in projects: only append. */
enum class NoteRandomTrigger : int
{
    EveryNote = 0,         ///< Every played note (outside the chord window of the previous roll)
    FirstNoteOfPhrase = 1, ///< Only a note pressed while no other key is held
};

/** The user settings of one Note Random modulator. */
struct NoteRandomSettings
{
    double chordWindowMilliseconds = 30.0; ///< 0..100: notes this close to the first share its value
    NoteRandomTrigger trigger = NoteRandomTrigger::EveryNote;
    int numSteps = 0; ///< 0 = smooth (any value), otherwise 2..12 evenly spaced values
};

constexpr double maximumChordWindowMilliseconds = 100.0;
constexpr int minimumNoteRandomSteps = 2;
constexpr int maximumNoteRandomSteps = 12;

/**
 * @brief One Note Random modulator. The engine calls noteStarted() for every note played into the plug-in.
 *
 * Real-time safe and allocation free. The random sequence is reproducible from the seed.
 */
class NoteRandom
{
public:
    explicit NoteRandom (uint64_t randomSeed = 1) : randomState (randomSeed) {}

    /** Back to 0, as if no note had been played. */
    void reset() noexcept;

    /**
     * Tells the modulator a note was played.
     * @param sampleTime        Absolute time of the note-on in samples.
     * @param isFirstKeyOfPhrase True if no other key was held.
     * @param sampleRate        To convert the chord window to samples.
     * @return True if a new value was rolled.
     */
    bool noteStarted (int64_t sampleTime, bool isFirstKeyOfPhrase, const NoteRandomSettings& settings,
                      double sampleRate) noexcept;

    /** @return The current value, -1..+1 (0 before the first note). */
    float getValue() const noexcept { return currentValue; }

    /** Turns a uniform random number in [0, 1) into the modulator value (-1..+1), applying the steps setting. */
    static float valueFromUniform (double uniform, int numSteps) noexcept;

private:
    /** @return A uniformly distributed number in [0, 1). */
    double nextUniform() noexcept;

    uint64_t randomState;
    float currentValue = 0.0f;
    bool hasRolled = false;
    int64_t timeOfLastRoll = 0;
};

} // namespace midiforge
