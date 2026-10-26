/**
 * @file FactoryPresets.cpp
 * @brief Preset definitions. Choice values are indices into the label lists in ModuleDescriptions.cpp.
 */
#include "FactoryPresets.h"

#include <cmath>

namespace midiforge
{

const float keepDefault = NAN;

const std::vector<FactoryPreset>& getFactoryPresets()
{
    constexpr float minor7Chord = 7, upDownPattern = 2, sixteenthRate = 3;
    constexpr float rootC = 0, rootD = 2, minorScale = 1, dorianScale = 2, minorPentatonicScale = 10;
    constexpr float dottedEighth = 6, quarterTriplet = 7, octavesMode = 1;
    const float keep = keepDefault;

    static const std::vector<FactoryPreset> presets = {
        { "Init", {} },

        { "Chord Arp",
          { { ModuleType::Chord, { minor7Chord, 0, 0, 90 } },
            { ModuleType::Arpeggiator, { sixteenthRate, upDownPattern, 2, 60 } },
            { ModuleType::Humanize, { 4, 8, keep, keep } } } },

        { "Scale Lock (C Minor)",
          { { ModuleType::ScaleQuantize, { rootC, minorScale, 0, keep } },
            { ModuleType::VelocityCurve, { -30, 20, 120, 0 } } } },

        { "Echo Cascade",
          { { ModuleType::NoteEcho, { dottedEighth, 6, 65, 7 } },
            { ModuleType::ScaleQuantize, { rootC, minorPentatonicScale, 0, keep } } } },

        { "Generative Dorian",
          { { ModuleType::Probability, { 60, keep, keep, keep } },
            { ModuleType::RandomPitch, { 12, 40, octavesMode, keep } },
            { ModuleType::ScaleQuantize, { rootD, dorianScale, 0, keep } },
            { ModuleType::NoteEcho, { quarterTriplet, 3, 70, 0 } },
            { ModuleType::Humanize, { 8, 12, keep, keep } } } },

        { "Bass Split to Ch 2",
          { { ModuleType::NoteRange, { 0, 59, 0, keep } },
            { ModuleType::ChannelRemap, { 0, 2, keep, keep } },
            { ModuleType::VelocityCurve, { 0, 1, 127, 100 } } } },
    };
    return presets;
}

} // namespace midiforge
