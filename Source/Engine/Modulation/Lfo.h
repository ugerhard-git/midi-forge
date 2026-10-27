/**
 * @file Lfo.h
 * @brief A low-frequency oscillator that produces a control signal (-1..+1) for modulating slot parameters.
 *
 * The LFOs make no sound: their output only moves the A-D settings of the MIDI modules.
 */
#pragma once

#include "Engine/MidiEvent.h"
#include "Engine/ModuleDescriptions.h"

#include <cstdint>

namespace midiforge
{

/** The LFO wave shapes. Stored in projects: only append. */
enum class LfoWaveform : int
{
    Sine = 0,
    Triangle,
    SawUp,
    SawDown,
    Square,
    SampleAndHold, ///< A new random value every cycle, held for the whole cycle.
    SmoothRandom,  ///< A new random value every cycle, gliding smoothly from the previous one.

    Count
};

constexpr int numLfoWaveforms = (int) LfoWaveform::Count;

/** Display names of the waveforms, indexed by LfoWaveform. */
extern const char* const lfoWaveformNames[numLfoWaveforms];

/** Cycle lengths for tempo-synced LFOs, from 8 bars to 1/32. Stored in projects: only append. */
extern const NoteDivision lfoDivisions[];
extern const int numLfoDivisions;
/** Index of "1 Bar" in lfoDivisions, the default. */
constexpr int defaultLfoDivisionIndex = 3;

/** The user settings of one LFO, as plain values. */
struct LfoSettings
{
    LfoWaveform waveform = LfoWaveform::Sine;
    bool isTempoSynced = true;                   ///< True: cycle length from divisionIndex; false: rateHz.
    double rateHz = 1.0;                         ///< Cycles per second when not tempo synced.
    int divisionIndex = defaultLfoDivisionIndex; ///< Index into lfoDivisions when tempo synced.
    double phaseDegrees = 0.0;                   ///< 0..360, shifts the wave.
    double pulseWidth = 0.5;                     ///< 0.05..0.95, share of the cycle the Square wave is high.
    bool restartsOnFirstKey = false;             ///< Restart the cycle when the first key is pressed.
};

/**
 * @brief One LFO. Keeps its position in the cycle and computes its output for a stretch of time.
 *
 * Timing: while the host transport runs, a tempo-synced LFO follows the host's musical position, so it
 * stays locked to the bar grid even when you jump around in the song. Otherwise (free rate, or transport
 * stopped) it runs on its own clock.
 */
class Lfo
{
public:
    /** @param randomSeed Seed for the random waveforms, so tests are reproducible. */
    explicit Lfo (uint64_t randomSeed = 1) : seed (randomSeed) {}

    /** Back to the start of the cycle, without any restart anchor. */
    void reset() noexcept;

    /**
     * Restarts the cycle at the start of the given context (used for "restart on first key").
     * Tempo-synced LFOs remember the host position of the restart and count cycles from there.
     */
    void restart (const BlockContext& context) noexcept;

    /** @return The LFO output (-1..+1) at the start of the given stretch of time. */
    float valueAt (const LfoSettings& settings, const BlockContext& context) const noexcept;

    /**
     * @return The LFO output for unipolar routes (0..+1) at the start of the given stretch of time.
     *         Sine and Triangle are read a quarter cycle earlier, so their cycle starts at the lowest point.
     */
    float unipolarValueAt (const LfoSettings& settings, const BlockContext& context) const noexcept;

    /** @return The position in cycles (including the phase offset) at the start of the given stretch. */
    double cyclePositionAt (const LfoSettings& settings, const BlockContext& context) const noexcept;

    /** Moves the LFO's own clock forward by the length of the given stretch of time. */
    void advance (const LfoSettings& settings, const BlockContext& context) noexcept;

    /** @return The wave's value (-1..+1) at a position measured in cycles (1.0 = one full cycle). */
    float waveformValue (LfoWaveform waveform, double cyclePosition, double pulseWidth) const noexcept;

    /** @return Cycles per second for the given settings and tempo. */
    static double cyclesPerSecond (const LfoSettings& settings, double tempoBpm) noexcept;

private:
    /** A random value in -1..+1 that depends only on the cycle number and the seed. */
    float randomValueForCycle (int64_t cycleNumber) const noexcept;

    uint64_t seed;
    double freeRunningCycles = 0.0; ///< Own clock, in cycles since the last reset or restart.
    double restartBeat = 0.0;       ///< Host position (quarter notes) of the last restart.
};

} // namespace midiforge
