/**
 * @file ModuleDescriptions.cpp
 * @brief The table of all modules and their parameters, plus value conversion and formatting.
 *
 * When you change anything in this file, update docs/MANUAL.md: the manual lists every parameter.
 */
#include "ModuleDescriptions.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>

namespace midiforge
{

namespace
{
/** Builds a scale bit mask from a list of semitone offsets. */
constexpr unsigned short makeSemitoneMask (std::initializer_list<int> semitones)
{
    unsigned short mask = 0;
    for (int semitone : semitones)
        mask = (unsigned short) (mask | (1u << semitone));
    return mask;
}

//==============================================================================
// Choice labels. Their order is stored in projects, so only append.

const char* const passOrBlockInsideLabels[] = { "Pass inside", "Block inside" };
const char* const onlyOrExceptLabels[] = { "Only this", "All except" };
const char* const passOrBlockLabels[] = { "Pass", "Block" };
const char* const pitchClassLabels[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const char* const snapDirectionLabels[] = { "Nearest", "Up", "Down" };
const char* const arpeggiatorPatternLabels[] = { "Up", "Down", "Up/Down", "Random", "As Played" };
const char* const chordVoicingLabels[] = { "Close", "Open", "Bass -1 Oct" };
const char* const randomPitchModeLabels[] = { "Any", "Octaves", "5ths + Oct" };
const char* const repeaterDivisionLabels[] = { "1/4D", "1/4",  "1/8D",  "1/4T", "1/8",   "1/16D",
                                               "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
const char* const repeaterVelocityLabels[] = { "Last Note", "Aftertouch" };
const char* const noteSplitOctaveLabels[] = { "This octave", "Every octave" };
const char* const repeaterAccentLabels[] = { "Off", "Every 2nd", "Every 3rd", "Every 4th", "On Beat" };
const char* const echoDivisionLabels[] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
                                           "1/8D", "1/4T",  "1/4",  "1/4D",  "1/2",  "1 Bar" };
const char* const arpeggiatorDivisionLabels[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
const char* const scaleLabels[] = { "Major",       "Minor",   "Dorian",      "Phrygian",   "Lydian",
                                    "Mixolydian",  "Locrian", "Harm. Minor", "Mel. Minor", "Major Pent.",
                                    "Minor Pent.", "Blues",   "Whole Tone" };
const char* const chordLabels[] = { "Major", "Minor", "Sus2", "Sus4", "Dim",   "Aug",    "Maj7",
                                    "Min7",  "Dom7",  "Min9", "Add9", "Power", "Octave", "5th + Oct" };

//==============================================================================
// Small builders that keep the module table below readable.

constexpr ParameterDescription unusedParameter()
{
    return {};
}

ParameterDescription integerParameter (const char* name, float minimum, float maximum, float defaultValue,
                                       const char* unitSuffix = "", const char* labelForMinimum = nullptr)
{
    return { name, ParameterKind::Integer, minimum, maximum, defaultValue, nullptr, unitSuffix, labelForMinimum };
}

ParameterDescription continuousParameter (const char* name, float minimum, float maximum, float defaultValue,
                                          const char* unitSuffix)
{
    return { name, ParameterKind::Continuous, minimum, maximum, defaultValue, nullptr, unitSuffix, nullptr };
}

ParameterDescription percentParameter (const char* name, float minimum, float maximum, float defaultValue)
{
    return { name, ParameterKind::Percent, minimum, maximum, defaultValue, nullptr, "%", nullptr };
}

ParameterDescription noteParameter (const char* name, float defaultNoteNumber)
{
    return { name, ParameterKind::NoteNumber, 0, 127, defaultNoteNumber, nullptr, "", nullptr };
}

template <size_t NumLabels>
ParameterDescription choiceParameter (const char* name, const char* const (&labels)[NumLabels], int defaultIndex)
{
    return { name, ParameterKind::Choice, 0, (float) (NumLabels - 1), (float) defaultIndex, labels, "", nullptr };
}

/** The module table, indexed by ModuleType. */
const ModuleDescription* createModuleTable()
{
    static const ModuleDescription table[numModuleTypes] = {
        { "Empty",
          ModuleCategory::None,
          "Empty slot: events pass through unchanged.",
          { unusedParameter(), unusedParameter(), unusedParameter(), unusedParameter() } },

        // Filters ------------------------------------------------------------------------------------
        { "Note Range",
          ModuleCategory::Filter,
          "Passes or blocks notes inside a key range.",
          { noteParameter ("Low", 0), noteParameter ("High", 127), choiceParameter ("Mode", passOrBlockInsideLabels, 0),
            unusedParameter() } },
        { "Velocity Range",
          ModuleCategory::Filter,
          "Passes or blocks notes inside a velocity range.",
          { integerParameter ("Min", 1, 127, 1), integerParameter ("Max", 1, 127, 127),
            choiceParameter ("Mode", passOrBlockInsideLabels, 0), unusedParameter() } },
        { "Channel Filter",
          ModuleCategory::Filter,
          "Keeps only one MIDI channel, or removes it.",
          { integerParameter ("Channel", 1, 16, 1), choiceParameter ("Mode", onlyOrExceptLabels, 0), unusedParameter(),
            unusedParameter() } },
        { "Event Blocker",
          ModuleCategory::Filter,
          "Blocks whole classes of MIDI events.",
          { choiceParameter ("Notes", passOrBlockLabels, 0), choiceParameter ("CC", passOrBlockLabels, 0),
            choiceParameter ("Pitch Bend", passOrBlockLabels, 0),
            choiceParameter ("AT / Prog", passOrBlockLabels, 0) } },

        // Transforms ---------------------------------------------------------------------------------
        { "Transpose",
          ModuleCategory::Transform,
          "Shifts notes by semitones and octaves.",
          { integerParameter ("Semitones", -24, 24, 0, " st"), integerParameter ("Octave", -4, 4, 0, " oct"),
            unusedParameter(), unusedParameter() } },
        { "Scale Quantize",
          ModuleCategory::Transform,
          "Forces notes into a key and scale.",
          { choiceParameter ("Root", pitchClassLabels, 0), choiceParameter ("Scale", scaleLabels, 0),
            choiceParameter ("Snap", snapDirectionLabels, 0), unusedParameter() } },
        { "Velocity Curve",
          ModuleCategory::Transform,
          "Reshapes, compresses or fixes note velocities.",
          { integerParameter ("Curve", -100, 100, 0), integerParameter ("Out Min", 1, 127, 1),
            integerParameter ("Out Max", 1, 127, 127), integerParameter ("Fixed", 0, 127, 0, "", "Off") } },
        { "Channel Remap",
          ModuleCategory::Transform,
          "Moves events from one channel to another.",
          { integerParameter ("From", 0, 16, 0, "", "All"), integerParameter ("To", 1, 16, 1), unusedParameter(),
            unusedParameter() } },

        // Generators ---------------------------------------------------------------------------------
        { "Arpeggiator",
          ModuleCategory::Generator,
          "Turns held notes into a tempo-synced pattern.",
          { choiceParameter ("Rate", arpeggiatorDivisionLabels, 3),
            choiceParameter ("Pattern", arpeggiatorPatternLabels, 0), integerParameter ("Octaves", 1, 4, 1),
            percentParameter ("Gate", 5, 100, 50) } },
        { "Chord",
          ModuleCategory::Generator,
          "Builds a chord on every incoming note.",
          { choiceParameter ("Type", chordLabels, 0), integerParameter ("Inversion", 0, 3, 0),
            choiceParameter ("Voicing", chordVoicingLabels, 0), percentParameter ("Added Vel", 0, 100, 100) } },
        { "Note Echo",
          ModuleCategory::Generator,
          "Tempo-synced MIDI delay with pitch shift.",
          { choiceParameter ("Time", echoDivisionLabels, 5), integerParameter ("Repeats", 1, 16, 3),
            percentParameter ("Feedback", 10, 100, 70), integerParameter ("Pitch", -12, 12, 0, " st") } },

        // Randomizers --------------------------------------------------------------------------------
        { "Humanize",
          ModuleCategory::Randomizer,
          "Adds random timing delay and velocity variation.",
          { continuousParameter ("Timing", 0, 50, 10, " ms"), integerParameter ("Velocity", 0, 64, 10),
            unusedParameter(), unusedParameter() } },
        { "Probability",
          ModuleCategory::Randomizer,
          "Lets each note through with a given chance.",
          { percentParameter ("Chance", 0, 100, 75), unusedParameter(), unusedParameter(), unusedParameter() } },
        { "Random Pitch",
          ModuleCategory::Randomizer,
          "Randomly shifts some of the notes.",
          { integerParameter ("Range", 0, 24, 12, " st"), percentParameter ("Chance", 0, 100, 50),
            choiceParameter ("Mode", randomPitchModeLabels, 1), unusedParameter() } },

        // Added in 0.4.0 ---------------------------------------------------------------------------
        { "Repeater",
          ModuleCategory::Generator,
          "Retriggers held notes on the tempo grid (ratchets).",
          { choiceParameter ("Rate", repeaterDivisionLabels, 7),
            choiceParameter ("Velocity", repeaterVelocityLabels, 0), percentParameter ("Swing", 50, 75, 50),
            choiceParameter ("Accent", repeaterAccentLabels, 0),
            // Page 2 (added in 0.7.0)
            integerParameter ("Accent Boost", 0, 64, 25), percentParameter ("Length", 10, 100, 50) } },

        // Added in 0.5.0 ---------------------------------------------------------------------------
        { "Note Split",
          ModuleCategory::Filter,
          "Selected notes continue down the chain; all others go straight out.",
          { noteParameter ("Low", 60), noteParameter ("High", 60),
            choiceParameter ("Octaves", noteSplitOctaveLabels, 0), unusedParameter() } },
    };
    return table;
}
} // namespace

//==============================================================================
// Tables (their order must match the label arrays above)

const NoteDivision echoDivisions[] = {
    { "1/32", 0.125 },     { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
    { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 },         { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 },
    { "1/4", 1.0 },        { "1/4D", 1.5 },        { "1/2", 2.0 },   { "1 Bar", 4.0 },
};
const int numEchoDivisions = (int) std::size (echoDivisions);

const NoteDivision arpeggiatorDivisions[] = {
    { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },
};
const int numArpeggiatorDivisions = (int) std::size (arpeggiatorDivisions);

// Ordered from slow to fast, so turning the knob to the right speeds up the ratchet.
const NoteDivision repeaterDivisions[] = {
    { "1/4D", 1.5 },        { "1/4", 1.0 },     { "1/8D", 0.75 },        { "1/4T", 2.0 / 3.0 },
    { "1/8", 0.5 },         { "1/16D", 0.375 }, { "1/8T", 1.0 / 3.0 },   { "1/16", 0.25 },
    { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 },  { "1/32T", 1.0 / 12.0 }, { "1/64", 0.0625 },
};
const int numRepeaterDivisions = (int) std::size (repeaterDivisions);

const Scale scales[] = {
    { "Major", makeSemitoneMask ({ 0, 2, 4, 5, 7, 9, 11 }) },
    { "Minor", makeSemitoneMask ({ 0, 2, 3, 5, 7, 8, 10 }) },
    { "Dorian", makeSemitoneMask ({ 0, 2, 3, 5, 7, 9, 10 }) },
    { "Phrygian", makeSemitoneMask ({ 0, 1, 3, 5, 7, 8, 10 }) },
    { "Lydian", makeSemitoneMask ({ 0, 2, 4, 6, 7, 9, 11 }) },
    { "Mixolydian", makeSemitoneMask ({ 0, 2, 4, 5, 7, 9, 10 }) },
    { "Locrian", makeSemitoneMask ({ 0, 1, 3, 5, 6, 8, 10 }) },
    { "Harm. Minor", makeSemitoneMask ({ 0, 2, 3, 5, 7, 8, 11 }) },
    { "Mel. Minor", makeSemitoneMask ({ 0, 2, 3, 5, 7, 9, 11 }) },
    { "Major Pent.", makeSemitoneMask ({ 0, 2, 4, 7, 9 }) },
    { "Minor Pent.", makeSemitoneMask ({ 0, 3, 5, 7, 10 }) },
    { "Blues", makeSemitoneMask ({ 0, 3, 5, 6, 7, 10 }) },
    { "Whole Tone", makeSemitoneMask ({ 0, 2, 4, 6, 8, 10 }) },
};
const int numScales = (int) std::size (scales);

const ChordShape chordShapes[] = {
    { "Major", 3, { 0, 4, 7 } },        { "Minor", 3, { 0, 3, 7 } },      { "Sus2", 3, { 0, 2, 7 } },
    { "Sus4", 3, { 0, 5, 7 } },         { "Dim", 3, { 0, 3, 6 } },        { "Aug", 3, { 0, 4, 8 } },
    { "Maj7", 4, { 0, 4, 7, 11 } },     { "Min7", 4, { 0, 3, 7, 10 } },   { "Dom7", 4, { 0, 4, 7, 10 } },
    { "Min9", 5, { 0, 3, 7, 10, 14 } }, { "Add9", 4, { 0, 4, 7, 14 } },   { "Power", 2, { 0, 7 } },
    { "Octave", 2, { 0, 12 } },         { "5th + Oct", 3, { 0, 7, 12 } },
};
const int numChordShapes = (int) std::size (chordShapes);

static_assert (std::size (echoDivisionLabels) == std::size (echoDivisions), "echo labels and table differ");
static_assert (std::size (arpeggiatorDivisionLabels) == std::size (arpeggiatorDivisions), "arp labels differ");
static_assert (std::size (repeaterDivisionLabels) == std::size (repeaterDivisions), "repeater labels differ");
static_assert (std::size (scaleLabels) == std::size (scales), "scale labels and table differ");
static_assert (std::size (chordLabels) == std::size (chordShapes), "chord labels and table differ");

//==============================================================================

const ModuleDescription& getModuleDescription (ModuleType type) noexcept
{
    return getModuleDescription ((int) type);
}

const ModuleDescription& getModuleDescription (int typeIndex) noexcept
{
    static const ModuleDescription* const table = createModuleTable();
    if (typeIndex < 0 || typeIndex >= numModuleTypes)
        typeIndex = 0;
    return table[typeIndex];
}

const char* getCategoryName (ModuleCategory category) noexcept
{
    switch (category)
    {
        case ModuleCategory::Filter: return "Filters";
        case ModuleCategory::Transform: return "Transforms";
        case ModuleCategory::Generator: return "Generators";
        case ModuleCategory::Randomizer: return "Randomizers";
        case ModuleCategory::None: break;
    }
    return "";
}

int getNumberOfUsedParameters (const ModuleDescription& description) noexcept
{
    int numUsed = 0;
    for (int index = 0; index < numParametersPerSlot; ++index)
        if (description.parameters[index].isUsed())
            numUsed = index + 1;
    return numUsed;
}

int getNumberOfParameterPages (const ModuleDescription& description) noexcept
{
    return std::max (1, (getNumberOfUsedParameters (description) + numParametersPerPage - 1) / numParametersPerPage);
}

int getNumberOfSteps (const ParameterDescription& parameter) noexcept
{
    return (int) std::lround (parameter.maximum - parameter.minimum) + 1;
}

int normalisedToInteger (const ParameterDescription& parameter, float normalisedValue) noexcept
{
    const float clamped = std::clamp (normalisedValue, 0.0f, 1.0f);
    return (int) std::lround (parameter.minimum + clamped * (parameter.maximum - parameter.minimum));
}

float normalisedToPlainValue (const ParameterDescription& parameter, float normalisedValue) noexcept
{
    if (parameter.kind == ParameterKind::Continuous)
    {
        const float clamped = std::clamp (normalisedValue, 0.0f, 1.0f);
        return parameter.minimum + clamped * (parameter.maximum - parameter.minimum);
    }
    return (float) normalisedToInteger (parameter, normalisedValue);
}

float plainValueToNormalised (const ParameterDescription& parameter, float plainValue) noexcept
{
    if (parameter.maximum <= parameter.minimum)
        return 0.0f;
    return std::clamp ((plainValue - parameter.minimum) / (parameter.maximum - parameter.minimum), 0.0f, 1.0f);
}

float getNormalisedDefault (const ParameterDescription& parameter) noexcept
{
    return parameter.isUsed() ? plainValueToNormalised (parameter, parameter.defaultValue) : 0.0f;
}

std::string getNoteName (int noteNumber)
{
    noteNumber = std::clamp (noteNumber, 0, 127);
    const int octave = noteNumber / 12 - 2; // C3 = 60, as in Bitwig, Cubase and JUCE
    return std::string (pitchClassLabels[noteNumber % 12]) + std::to_string (octave);
}

std::string formatParameterValue (const ParameterDescription& parameter, float normalisedValue)
{
    switch (parameter.kind)
    {
        case ParameterKind::Unused: return "-";

        case ParameterKind::Choice:
        {
            if (parameter.choiceLabels == nullptr)
                return "";
            const int index =
                std::clamp (normalisedToInteger (parameter, normalisedValue), 0, getNumberOfSteps (parameter) - 1);
            return parameter.choiceLabels[index];
        }

        case ParameterKind::NoteNumber: return getNoteName (normalisedToInteger (parameter, normalisedValue));

        case ParameterKind::Percent: return std::to_string (normalisedToInteger (parameter, normalisedValue)) + "%";

        case ParameterKind::Integer:
        {
            const int value = normalisedToInteger (parameter, normalisedValue);
            if (parameter.labelForMinimum != nullptr && value == (int) parameter.minimum)
                return parameter.labelForMinimum;
            const bool showPlusSign = parameter.minimum < 0 && value > 0; // e.g. "+7 st" for bipolar ranges
            return (showPlusSign ? "+" : "") + std::to_string (value) + parameter.unitSuffix;
        }

        case ParameterKind::Continuous:
        {
            char text[32];
            std::snprintf (text, sizeof (text), "%.1f%s", (double) normalisedToPlainValue (parameter, normalisedValue),
                           parameter.unitSuffix);
            return text;
        }
    }
    return "";
}

float parseParameterText (const ParameterDescription& parameter, const std::string& text, float valueIfUnparseable)
{
    if (! parameter.isUsed())
        return valueIfUnparseable;

    if (parameter.kind == ParameterKind::Choice && parameter.choiceLabels != nullptr)
        for (int index = 0; index < getNumberOfSteps (parameter); ++index)
            if (text == parameter.choiceLabels[index])
                return plainValueToNormalised (parameter, (float) index);

    if (parameter.labelForMinimum != nullptr && text == parameter.labelForMinimum)
        return 0.0f;

    if (parameter.kind == ParameterKind::NoteNumber)
        for (int noteNumber = 0; noteNumber < 128; ++noteNumber)
            if (text == getNoteName (noteNumber))
                return plainValueToNormalised (parameter, (float) noteNumber);

    // Fall back to reading a number, ignoring any unit that follows it ("7 st", "50%").
    char* endOfNumber = nullptr;
    const double number = std::strtod (text.c_str(), &endOfNumber);
    if (endOfNumber == text.c_str() || ! std::isfinite (number))
        return valueIfUnparseable;
    return plainValueToNormalised (parameter, (float) number);
}

} // namespace midiforge
