/**
 * @file FactoryPresets.cpp
 * @brief Preset definitions. Choice values are indices into the label lists in ModuleDescriptions.cpp
 *        and Lfo.cpp.
 */
#include "FactoryPresets.h"

#include <cmath>

namespace midiforge
{

const float keepDefault = NAN;

namespace
{
/** A tempo-synced LFO. */
LfoSettings syncedLfo (LfoWaveform waveform, int divisionIndex)
{
    LfoSettings settings;
    settings.waveform = waveform;
    settings.isTempoSynced = true;
    settings.divisionIndex = divisionIndex;
    return settings;
}
} // namespace

const std::vector<FactoryPreset>& getFactoryPresets()
{
    // Choice indices, named so the presets below read like the manual
    constexpr float major7Chord = 6, minor7Chord = 7, upDownPattern = 2, sixteenthRate = 3;
    constexpr float rootC = 0, rootD = 2, minorScale = 1, dorianScale = 2, minorPentatonicScale = 10;
    constexpr float dottedEighth = 6, quarterTriplet = 7, octavesMode = 1;
    constexpr int oneBar = 3, twoBars = 2;
    const float keep = keepDefault;

    // Parameter positions (A..D) used by the routes
    constexpr int chordInversion = 1, arpeggiatorGate = 3, echoPitch = 3;

    static const std::vector<FactoryPreset> presets = {
        { "Init", {}, {}, {} },

        { "Chord Arp",
          { { ModuleType::Chord, { minor7Chord, 0, 0, 90 } },
            { ModuleType::Arpeggiator, { sixteenthRate, upDownPattern, 2, 60 } },
            { ModuleType::Humanize, { 4, 8, keep, keep } } },
          {},
          {} },

        { "Scale Lock (C Minor)",
          { { ModuleType::ScaleQuantize, { rootC, minorScale, 0, keep } },
            { ModuleType::VelocityCurve, { -30, 20, 120, 0 } } },
          {},
          {} },

        { "Echo Cascade",
          { { ModuleType::NoteEcho, { dottedEighth, 6, 65, 7 } },
            { ModuleType::ScaleQuantize, { rootC, minorPentatonicScale, 0, keep } } },
          {},
          {} },

        { "Generative Dorian",
          { { ModuleType::Probability, { 60, keep, keep, keep } },
            { ModuleType::RandomPitch, { 12, 40, octavesMode, keep } },
            { ModuleType::ScaleQuantize, { rootD, dorianScale, 0, keep } },
            { ModuleType::NoteEcho, { quarterTriplet, 3, 70, 0 } },
            { ModuleType::Humanize, { 8, 12, keep, keep } } },
          {},
          {} },

        { "Bass Split to Ch 2",
          { { ModuleType::NoteRange, { 0, 59, 0, keep } },
            { ModuleType::ChannelRemap, { 0, 2, keep, keep } },
            { ModuleType::VelocityCurve, { 0, 1, 127, 100 } } },
          {},
          {} },

        // LFO presets ----------------------------------------------------------------------------
        { "Breathing Arp",
          { { ModuleType::Chord, { major7Chord, 0, 0, 85 } },
            { ModuleType::Arpeggiator, { sixteenthRate, upDownPattern, 2, 50 } } },
          { { 0, syncedLfo (LfoWaveform::Triangle, twoBars) }, { 1, syncedLfo (LfoWaveform::SampleAndHold, oneBar) } },
          { { 0, 1, arpeggiatorGate, 45.0f },    // gate swells over two bars
            { 1, 0, chordInversion, 40.0f } } }, // a new inversion every bar

        { "Wandering Echo",
          { { ModuleType::NoteEcho, { dottedEighth, 5, 70, 0 } },
            { ModuleType::ScaleQuantize, { rootC, minorPentatonicScale, 0, keep } } },
          { { 0, syncedLfo (LfoWaveform::SmoothRandom, oneBar) } },
          { { 0, 0, echoPitch, 25.0f } } }, // echo pitch step drifts up and down
    };
    return presets;
}

} // namespace midiforge
