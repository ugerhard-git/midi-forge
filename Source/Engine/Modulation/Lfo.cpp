/**
 * @file Lfo.cpp
 * @brief Implementation of Lfo: waveforms, timing and the random waves.
 */
#include "Lfo.h"

#include <cmath>
#include <iterator>

namespace midiforge
{

const char* const lfoWaveformNames[numLfoWaveforms] = { "Sine",   "Triangle", "Saw Up",       "Saw Down",
                                                        "Square", "S&H",      "Smooth Random" };

const NoteDivision lfoDivisions[] = {
    { "8 Bars", 32.0, 8 }, { "4 Bars", 16.0, 4 }, { "2 Bars", 8.0, 2 },   { "1 Bar", 4.0, 1 },
    { "1/2", 2.0 },        { "1/2T", 4.0 / 3.0 }, { "1/4D", 1.5 },        { "1/4", 1.0 },
    { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 },      { "1/8", 0.5 },         { "1/8T", 1.0 / 3.0 },
    { "1/16D", 0.375 },    { "1/16", 0.25 },      { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },
};
const int numLfoDivisions = (int) std::size (lfoDivisions);

namespace
{
constexpr double twoPi = 6.283185307179586;

/** SplitMix64: a fast, well-distributed hash, used to turn (seed, cycle number) into a random value. */
uint64_t mixBits (uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

const NoteDivision& divisionOf (const LfoSettings& settings) noexcept
{
    return lfoDivisions[std::clamp (settings.divisionIndex, 0, numLfoDivisions - 1)];
}
} // namespace

void Lfo::reset() noexcept
{
    freeRunningCycles = 0.0;
    restartBeat = 0.0;
}

void Lfo::restart (const BlockContext& context) noexcept
{
    freeRunningCycles = 0.0;
    // While following the host, count cycles from the restart position; otherwise re-align to the bar
    // grid once the transport starts.
    restartBeat = context.followsHostTransport() ? context.ppqPositionAtBlockStart : 0.0;
}

double Lfo::cyclesPerSecond (const LfoSettings& settings, const BlockContext& context) noexcept
{
    if (settings.isTempoSynced)
        return (std::max (1.0, context.tempoBpm) / 60.0) /
               divisionLengthInBeats (divisionOf (settings), context.timeSignature);
    return std::clamp (settings.rateHz, 0.0, 1000.0);
}

double Lfo::cyclePositionAt (const LfoSettings& settings, const BlockContext& context) const noexcept
{
    const double phaseOffsetInCycles = settings.phaseDegrees / 360.0;

    if (settings.isTempoSynced && context.followsHostTransport())
    {
        return divisionsBetween (divisionOf (settings), context, restartBeat, context.ppqPositionAtBlockStart) +
               phaseOffsetInCycles;
    }
    return freeRunningCycles + phaseOffsetInCycles;
}

float Lfo::valueAt (const LfoSettings& settings, const BlockContext& context) const noexcept
{
    return waveformValue (settings.waveform, cyclePositionAt (settings, context), settings.pulseWidth);
}

float Lfo::unipolarValueAt (const LfoSettings& settings, const BlockContext& context) const noexcept
{
    const bool startsInTheMiddle = settings.waveform == LfoWaveform::Sine || settings.waveform == LfoWaveform::Triangle;
    const double quarterCycleEarlier = startsInTheMiddle ? 0.25 : 0.0;
    const float bipolar = waveformValue (settings.waveform, cyclePositionAt (settings, context) - quarterCycleEarlier,
                                         settings.pulseWidth);
    return (bipolar + 1.0f) * 0.5f;
}

void Lfo::advance (const LfoSettings& settings, const BlockContext& context) noexcept
{
    if (settings.isTempoSynced && context.followsHostTransport())
    {
        // Mirror the host position in our own clock, so the LFO continues smoothly if the transport stops.
        const double blockEndBeat = context.beatAtSample (context.numSamples);
        freeRunningCycles = divisionsBetween (divisionOf (settings), context, restartBeat, blockEndBeat);
    }
    else
    {
        freeRunningCycles += context.numSamples * cyclesPerSecond (settings, context) / context.sampleRate;
    }

    // Keep the number small so precision does not degrade over long sessions. Random waves use the
    // cycle number, so only drop multiples of a large period.
    constexpr double wrapPeriod = 1048576.0; // 2^20 cycles
    if (freeRunningCycles >= wrapPeriod)
        freeRunningCycles -= wrapPeriod;
}

float Lfo::randomValueForCycle (int64_t cycleNumber) const noexcept
{
    const uint64_t hashed = mixBits (seed ^ mixBits ((uint64_t) cycleNumber));
    const double unitValue = (double) (hashed >> 11) / 9007199254740992.0; // 53 bits -> [0, 1)
    return (float) (unitValue * 2.0 - 1.0);
}

float Lfo::waveformValue (LfoWaveform waveform, double cyclePosition, double pulseWidth) const noexcept
{
    const double cycleNumber = std::floor (cyclePosition);
    const double phase = cyclePosition - cycleNumber; // 0..1 within the current cycle

    switch (waveform)
    {
        case LfoWaveform::Sine: return (float) std::sin (twoPi * phase);

        case LfoWaveform::Triangle:
        {
            // Starts at 0 rising, like the sine: 0 -> +1 at 1/4 -> -1 at 3/4 -> 0
            const double shifted = std::fmod (phase + 0.25, 1.0);
            return (float) (1.0 - 4.0 * std::abs (shifted - 0.5));
        }

        case LfoWaveform::SawUp: return (float) (2.0 * phase - 1.0);
        case LfoWaveform::SawDown: return (float) (1.0 - 2.0 * phase);

        case LfoWaveform::Square: return phase < std::clamp (pulseWidth, 0.01, 0.99) ? 1.0f : -1.0f;

        case LfoWaveform::SampleAndHold: return randomValueForCycle ((int64_t) cycleNumber);

        case LfoWaveform::SmoothRandom:
        {
            // Glide from the previous cycle's random value to this cycle's, with an S-shaped curve.
            const float previous = randomValueForCycle ((int64_t) cycleNumber - 1);
            const float current = randomValueForCycle ((int64_t) cycleNumber);
            const double smoothed = phase * phase * (3.0 - 2.0 * phase);
            return (float) (previous + (current - previous) * smoothed);
        }

        case LfoWaveform::Count: break;
    }
    return 0.0f;
}

} // namespace midiforge
