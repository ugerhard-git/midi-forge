/**
 * @file ModuleDescriptions.h
 * @brief Static description of every module type: its name, category, help texts and (up to eight) parameters.
 *
 * Every slot of the chain exposes eight generic host parameters, called A to H, each normalised to 0..1.
 * The editor shows them in pages of four (A-D, E-H); most modules only use A-D.
 * The description of the module loaded in a slot decides what each of them means, which range it maps
 * to and how its value is shown as text. Keeping this in one table means the engine, the host parameter
 * display and the editor can never disagree about it.
 */
#pragma once

#include "MidiEvent.h"

#include <string>

namespace midiforge
{

/**
 * @brief All module types. The numeric values are stored in host projects and presets,
 *        so existing entries must never be reordered or removed; new types go at the end (before Count).
 */
enum class ModuleType : int
{
    Empty = 0,
    // Filters
    NoteRange,
    VelocityRange,
    ChannelFilter,
    EventBlocker,
    // Transforms
    Transpose,
    ScaleQuantize,
    VelocityCurve,
    ChannelRemap,
    // Generators
    Arpeggiator,
    Chord,
    NoteEcho,
    // Randomizers
    Humanize,
    Probability,
    RandomPitch,
    // Added in 0.4.0 (appended: the numbers are stored in projects)
    Repeater, ///< Generator
    // Added in 0.5.0
    NoteSplit, ///< Filter
    // Added in 0.10.0
    PitchSlide, ///< Randomizer
    // Added in 0.12.0
    NoteDispatcher, ///< Transform
    // Added in 0.15.0
    Mutate, ///< Randomizer
    // Added in 0.17.0
    Groove, ///< Transform
    // Added in 0.18.0
    Strum,      ///< Generator
    GhostNotes, ///< Generator
    // Added in 0.19.0
    BeatThinning,       ///< Randomizer
    DiatonicHarmonizer, ///< Generator (a Transform until 0.22.0; the number stays)

    Count
};

constexpr int numModuleTypes = (int) ModuleType::Count;
constexpr int numParametersPerSlot = 8;
/** The editor shows a slot's parameters in pages of this many knobs. */
constexpr int numParametersPerPage = 4;
constexpr int numParameterPages = numParametersPerSlot / numParametersPerPage;

/**
 * Knob layout versions: projects and presets store the version they were saved with, so knobs that a
 * module gained later can be set to their defaults when an older state is loaded (an older state holds
 * 0 for them, which may be a bad value, e.g. a Chance of 0 %).
 * 0 = up to 0.6.0, 1 = 0.7.0 (Repeater page 2: Accent Boost, Length), 2 = 0.9.0 (Repeater: Chance),
 * 3 = 0.19.1 (Arpeggiator and Repeater: Grid).
 */
constexpr int currentKnobLayoutVersion = 3;

/** How a parameter's normalised value is turned into a plain value and displayed. */
enum class ParameterKind
{
    Unused,     ///< The module does not use this parameter.
    Integer,    ///< Whole number between minimum and maximum.
    Continuous, ///< Real number between minimum and maximum.
    Choice,     ///< Index into choiceLabels.
    NoteNumber, ///< MIDI note number shown as a note name (C3 = 60).
    Percent,    ///< Whole number shown with a % sign.
    Trigger     ///< A button (0 = off, 1 = on); the module acts when it turns on (pressed or modulated).
};

/** The groups the module menu and the colour coding of the editor are organised by. */
enum class ModuleCategory
{
    None,
    Filter,
    Transform,
    Generator,
    Randomizer
};

/** Describes one of the (up to eight) parameters of a module. */
struct ParameterDescription
{
    const char* name = "";
    ParameterKind kind = ParameterKind::Unused;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;                 ///< Plain (not normalised) default value.
    const char* const* choiceLabels = nullptr; ///< Labels for ParameterKind::Choice.
    const char* unitSuffix = "";               ///< Appended to the displayed number, e.g. " st".
    const char* labelForMinimum = nullptr;     ///< Optional text shown at the minimum, e.g. "Off".
    /** One line explaining what the knob does, shown in the module info, the module browser and as tooltip. */
    const char* helpText = "";
    /** The knob layout version that added this knob to its module (0 = it was there from the start). */
    int addedInKnobLayout = 0;

