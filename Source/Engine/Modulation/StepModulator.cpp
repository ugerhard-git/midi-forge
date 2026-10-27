/**
 * @file StepModulator.cpp
 * @brief Implementation of StepModulator.
 */
#include "StepModulator.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace midiforge
{

const NoteDivision stepDivisions[] = {
    { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/16D", 0.375 },
    { "1/8", 0.5 },    { "1/4T", 2.0 / 3.0 },  { "1/8D", 0.75 }, { "1/4", 1.0 },        { "1/2T", 4.0 / 3.0 },
    { "1/4D", 1.5 },   { "1/2", 2.0 },         { "1 Bar", 4.0 },
};
const int numStepDivisions = (int) std::size (stepDivisions);

namespace
{
double stepLengthInBeats (const StepModulatorSettings& settings) noexcept
{
    return stepDivisions[std::clamp (settings.divisionIndex, 0, numStepDivisions - 1)].lengthInBeats;
}

int clampedNumberOfSteps (const StepModulatorSettings& settings) noexcept
{
    return std::clamp (settings.numSteps, minimumSteps, maximumSteps);
}
} // namespace

void StepModulator::reset() noexcept
{
    freeRunningSteps = 0.0;
    restartBeat = 0.0;
}

void StepModulator::restart (const BlockContext& context) noexcept
{
    freeRunningSteps = 0.0;
    restartBeat = context.followsHostTransport() ? context.ppqPositionAtBlockStart : 0.0;
}

double StepModulator::stepPositionAt (const StepModulatorSettings& settings, const BlockContext& context) const noexcept
{
    if (context.followsHostTransport())
        return (context.ppqPositionAtBlockStart - restartBeat) / stepLengthInBeats (settings);
    return freeRunningSteps;
}

int StepModulator::currentStepAt (const StepModulatorSettings& settings, const BlockContext& context) const noexcept
{
    const int numSteps = clampedNumberOfSteps (settings);
    const auto stepNumber = (int64_t) std::floor (stepPositionAt (settings, context));
    return (int) (((stepNumber % numSteps) + numSteps) % numSteps);
}

float StepModulator::valueAtStepPosition (const StepModulatorSettings& settings, double stepPosition) noexcept
{
    const int numSteps = clampedNumberOfSteps (settings);
    const double stepNumber = std::floor (stepPosition);
    const double withinStep = stepPosition - stepNumber; // 0..1
    const auto step = (int) ((((int64_t) stepNumber % numSteps) + numSteps) % numSteps);
    const float value = std::clamp (settings.stepValues[step], -1.0f, 1.0f);

    const double glide = std::clamp (settings.glide, 0.0, 1.0);
    const double glideStart = 1.0 - glide;
    if (glide <= 0.0 || withinStep < glideStart)
        return value;

    // Slide to the next step's value with an S-shaped curve during the last `glide` part of the step.
    const float nextValue = std::clamp (settings.stepValues[(step + 1) % numSteps], -1.0f, 1.0f);
    const double progress = (withinStep - glideStart) / glide;
    const double smoothed = progress * progress * (3.0 - 2.0 * progress);
    return value + (nextValue - value) * (float) smoothed;
}

float StepModulator::valueAt (const StepModulatorSettings& settings, const BlockContext& context) const noexcept
{
    return valueAtStepPosition (settings, stepPositionAt (settings, context));
}

void StepModulator::advance (const StepModulatorSettings& settings, const BlockContext& context) noexcept
{
    if (context.followsHostTransport())
    {
        // Mirror the host position, so the steps continue smoothly when the transport stops.
        freeRunningSteps =
            (context.ppqPositionAtBlockStart - restartBeat + context.numSamples / context.samplesPerBeat()) /
            stepLengthInBeats (settings);
    }
    else
    {
        freeRunningSteps += context.numSamples / context.samplesPerBeat() / stepLengthInBeats (settings);
    }

    // Keep the number small; whole multiples of every possible step count (lcm of 4..16 = 720720) keep the step.
    constexpr double wrapPeriod = 720720.0;
    if (freeRunningSteps >= wrapPeriod)
        freeRunningSteps -= wrapPeriod;
}

} // namespace midiforge
