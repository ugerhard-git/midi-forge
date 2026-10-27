/**
 * @file StepModulator.h
 * @brief A step sequencer as modulation source: 4 to 16 steps, each with its own value (-1..+1), played in
 *        time with the song and routed like an LFO.
 */
#pragma once

#include "Engine/MidiEvent.h"
#include "Engine/ModuleDescriptions.h"

namespace midiforge
{

constexpr int minimumSteps = 4;
constexpr int maximumSteps = 16;

/** Step lengths of the step modulators, short to long. Stored in projects: only append. */
extern const NoteDivision stepDivisions[];
extern const int numStepDivisions;
/** Index of "1/16" in stepDivisions, the default. */
constexpr int defaultStepDivisionIndex = 2;

/** The user settings of one step modulator, as plain values. */
struct StepModulatorSettings
{
    int numSteps = maximumSteps;                  ///< minimumSteps..maximumSteps
    float stepValues[maximumSteps] {};            ///< -1..+1 per step
    int divisionIndex = defaultStepDivisionIndex; ///< Length of one step, index into stepDivisions
    double glide = 0.0;                           ///< 0..1: share of each step spent sliding to the next value
    bool restartsOnFirstKey = false;              ///< Start again at step 1 with the first key of a phrase
};

/**
 * @brief One step modulator. Its timing works like a tempo-synced LFO: while the host transport runs it
 *        follows the host's musical position (step 1 on the bar grid), otherwise it runs on its own clock.
 */
class StepModulator
{
public:
    /** Back to step 1, without any restart anchor. */
    void reset() noexcept;

    /** Starts again at step 1 at the start of the given context ("restart on key"). */
    void restart (const BlockContext& context) noexcept;

    /** @return The position in steps since the start (or restart) at the start of the given stretch of time. */
    double stepPositionAt (const StepModulatorSettings& settings, const BlockContext& context) const noexcept;

    /** @return The output (-1..+1) at the start of the given stretch of time. */
    float valueAt (const StepModulatorSettings& settings, const BlockContext& context) const noexcept;

    /** @return The step (0..numSteps-1) playing at the start of the given stretch of time. */
    int currentStepAt (const StepModulatorSettings& settings, const BlockContext& context) const noexcept;

    /** Moves the own clock forward by the length of the given stretch of time. */
    void advance (const StepModulatorSettings& settings, const BlockContext& context) noexcept;

    /**
     * @return The output for a position in steps: the step's value, and with glide a smooth slide to the
     *         next step's value during the last part of the step.
     */
    static float valueAtStepPosition (const StepModulatorSettings& settings, double stepPosition) noexcept;

private:
    double freeRunningSteps = 0.0; ///< Own clock, in steps since the last reset or restart.
    double restartBeat = 0.0;      ///< Host position (quarter notes) of the last restart.
};

} // namespace midiforge