    bool isUsed() const noexcept { return kind != ParameterKind::Unused; }

    /** @return A copy of this description with the given help text. Keeps the module table readable. */
    ParameterDescription withHelp (const char* text) const noexcept
    {
        auto copy = *this;
        copy.helpText = text;
        return copy;
    }

    /** @return A copy of this description marked as added in the given knob layout version. */
    ParameterDescription addedInLayout (int knobLayoutVersion) const noexcept
    {
        auto copy = *this;
        copy.addedInKnobLayout = knobLayoutVersion;
        return copy;
    }
};

/** @return How many of a module's parameters are in use (index of the last used one + 1). */
int getNumberOfUsedParameters (const struct ModuleDescription& description) noexcept;
/** @return How many knob pages a module needs (1 or 2). */
int getNumberOfParameterPages (const struct ModuleDescription& description) noexcept;

/** Describes one module type. */
struct ModuleDescription
{
    const char* name;
    ModuleCategory category;
    const char* summary; ///< One short sentence, shown under the module menu.
    ParameterDescription parameters[numParametersPerSlot];
    /** A few sentences explaining how the module works, shown in the module info and the module browser. */
    const char* details;
};

/** @return The description of a module type. Out-of-range values return the Empty module. */
const ModuleDescription& getModuleDescription (ModuleType type) noexcept;
const ModuleDescription& getModuleDescription (int typeIndex) noexcept;

/** @return The display name of a category, e.g. "Filters". */
const char* getCategoryName (ModuleCategory category) noexcept;

/**
 * @return True if a knob has no effect right now because of another knob of the same module (the editor
 *         dims it), e.g. Pitch Slide's Delay in the Fall Off modes.
 * @param normalisedValues The module's numParametersPerSlot values (0..1).
 */
bool isParameterInactive (int moduleType, int parameterIndex, const float* normalisedValues) noexcept;

/** How well a module matches the search text of the module browser; higher is better. */
enum class SearchRelevance
{
    noMatch = 0,
    inDetails = 1, ///< Every word appears somewhere: name, category, summary, details or knob names.
    inSummary = 2, ///< Every word appears in the name, the category or the summary.
    inName = 3     ///< Every word appears in the module's name.
};

/**
 * Rates a module against the search text of the module browser. The text is split into words, and every
 * word must appear (ignoring case) for a match. An empty search text matches every module by name.
 */
SearchRelevance getSearchRelevance (const ModuleDescription& description, const std::string& searchText);

/** @return True if the module is found by the search text at all. @see getSearchRelevance */
inline bool moduleMatchesSearch (const ModuleDescription& description, const std::string& searchText)
{
    return getSearchRelevance (description, searchText) != SearchRelevance::noMatch;
}

/** @return The number of distinct values of an Integer, Choice, NoteNumber or Percent parameter. */
int getNumberOfSteps (const ParameterDescription& parameter) noexcept;

/** Converts a normalised value (0..1) into the parameter's plain value (rounded for whole-number kinds). */
float normalisedToPlainValue (const ParameterDescription& parameter, float normalisedValue) noexcept;
/** Converts a normalised value (0..1) into the nearest whole plain value. */
int normalisedToInteger (const ParameterDescription& parameter, float normalisedValue) noexcept;
/** Converts a plain value into the normalised range 0..1. */
float plainValueToNormalised (const ParameterDescription& parameter, float plainValue) noexcept;
/** @return The normalised default value of a parameter (0 for unused parameters). */
float getNormalisedDefault (const ParameterDescription& parameter) noexcept;

/** Formats a normalised value for display, e.g. "+7 st", "1/16", "C3", "75%". */
std::string formatParameterValue (const ParameterDescription& parameter, float normalisedValue);
/**
 * Parses text typed by the user in a host's parameter field.
 * @return The matching normalised value, or valueIfUnparseable when the text makes no sense.
 */
float parseParameterText (const ParameterDescription& parameter, const std::string& text, float valueIfUnparseable);

/** @return A note name such as "C#3", using the convention C3 = MIDI note 60. */
std::string getNoteName (int noteNumber);

//==============================================================================
// Lookup tables shared by the descriptions and the modules

/**
 * A musical note length expressed in quarter notes ("beats"). Bar-long divisions ("1 Bar", "4 Bars") also
 * give their number of bars, so they follow the host's time signature; use divisionLengthInBeats().
 */
struct NoteDivision
{
    const char* name;
    double lengthInBeats; ///< The length in quarter notes (for bar-long divisions: in 4/4)
    int bars = 0;         ///< For bar-long divisions the number of bars, otherwise 0
};

/** @return A division's length in quarter notes in a time signature (bar-long divisions follow the meter). */
inline double divisionLengthInBeats (const NoteDivision& division, const TimeSignature& timeSignature) noexcept
{
    return division.bars > 0 ? division.bars * timeSignature.barLengthInQuarters() : division.lengthInBeats;
}

/**
 * @return How many lengths of a division lie between two musical positions (quarter notes), e.g. LFO cycles
 *         or steps. Bar-long divisions count the host's bars, so they stay aligned to the bar start in any
 *         time signature.
 */
inline double divisionsBetween (const NoteDivision& division, const BlockContext& context, double fromBeat,
                                double toBeat) noexcept
{
    if (division.bars > 0)
        return (context.barsAtBeat (toBeat) - context.barsAtBeat (fromBeat)) / division.bars;
    return (toBeat - fromBeat) / division.lengthInBeats;
}
extern const NoteDivision echoDivisions[];
extern const int numEchoDivisions;
extern const NoteDivision arpeggiatorDivisions[];
extern const int numArpeggiatorDivisions;
extern const NoteDivision repeaterDivisions[];
extern const int numRepeaterDivisions;
/** Slide lengths of the Pitch Slide, short to long. Its Delay knob uses "Off" followed by the same lengths. */
extern const NoteDivision slideDivisions[];
extern const int numSlideDivisions;

/** A scale as a bit mask: bit n is set if the scale contains the note n semitones above the root. */
struct Scale
{
    const char* name;
    unsigned short semitoneMask;
};
extern const Scale scales[];
extern const int numScales;

/**
 * @return The note `steps` notes of a scale above (positive) or below (negative) a note, e.g. 2 steps = the
 *         third above in a major scale. The note itself for 0 steps or when the result would leave the MIDI
 *         range. The start note does not have to be in the scale. Shared by Mutate and Diatonic Harmonizer.
 * @param root          Pitch class of the scale's root (0 = C).
 * @param semitoneMask  Scale::semitoneMask.
 */
inline int moveByScaleSteps (int noteNumber, int steps, int root, unsigned short semitoneMask) noexcept
{
    if (semitoneMask == 0)
        return noteNumber;
    const int stepDirection = steps > 0 ? 1 : -1;
    int note = noteNumber;
    for (int stepsLeft = steps > 0 ? steps : -steps; stepsLeft > 0;)
    {
        note += stepDirection;
        if (note < 0 || note > 127)
            return noteNumber;
        const int semitoneAboveRoot = ((note - root) % 12 + 12) % 12;
        if ((semitoneMask >> semitoneAboveRoot) & 1u)
            --stepsLeft;
    }
    return note;
}

/** A chord as semitone offsets from the played note. */
struct ChordShape
{
    const char* name;
    int numNotes;
    int semitoneOffsets[5];
};
extern const ChordShape chordShapes[];
extern const int numChordShapes;

} // namespace midiforge
