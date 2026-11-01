/**
 * @file ModuleDescriptions.cpp
 * @brief The table of all modules and their parameters, plus value conversion and formatting.
 *
 * When you change anything in this file, update docs/MANUAL.md: the manual lists every parameter.
 */
#include "ModuleDescriptions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <sstream>

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
const char* const slideModeLabels[] = { "Slide In", "Slide Out", "Fall Off", "In + Fall Off" };
const char* const slideDivisionLabels[] = { "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8",
                                            "1/4T", "1/8D",  "1/4",  "1/2T",  "1/4D",  "1/2",  "1/2D", "1 Bar" };
const char* const slideDelayLabels[] = { "Off", "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D",
                                         "1/8", "1/4T", "1/8D",  "1/4",  "1/2T",  "1/4D",  "1/2",  "1/2D", "1 Bar" };
const char* const dispatchAlgorithmLabels[] = { "Round Robin", "Random", "Ping Pong" };
const char* const dispatchModeLabels[] = { "Single Notes", "Chords" };
const char* const mutateDirectionLabels[] = { "Up", "Down", "Both" };
const char* const mutateMemoryLabels[] = { "4 Bars", "2 Bars", "1 Bar", "Per Pitch" };
const char* const grooveTemplateLabels[] = { "Swing", "Laid-back Snare", "Pushed Off-beats", "Shuffle", "Drunk" };
const char* const grooveGridLabels[] = { "1/8", "1/16", "1/32" };
const char* const strumDirectionLabels[] = { "Up", "Down", "Alternate", "Random" };
const char* const ghostPositionLabels[] = { "After", "Before", "Both" };
const char* const ghostDistanceLabels[] = { "1/32", "1/16T", "1/16", "1/8" };
const char* const stepGridLabels[] = { "Steady", "Bar" };
const char* const meterGroupingLabels[] = { "3 Last (2+2+3)", "3 First (3+2+2)", "3 Middle (2+3+2)" };
const char* const harmonyIntervalLabels[] = { "Oct Down", "7th Down", "6th Down", "5th Down", "4th Down",
                                              "3rd Down", "2nd Down", "Off",      "2nd Up",   "3rd Up",
                                              "4th Up",   "5th Up",   "6th Up",   "7th Up",   "Oct Up" };
const char* const echoDivisionLabels[] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8",
                                           "1/8D", "1/4T",  "1/4",  "1/4D",  "1/2",  "1 Bar" };
const char* const preDelaySyncLabels[] = { "Tempo", "ms" };
const char* const bounceHeightLabels[] = { "5 cm",  "6 cm",  "8 cm",  "10 cm", "12 cm", "15 cm", "20 cm",
                                           "25 cm", "30 cm", "40 cm", "50 cm", "60 cm", "80 cm", "1 m",
                                           "1.2 m", "1.5 m", "2 m",   "2.5 m", "3 m",   "4 m",   "5 m" };
const char* const bounceMaterialLabels[] = { "Super Ball", "Rubber", "Basketball", "Tennis Ball",
                                             "Wood",       "Glass",  "Metal",      "Clay" };
const char* const bounceSyncLabels[] = { "Height", "Tempo" };
const char* const bounceGravityLabels[] = { "Earth", "Moon", "Mars", "Jupiter", "Pluto", "Sun" };
const char* const ccOutControllerLabels[] = { "0 Bank Select",
                                              "1 Mod Wheel",
                                              "2 Breath",
                                              "3",
                                              "4 Foot",
                                              "5 Porta Time",
                                              "6",
                                              "7 Volume",
                                              "8 Balance",
                                              "9",
                                              "10 Pan",
                                              "11 Expression",
                                              "12",
                                              "13",
                                              "14",
                                              "15",
                                              "16",
                                              "17",
                                              "18",
                                              "19",
                                              "20",
                                              "21",
                                              "22",
                                              "23",
                                              "24",
                                              "25",
                                              "26",
                                              "27",
                                              "28",
                                              "29",
                                              "30",
                                              "31",
                                              "32",
                                              "33",
                                              "34",
                                              "35",
                                              "36",
                                              "37",
                                              "38",
                                              "39",
                                              "40",
                                              "41",
                                              "42",
                                              "43",
                                              "44",
                                              "45",
                                              "46",
                                              "47",
                                              "48",
                                              "49",
                                              "50",
                                              "51",
                                              "52",
                                              "53",
                                              "54",
                                              "55",
                                              "56",
                                              "57",
                                              "58",
                                              "59",
                                              "60",
                                              "61",
                                              "62",
                                              "63",
                                              "64 Sustain",
                                              "65 Portamento",
                                              "66 Sostenuto",
                                              "67 Soft Pedal",
                                              "68",
                                              "69",
                                              "70",
                                              "71 Resonance",
                                              "72 Release",
                                              "73 Attack",
                                              "74 Cutoff",
                                              "75 Decay",
                                              "76 Vib Rate",
                                              "77 Vib Depth",
                                              "78 Vib Delay",
                                              "79",
                                              "80",
                                              "81",
                                              "82",
                                              "83",
                                              "84",
                                              "85",
                                              "86",
                                              "87",
                                              "88",
                                              "89",
                                              "90",
                                              "91 Reverb",
                                              "92",
                                              "93 Chorus",
                                              "94 Detune",
                                              "95",
                                              "96",
                                              "97",
                                              "98",
                                              "99",
                                              "100",
                                              "101",
                                              "102",
                                              "103",
                                              "104",
                                              "105",
                                              "106",
                                              "107",
                                              "108",
                                              "109",
                                              "110",
                                              "111",
                                              "112",
                                              "113",
                                              "114",
                                              "115",
                                              "116",
                                              "117",
                                              "118",
                                              "119",
                                              "120",
                                              "121",
                                              "122",
                                              "123",
                                              "124",
                                              "125",
                                              "126",
                                              "127",
                                              "Pitch Bend",
                                              "Ch. Pressure" };
const char* const noteLengthModeLabels[] = { "Note Value", "Milliseconds", "Stretch" };
const char* const noteLengthValueLabels[] = { "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16",
                                              "1/8T", "1/16D", "1/8",  "1/4T",  "1/8D",  "1/4",
                                              "1/2T", "1/4D",  "1/2",  "1/2D",  "1 Bar", "Legato" };
const char* const noteLengthMaximumLabels[] = { "Off", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars" };
const char* const ccOutSendLabels[] = { "Always", "While Playing", "While Notes Held" };
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

ParameterDescription triggerParameter (const char* name)
{
    return { name, ParameterKind::Trigger, 0, 1, 0, nullptr, "", nullptr };
}

template <size_t NumLabels>
ParameterDescription choiceParameter (const char* name, const char* const (&labels)[NumLabels], int defaultIndex)
{
    return { name, ParameterKind::Choice, 0, (float) (NumLabels - 1), (float) defaultIndex, labels, "", nullptr };
}

/** Help text of Note Echo's Pre-Delay; the same for both of its units. */
constexpr const char* preDelayHelp =
    "A pause before the first echo; the note itself plays right away. A note value or ms, set by Pre-Delay Sync.";

/** Note Echo's Pre-Delay with Pre-Delay Sync on *Tempo* (the module table's entry): Off, 1/64 ... 1 Bar. */
ParameterDescription preDelayInNoteValues()
{
    return choiceParameter ("Pre-Delay", slideDelayLabels, 0).withHelp (preDelayHelp).addedInLayout (4);
}

/** Note Echo's Pre-Delay with Pre-Delay Sync on *ms*: Off, 1 ... 1000 ms. */
ParameterDescription preDelayInMilliseconds()
{
    return integerParameter ("Pre-Delay", 0, 1000, 0, " ms", "Off").withHelp (preDelayHelp).addedInLayout (4);
}

/** Help text of Note Length's Length knob; the same for all three of its units. */
constexpr const char* noteLengthHelp = "How long every note lasts: a note value (or Legato: until the next note), a "
                                       "time in ms, or a share of how long the key was held (Stretch).";

/** Note Length's Length in Note Value mode (the module table's entry): 1/64 ... 1 Bar, Legato. */
ParameterDescription noteLengthInNoteValues()
{
    return choiceParameter ("Length", noteLengthValueLabels, 8).withHelp (noteLengthHelp);
}

/** Note Length's Length in Milliseconds mode: 5 ... 2000 ms. */
ParameterDescription noteLengthInMilliseconds()
{
    return integerParameter ("Length", 5, 2000, 250, " ms").withHelp (noteLengthHelp);
}

/** Note Length's Length in Stretch mode: 100 ... 400 % of the time the key was held. */
ParameterDescription noteLengthAsStretch()
{
    return percentParameter ("Length", 100, 400, 200).withHelp (noteLengthHelp);
}

/** The module table, indexed by ModuleType. Keep the help texts in step with docs/MANUAL.md. */
const ModuleDescription* createModuleTable()
{
    static const ModuleDescription table[numModuleTypes] = {
        { "Empty",
          ModuleCategory::None,
          "Empty slot: events pass through unchanged.",
          { unusedParameter(), unusedParameter(), unusedParameter(), unusedParameter() },
          "Clears the slot. Every event passes through it unchanged." },

        // Filters ------------------------------------------------------------------------------------
        { "Note Range",
          ModuleCategory::Filter,
          "Passes or blocks notes inside a key range.",
          { noteParameter ("Low", 0).withHelp ("Lowest key of the range."),
            noteParameter ("High", 127).withHelp ("Highest key of the range. Low and High may be swapped."),
            choiceParameter ("Mode", passOrBlockInsideLabels, 0)
                .withHelp ("Keep only the notes inside the range, or remove them."),
            unusedParameter() },
          "Lets only notes in a key range through, or removes exactly those notes. Everything that is not a note "
          "passes unchanged." },
        { "Velocity Range",
          ModuleCategory::Filter,
          "Passes or blocks notes inside a velocity range.",
          { integerParameter ("Min", 1, 127, 1).withHelp ("Lowest velocity of the range."),
            integerParameter ("Max", 1, 127, 127).withHelp ("Highest velocity of the range."),
            choiceParameter ("Mode", passOrBlockInsideLabels, 0)
                .withHelp ("Keep only the notes inside the range, or remove them."),
            unusedParameter() },
          "Filters notes by how hard they were played: keeps only the notes inside a velocity range, or removes "
          "them. Everything that is not a note passes unchanged." },
        { "Channel Filter",
          ModuleCategory::Filter,
          "Keeps only one MIDI channel, or removes it.",
          { integerParameter ("Channel", 1, 16, 1).withHelp ("The MIDI channel."),
            choiceParameter ("Mode", onlyOrExceptLabels, 0)
                .withHelp ("Keep only this channel, or remove it and keep all others."),
            unusedParameter(), unusedParameter() },
          "Keeps the messages of one MIDI channel, or removes them. Applies to every channel message: notes, "
          "controllers, pitch bend, aftertouch and program changes." },
        { "Event Blocker",
          ModuleCategory::Filter,
          "Blocks whole classes of MIDI events.",
          { choiceParameter ("Notes", passOrBlockLabels, 0).withHelp ("Notes and their polyphonic aftertouch."),
            choiceParameter ("CC", passOrBlockLabels, 0).withHelp ("Control changes (mod wheel, sustain pedal ...)."),
            choiceParameter ("Pitch Bend", passOrBlockLabels, 0).withHelp ("Pitch bend."),
            choiceParameter ("AT / Prog", passOrBlockLabels, 0)
                .withHelp ("Channel aftertouch, polyphonic aftertouch and program changes.") },
          "Removes whole kinds of MIDI events, for example to keep a synth from receiving the sustain pedal or "
          "pitch bend." },

        // Transforms ---------------------------------------------------------------------------------
        { "Transpose",
          ModuleCategory::Transform,
          "Shifts notes by semitones and octaves.",
          { integerParameter ("Semitones", -24, 24, 0, " st").withHelp ("Shift in semitones."),
            integerParameter ("Octave", -4, 4, 0, " oct").withHelp ("Additional shift in octaves."), unusedParameter(),
            unusedParameter() },
          "Moves every note up or down. Notes pushed outside the MIDI range are dropped. A note always ends at "
          "the pitch it started on, even if you turn the knobs while holding it." },
        { "Scale Quantize",
          ModuleCategory::Transform,
          "Forces notes into a key and scale.",
          { choiceParameter ("Root", pitchClassLabels, 0).withHelp ("Key of the scale."),
            choiceParameter ("Scale", scaleLabels, 0).withHelp ("The scale."),
            choiceParameter ("Snap", snapDirectionLabels, 0)
                .withHelp ("Where wrong notes go. Nearest picks the lower note when both are equally close."),
            unusedParameter() },
          "Moves every note that is not in the scale to a note that is. If two keys land on the same scale note, "
          "it keeps sounding until both keys are released." },
        { "Velocity Curve",
          ModuleCategory::Transform,
          "Reshapes, compresses or fixes note velocities.",
          { integerParameter ("Curve", -100, 100, 0)
                .withHelp ("Negative: soft playing gets louder. Positive: you have to play harder. 0: linear."),
            integerParameter ("Out Min", 1, 127, 1).withHelp ("Velocity of the softest possible note."),
            integerParameter ("Out Max", 1, 127, 127).withHelp ("Velocity of the hardest possible note."),
            integerParameter ("Fixed", 0, 127, 0, "", "Off")
                .withHelp ("If not Off, every note gets exactly this velocity.") },
          "Changes how hard notes are played: bends the velocity response, squeezes it into a range, or gives "
          "every note the same velocity." },
        { "Channel Remap",
          ModuleCategory::Transform,
          "Moves events from one channel to another.",
          { integerParameter ("From", 0, 16, 0, "", "All").withHelp ("Which channel to move (All = every channel)."),
            integerParameter ("To", 1, 16, 1).withHelp ("Target channel."), unusedParameter(), unusedParameter() },
          "Moves the messages of one channel (or of all channels) to another channel. Applies to every channel "
          "message." },

        // Generators ---------------------------------------------------------------------------------
        { "Arpeggiator",
          ModuleCategory::Generator,
          "Turns held notes into a tempo-synced pattern.",
          { choiceParameter ("Rate", arpeggiatorDivisionLabels, 3).withHelp ("Step length (T = triplet)."),
            choiceParameter ("Pattern", arpeggiatorPatternLabels, 0).withHelp ("Order in which the notes play."),
            integerParameter ("Octaves", 1, 4, 1).withHelp ("How many octaves the pattern spans."),
            percentParameter ("Gate", 5, 100, 50).withHelp ("Note length as a share of the step."),
            // Page 2 (added in 0.19.1)
            choiceParameter ("Grid", stepGridLabels, 0)
                .withHelp ("Steady: steps run on from the song start. Bar: they start again at every bar line.")
                .addedInLayout (3) },
          "Plays the keys you hold (a chord, say) one after another. While the DAW plays, the steps lock to its bars; "
          "when it is "
          "stopped, the arpeggiator runs at the project tempo from your first key. Only the arpeggio is passed "
          "on, not the held keys." },
        { "Chord",
          ModuleCategory::Generator,
          "Builds a chord on every incoming note.",
          { choiceParameter ("Type", chordLabels, 0).withHelp ("The chord, built upwards from the played note."),
            integerParameter ("Inversion", 0, 3, 0).withHelp ("Each step moves the lowest chord note up an octave."),
            choiceParameter ("Voicing", chordVoicingLabels, 0)
                .withHelp ("Open raises the second-lowest note an octave; Bass -1 Oct adds a lower bass note."),
            percentParameter ("Added Vel", 0, 100, 100)
                .withHelp ("Velocity of the added notes relative to the played note.") },
          "Plays a whole chord for every key you press. Put an Arpeggiator after it to arpeggiate the chords." },
        { "Note Echo",
          ModuleCategory::Generator,
          "Tempo-synced MIDI delay with pitch shift.",
          { choiceParameter ("Time", echoDivisionLabels, 5).withHelp ("Distance between echoes, synced to the tempo."),
            integerParameter ("Repeats", 1, 16, 3).withHelp ("Maximum number of echoes."),
            percentParameter ("Feedback", 10, 100, 70)
                .withHelp ("Each echo's velocity as a share of the previous one."),
            integerParameter ("Pitch", -12, 12, 0, " st").withHelp ("Transposition added with every echo."),
            // Page 2 (added in 0.23.0)
            preDelayInNoteValues(),
            choiceParameter ("Pre-Delay Sync", preDelaySyncLabels, 0)
                .withHelp ("Pre-Delay as a note value (Tempo, a bar follows the time signature) or in ms.")
                .addedInLayout (4) },
          "A MIDI delay: the note plays immediately, then each echo follows one Time later, quieter and "
          "optionally transposed. Pre-Delay adds a pause before the first echo, so the whole series starts "
          "later. Every echo is as long as the original note; echoes stop when they get too quiet." },

        // Randomizers --------------------------------------------------------------------------------
        { "Humanize",
          ModuleCategory::Randomizer,
          "Adds random timing delay and velocity variation.",
          { continuousParameter ("Timing", 0, 50, 10, " ms")
                .withHelp ("Maximum delay. Notes are only ever delayed, never moved earlier."),
            integerParameter ("Velocity", 0, 64, 10).withHelp ("Maximum velocity change up or down."),
            unusedParameter(), unusedParameter() },
          "Makes playing less mechanical: each note is delayed by a random amount and its velocity varied. The "
          "note-off is delayed by the same amount, so note lengths stay as played." },
        { "Probability",
          ModuleCategory::Randomizer,
          "Lets each note through with a given chance.",
          { percentParameter ("Chance", 0, 100, 75).withHelp ("Probability that a note plays."), unusedParameter(),
            unusedParameter(), unusedParameter() },
          "Each note plays only with the given chance; the others are skipped. Good for thinning out patterns." },
        { "Random Pitch",
          ModuleCategory::Randomizer,
          "Randomly shifts some of the notes.",
          { integerParameter ("Range", 0, 24, 12, " st").withHelp ("Largest possible shift."),
            percentParameter ("Chance", 0, 100, 50).withHelp ("Share of notes that get shifted."),
            choiceParameter ("Mode", randomPitchModeLabels, 1).withHelp ("Which intervals are used."),
            unusedParameter() },
          "Randomly transposes some of the notes up or down. Put a Scale Quantize after it to stay in key." },

        // Added in 0.4.0 ---------------------------------------------------------------------------
        { "Repeater",
          ModuleCategory::Generator,
          "Retriggers held notes on the tempo grid (ratchets).",
          { choiceParameter ("Rate", repeaterDivisionLabels, 7)
                .withHelp ("Distance between repeats (D = dotted, T = triplet); turning right is faster."),
            choiceParameter ("Velocity", repeaterVelocityLabels, 0)
                .withHelp ("Repeats use the velocity of the last key pressed, or follow aftertouch live."),
            percentParameter ("Swing", 50, 75, 50)
                .withHelp ("Delays every second repeat. 50% is straight, 66% a triplet feel."),
            choiceParameter ("Accent", repeaterAccentLabels, 0)
                .withHelp ("Which repeats are made louder by Accent Boost."),
            // Page 2 (added in 0.7.0)
            integerParameter ("Accent Boost", 0, 64, 25)
                .withHelp ("How many velocity steps louder accented repeats are.")
                .addedInLayout (1),
            percentParameter ("Length", 10, 100, 50)
                .withHelp ("How long each repeat lasts, as a share of a step.")
                .addedInLayout (1),
            // Added in 0.9.0
            percentParameter ("Chance", 0, 100, 100)
                .withHelp ("Probability that a repeat plays; the key you press always sounds.")
                .addedInLayout (2),
            // Added in 0.19.1
            choiceParameter ("Grid", stepGridLabels, 0)
                .withHelp ("Steady: repeats run on from the song start. Bar: they (and swing and accents) start "
                           "again at every bar line.")
                .addedInLayout (3) },
          "Ratchets: every held note is played again and again on the tempo grid, a held chord repeats as a "
          "chord. The key you press sounds immediately, the repeats follow on the grid. Releasing a key stops "
          "its repeats. Below 100 % Chance, some repeats rest, so the ratchet gets gaps." },

        // Added in 0.5.0 ---------------------------------------------------------------------------
        { "Note Split",
          ModuleCategory::Filter,
          "Selected notes continue down the chain; all others go straight out.",
          { noteParameter ("Low", 60).withHelp ("Lowest selected note."),
            noteParameter ("High", 60).withHelp ("Highest selected note. Same as Low selects exactly one note."),
            choiceParameter ("Octaves", noteSplitOctaveLabels, 0)
                .withHelp ("Every octave: the selected notes count in every octave (e.g. every C)."),
            unusedParameter() },
          "Splits the chain: notes in the selected range continue to the slots below; all other notes skip the "
          "rest of the chain and go straight to the DAW. Everything that is not a note continues down the "
          "chain. Example: select the hi-hat and put a Repeater below to ratchet only the hi-hat." },

        // Added in 0.10.0 --------------------------------------------------------------------------
        { "Pitch Slide",
          ModuleCategory::Randomizer,
          "Slides notes in from, out to, or falls off to another pitch (pitch bend).",
          { integerParameter ("Offset", -24, 24, -2, " st")
                .withHelp ("Where the slide starts (Slide In) or ends (Slide Out), relative to the note."),
            choiceParameter ("Mode", slideModeLabels, 0)
                .withHelp ("Slide In: from Offset to the note. Slide Out: from the note to Offset. Fall Off: to "
                           "Offset after you release the key. In + Fall Off: both."),
            choiceParameter ("Length", slideDivisionLabels, 5)
                .withHelp ("How long the slide takes, synced to the tempo."),
            percentParameter ("Chance", 0, 100, 100).withHelp ("Probability that a note (or chord) slides."),
            // Page 2
            integerParameter ("Curve", -100, 100, 0)
                .withHelp ("0: even slide. Negative: fast start, slow end. Positive: slow start, fast end."),
            integerParameter ("Bend Range", 1, 48, 2, " st")
                .withHelp ("Set this to your synth's pitch bend range, so the slide hits the right pitch."),
            choiceParameter ("Delay", slideDelayLabels, 0)
                .withHelp ("When the slide starts after the note begins (Off = right away). Slide In and Slide Out "
                           "only.") },
          "Slides the pitch with pitch bend messages. Slide In starts the note at Offset and glides to its own "
          "pitch; Slide Out starts at the note's pitch and glides to Offset. Fall Off keeps the note sounding "
          "after you release the key and lets it fall to Offset over Length; In + Fall Off also slides it in. "
          "Pitch bend moves the whole channel, so a chord slides together (notes within 30 ms count as one chord) "
          "and falls when its last key is released. Set Bend Range to your synth's pitch bend range. Your own "
          "pitch bend is added to the slide." },

        // Added in 0.12.0 --------------------------------------------------------------------------
        { "Note Dispatcher",
          ModuleCategory::Transform,
          "Spreads notes over several channels, one free channel at a time.",
          { choiceParameter ("Algorithm", dispatchAlgorithmLabels, 0)
                .withHelp ("How the next free channel is chosen: in turn, at random, or up and down."),
            choiceParameter ("Mode", dispatchModeLabels, 0)
                .withHelp ("Single Notes: each note gets a channel. Chords: a whole chord shares one channel."),
            integerParameter ("First Channel", 1, 16, 1).withHelp ("The first channel notes are sent to."),
            integerParameter ("Channels", 2, 16, 4).withHelp ("How many channels are used (up to channel 16)."),
            // Page 2
            integerParameter ("Input", 0, 16, 0, "", "All")
                .withHelp ("Which incoming channel is dispatched; other channels pass unchanged."),
            integerParameter ("Chord Window", 0, 100, 30, " ms")
                .withHelp ("Notes starting this close together count as one chord (Chords mode).") },
          "Sends every note (or chord) to one of several MIDI channels, for example to play several mono "
          "synths as one polyphonic instrument. A channel is free when none of its notes are held. Round "
          "Robin takes the next free channel in turn, Random any free one, Ping Pong goes up and down. When "
          "every channel is busy, the oldest note is ended to make room. Controllers, pitch bend and "
          "aftertouch are copied to every channel." },

        // Added in 0.15.0 --------------------------------------------------------------------------
        { "Mutate",
          ModuleCategory::Randomizer,
          "Lets a melody drift: notes slowly move to neighbouring scale notes.",
          { percentParameter ("Amount", 0, 100, 25).withHelp ("How often a note moves one more step away."),
            integerParameter ("Range", 1, 3, 1).withHelp ("How many scale steps a single change can move."),
            choiceParameter ("Direction", mutateDirectionLabels, 2).withHelp ("Which way the notes move."),
            percentParameter ("Return", 0, 100, 30).withHelp ("How strongly changed notes drift back to the original."),
            // Page 2
            choiceParameter ("Root", pitchClassLabels, 0).withHelp ("Key of the scale the notes move in."),
            choiceParameter ("Scale", scaleLabels, 1).withHelp ("The scale the notes move in."),
            choiceParameter ("Memory", mutateMemoryLabels, 0)
                .withHelp ("Remember changes per position in a 4, 2 or 1 bar loop, or per pitch."),
            triggerParameter ("Reset").withHelp ("Back to the original melody (also when the DAW starts playing).") },
          "Varies a looped melody or bassline slowly, one note at a time, staying in key. Every time a note plays, "
          "it may move one more step to a neighbouring note of the scale, or back towards the original, and the "
          "module remembers it: per position in the loop (the note on beat 2 of bar 3) or per pitch (every C). "
          "Chords (notes within 30 ms) pass untouched." },

        // Added in 0.17.0 --------------------------------------------------------------------------
        { "Groove",
          ModuleCategory::Transform,
          "Pulls notes onto a groove: swing, laid-back snare, shuffle ...",
          { choiceParameter ("Groove", grooveTemplateLabels, 0).withHelp ("The groove the notes are pulled to."),
            percentParameter ("Amount", 0, 100, 100)
                .withHelp ("How firmly notes snap to the groove (0 = untouched, 100 = exactly on it)."),
            choiceParameter ("Grid", grooveGridLabels, 1).withHelp ("The grid the groove works on, e.g. 1/16 swing."),
            percentParameter ("Velocity", 0, 100, 50).withHelp ("How strongly the groove's accents change velocities."),
            // Page 2
            integerParameter ("Early", 0, 30, 0, " ms", "Off")
                .withHelp ("Room for notes to come early: everything is delayed this much (reported to the DAW).") },
          "Moves every note towards the groove position of its nearest grid step and applies the groove's accents, "
          "so programmed parts get a pocket. Works while the DAW plays (it needs the bar position). A plug-in "
          "cannot play notes before they arrive: for grooves that push notes early, set Early; all notes are then "
          "delayed that much, the early ones less, and the DAW is told to compensate." },

        // Added in 0.18.0 --------------------------------------------------------------------------
        { "Strum",
          ModuleCategory::Generator,
          "Plays chords as strums: the notes one after another.",
          { integerParameter ("Speed", 1, 100, 15, " ms").withHelp ("Time between the notes of a strum."),
            choiceParameter ("Direction", strumDirectionLabels, 0)
                .withHelp ("Up: lowest note first. Alternate: up, down, up ... Random: a new order every time."),
            integerParameter ("Velocity Tilt", -50, 50, 0)
                .withHelp ("Later notes get louder (positive) or softer (negative) by up to this much."),
            integerParameter ("Window", 0, 40, 10, " ms")
                .withHelp ("How long Strum collects a played chord; everything is delayed this much (reported to "
                           "the DAW). 0: only exactly simultaneous chords.") },
          "Rolls every chord: its notes start one after another, Speed apart, low to high, high to low, alternating "
          "like a strummed guitar (starting low to high again after a pause), or in random order. Notes that "
          "arrive within Window count as one chord. Single notes pass, delayed by Window like everything else." },
        { "Ghost Notes",
          ModuleCategory::Generator,
          "Adds quiet extra hits around notes, mainly for drums.",
          { percentParameter ("Chance", 0, 100, 50).withHelp ("How often a note gets ghost notes."),
            choiceParameter ("Position", ghostPositionLabels, 0)
                .withHelp ("After the note, before it (where a note fell one bar earlier), or both."),
            choiceParameter ("Distance", ghostDistanceLabels, 2).withHelp ("The spacing of the ghost notes."),
            integerParameter ("Count", 1, 3, 1).withHelp ("How many ghost notes (on each side)."),
            // Page 2
            percentParameter ("Velocity", 10, 60, 30).withHelp ("Ghost note velocity, as a share of the note's.") },
          "Adds short, quiet copies of a note one or more Distance apart, e.g. around a snare. A plug-in cannot "
          "play before a note it has not received yet, so Before works from the previous bar: ghost notes come "
          "just before the places where notes fell one bar earlier (needs the DAW playing). Put a Note Split in "
          "front to give only one drum ghost notes." },

        // Added in 0.19.0 --------------------------------------------------------------------------
        { "Beat Thinning",
          ModuleCategory::Randomizer,
          "Drops notes by their place in the bar: off-beats go first.",
          { percentParameter ("Amount", 0, 100, 50)
                .withHelp ("How much is thinned out. The bar start always plays; at 100 % the finest off-beats "
                           "never do."),
            choiceParameter ("Grid", grooveGridLabels, 1).withHelp ("The grid notes are placed on to weigh them."),
            choiceParameter ("Grouping", meterGroupingLabels, 0)
                .withHelp ("For odd meters (5/8, 7/8, 7/4 …): where the group of three goes. 5/8 is 2+3 or 3+2.") },
          "Like Probability, but every place in the bar has its own chance, taken from the DAW's time signature: "
          "the bar start always plays, the start of a beat group (beat 3 in 4/4, the second half of 6/8) rarely "
          "drops, other beats more often and off-beats most. Variation without losing the backbone of the "
          "groove. Odd meters are split into groups of two and three as set by Grouping. Notes within 30 ms "
          "count as a chord and are kept or dropped together. Works while the DAW plays; otherwise every note "
          "passes." },
        { "Diatonic Harmonizer",
          ModuleCategory::Generator, // a Transform in 0.19.0–0.22.0; it adds notes, like Chord
          "Adds one or two harmony notes that stay in key.",
          { choiceParameter ("Voice 1", harmonyIntervalLabels, 9)
                .withHelp ("The first harmony note, counted in notes of the scale (3rd Up = two scale notes up)."),
            choiceParameter ("Voice 2", harmonyIntervalLabels, 7).withHelp ("A second harmony note, or Off."),
            choiceParameter ("Root", pitchClassLabels, 0).withHelp ("Key of the scale."),
            choiceParameter ("Scale", scaleLabels, 0).withHelp ("The scale the harmony notes are taken from."),
            // Page 2
            percentParameter ("Velocity", 10, 100, 80)
                .withHelp ("Velocity of the harmony notes, as a share of the played note's.") },
          "Plays harmony notes along with every single note, taken from the scale, so a third above is major or "
          "minor as the key demands (unlike the fixed intervals of Chord). Intervals count notes of the scale; in "
          "scales with fewer than seven notes they are wider. Octaves are always 12 semitones. Chords (notes "
          "within 30 ms) pass untouched: a plug-in cannot look ahead, so when a note turns out to start a chord, "
          "its harmony notes are ended." },

        // Added in 0.23.0 --------------------------------------------------------------------------
        { "Bounce",
          ModuleCategory::Generator,
          "Repeats notes like a bouncing ball: ever faster and softer.",
          { choiceParameter ("Height", bounceHeightLabels, 13)
                .withHelp ("The height the ball is dropped from: higher means longer gaps (Height Sync Height)."),
            choiceParameter ("Material", bounceMaterialLabels, 1)
                .withHelp ("What the ball is made of: how much of its speed it keeps (Super Ball most, Clay least)."),
            integerParameter ("Bounciness", -20, 20, 0)
                .withHelp (
                    "Fine-tunes the material: adds up to 20 points to the share of speed kept, or removes them."),
            integerParameter ("Bounces", 0, 32, 0, "", "Auto")
                .withHelp ("The most bounces. Auto: until they get too fast (20 ms) or too soft (at most 64)."),
            // Page 2
            choiceParameter ("Height Sync", bounceSyncLabels, 0)
                .withHelp ("Height: real physics, independent of the tempo. Tempo: the first gap is a note value."),
            choiceParameter ("First Gap", echoDivisionLabels, 8)
                .withHelp ("The gap before the first bounce (Height Sync Tempo); a bar follows the time signature."),
            integerParameter ("Pitch per Bounce", -12, 12, 0, " st")
                .withHelp ("Transposition added with every bounce."),
            choiceParameter ("Gravity", bounceGravityLabels, 0)
                .withHelp ("Where the ball falls: weaker gravity bounces slower (Height Sync Height).") },
          "A delay whose repeats follow a ball bouncing on the floor. The note you play is the first impact; "
          "every bounce comes after a shorter gap and softer than the one before, both by the same factor: the "
          "share of its speed the ball keeps, set by Material and Bounciness. With Height Sync Height, the drop "
          "height and the gravity set the gaps (1 m on Earth, Rubber: about 0.72, 0.58, 0.46 s ...). With Tempo, "
          "the first gap is a note value and the bounces then speed up freely, as a ball does. Each bounce is "
          "half as long as the gap to the next one; a chord bounces together. Releasing the key does not stop "
          "the ball." },

        // Added in 0.25.0 --------------------------------------------------------------------------
        { "CC Out",
          ModuleCategory::Generator,
          "Sends a MIDI controller to the synth; modulate Value to move its sound.",
          { choiceParameter ("Controller", ccOutControllerLabels, 74)
                .withHelp ("What is sent: a MIDI controller (74 is often the filter cutoff), pitch bend or channel "
                           "pressure."),
            integerParameter ("Channel", 1, 16, 1).withHelp ("The MIDI channel it is sent on."),
            integerParameter ("Value", 0, 127, 64)
                .withHelp ("The value sent. Point a modulation route here to move the synth's sound."),
            choiceParameter ("Send", ccOutSendLabels, 1)
                .withHelp ("When values are sent: always, only while the DAW plays, or only while keys are held."),
            // Page 2
            integerParameter ("Low", 0, 127, 0).withHelp ("The value sent when Value is 0 (may be above High)."),
            integerParameter ("High", 0, 127, 127).withHelp ("The value sent when Value is 127."),
            integerParameter ("Smoothing", 0, 100, 0, " ms", "Off")
                .withHelp ("Glides to a new value over this time, so jumps (Random, steps) do not click.") },
          "Turns modulation into sound: sends one MIDI controller (or pitch bend, or channel pressure) to the synth. "
          "Point a modulation route at Value, and an LFO, a Random or a step modulator moves the synth's filter, "
          "volume or any other controller. Low and High set the range Value sweeps. A value is sent only when it "
          "changes, at most once per millisecond. Incoming messages of the same controller on the same channel "
          "are blocked, so they never fight; everything else passes. Use one CC Out per controller." },

        // Added in 0.26.0 --------------------------------------------------------------------------
        { "Note Length",
          ModuleCategory::Transform,
          "Sets how long notes last: a note value, ms, stretched, or legato.",
          { choiceParameter ("Mode", noteLengthModeLabels, 0)
                .withHelp ("How Length is measured: a note value (up to Legato), milliseconds, or Stretch."),
            noteLengthInNoteValues(),
            integerParameter ("Overlap", 0, 50, 0, " ms")
                .withHelp ("Legato: the old note ends this long after the new one starts (for mono synths' glide)."),
            choiceParameter ("Max Length", noteLengthMaximumLabels, 4)
                .withHelp ("Legato and Stretch: the longest a note may last, so a phrase's last note does not hang."),
            // Page 2
            integerParameter ("Velocity > Length", -100, 100, 0, "%")
                .withHelp ("Harder notes last longer (positive) or shorter (negative); 0: all the same.") },
          "Decides how long every note lasts, however long the key is held: a note value (a bar follows the time "
          "signature), a time in milliseconds, or Stretch, a share of how long the key was held (100 ... 400 %; a "
          "plug-in cannot know at a note's start when its key will be released, so it can only lengthen). The "
          "last Length step in Note Value mode is Legato: each note lasts until the next note or chord starts "
          "(notes within 30 ms count as a chord). Route a slow LFO to Length to go from staccato to legato over a "
          "phrase. The note-on passes on time; Note Length sends its own note-off." },
    };
    return table;
}
} // namespace

//==============================================================================
// Tables (their order must match the label arrays above)

const NoteDivision echoDivisions[] = {
    { "1/32", 0.125 },     { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
    { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 },         { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 },
    { "1/4", 1.0 },        { "1/4D", 1.5 },        { "1/2", 2.0 },   { "1 Bar", 4.0, 1 },
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

const NoteDivision slideDivisions[] = {
    { "1/64", 0.0625 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 0.125 },     { "1/16T", 1.0 / 6.0 }, { "1/32D", 0.1875 },
    { "1/16", 0.25 },   { "1/8T", 1.0 / 3.0 },   { "1/16D", 0.375 },    { "1/8", 0.5 },         { "1/4T", 2.0 / 3.0 },
    { "1/8D", 0.75 },   { "1/4", 1.0 },          { "1/2T", 4.0 / 3.0 }, { "1/4D", 1.5 },        { "1/2", 2.0 },
    { "1/2D", 3.0 },    { "1 Bar", 4.0, 1 },
};
const int numSlideDivisions = (int) std::size (slideDivisions);

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

const BounceHeight bounceHeights[] = {
    { "5 cm", 0.05 },  { "6 cm", 0.06 },  { "8 cm", 0.08 },  { "10 cm", 0.10 }, { "12 cm", 0.12 }, { "15 cm", 0.15 },
    { "20 cm", 0.20 }, { "25 cm", 0.25 }, { "30 cm", 0.30 }, { "40 cm", 0.40 }, { "50 cm", 0.50 }, { "60 cm", 0.60 },
    { "80 cm", 0.80 }, { "1 m", 1.0 },    { "1.2 m", 1.2 },  { "1.5 m", 1.5 },  { "2 m", 2.0 },    { "2.5 m", 2.5 },
    { "3 m", 3.0 },    { "4 m", 4.0 },    { "5 m", 5.0 },
};
const int numBounceHeights = (int) std::size (bounceHeights);

// Coefficients of restitution, roughly as measured and then tuned by ear (the knob's order is stored, the
// values may change).
const BounceMaterial bounceMaterials[] = {
    { "Super Ball", 0.90 }, { "Rubber", 0.80 }, { "Basketball", 0.75 }, { "Tennis Ball", 0.70 },
    { "Wood", 0.55 },       { "Glass", 0.65 },  { "Metal", 0.60 },      { "Clay", 0.30 },
};
const int numBounceMaterials = (int) std::size (bounceMaterials);

const BounceGravity bounceGravities[] = {
    { "Earth", 9.81 }, { "Moon", 1.62 }, { "Mars", 3.71 }, { "Jupiter", 24.79 }, { "Pluto", 0.62 }, { "Sun", 274.0 },
};
const int numBounceGravities = (int) std::size (bounceGravities);

const NoteDivision noteLengthMaximumDivisions[] = {
    { "1/4", 1.0 }, { "1/2", 2.0 }, { "1 Bar", 4.0, 1 }, { "2 Bars", 8.0, 2 }, { "4 Bars", 16.0, 4 },
};
const int numNoteLengthMaximumDivisions = (int) std::size (noteLengthMaximumDivisions);

static_assert (std::size (echoDivisionLabels) == std::size (echoDivisions), "echo labels and table differ");
static_assert (std::size (arpeggiatorDivisionLabels) == std::size (arpeggiatorDivisions), "arp labels differ");
static_assert (std::size (repeaterDivisionLabels) == std::size (repeaterDivisions), "repeater labels differ");
static_assert (std::size (slideDivisionLabels) == std::size (slideDivisions), "slide labels differ");
static_assert (std::size (slideDelayLabels) == std::size (slideDivisions) + 1, "slide delay labels differ");
static_assert (std::size (scaleLabels) == std::size (scales), "scale labels and table differ");
static_assert (std::size (chordLabels) == std::size (chordShapes), "chord labels and table differ");
static_assert (std::size (bounceHeightLabels) == std::size (bounceHeights), "bounce height labels differ");
static_assert (std::size (bounceMaterialLabels) == std::size (bounceMaterials), "bounce material labels differ");
static_assert (std::size (bounceGravityLabels) == std::size (bounceGravities), "bounce gravity labels differ");
static_assert (std::size (ccOutControllerLabels) == ccOutChannelPressureController + 1, "CC Out labels differ");
static_assert (std::size (noteLengthValueLabels) == std::size (slideDivisions) + 1 &&
                   noteLengthLegatoIndex == (int) std::size (slideDivisions),
               "Note Length's values are the slide lengths plus Legato");
static_assert (std::size (noteLengthMaximumLabels) == std::size (noteLengthMaximumDivisions) + 1,
               "Note Length's Max Length labels differ");

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

namespace
{
/** @return The text in lower case (ASCII), for searches that ignore case. */
std::string toLowerCase (std::string text)
{
    for (char& character : text)
        character = (char) std::tolower ((unsigned char) character);
    return text;
}

/** @return True if every whitespace-separated word of searchText appears in haystack (both lower case). */
bool containsEveryWord (const std::string& lowerCaseHaystack, const std::string& searchText)
{
    std::istringstream words (toLowerCase (searchText));
    std::string word;
    while (words >> word)
        if (lowerCaseHaystack.find (word) == std::string::npos)
            return false;
    return true;
}
} // namespace

bool isParameterInactive (int moduleType, int parameterIndex, const float* normalisedValues) noexcept
{
    const auto& description = getModuleDescription (moduleType);
    if (moduleType == (int) ModuleType::PitchSlide)
    {
        // Pitch Slide: Delay (G) only applies to Slide In and Slide Out, not to the fall modes (Mode = B).
        constexpr int modeIndex = 1, delayIndex = 6, firstFallMode = 2;
        return parameterIndex == delayIndex &&
               normalisedToInteger (description.parameters[modeIndex], normalisedValues[modeIndex]) >= firstFallMode;
    }
    if (moduleType == (int) ModuleType::NoteLength)
    {
        // Note Length: Overlap (C) only in Legato; Max Length (D) in Legato and Stretch; Velocity > Length (E)
        // everywhere but Legato (a legato note has no length of its own).
        constexpr int modeIndex = 0, lengthIndex = 1, overlapIndex = 2, maximumIndex = 3, velocityIndex = 4;
        constexpr int noteValueMode = 0, stretchMode = 2;
        const int mode = normalisedToInteger (description.parameters[modeIndex], normalisedValues[modeIndex]);
        const bool isLegato =
            mode == noteValueMode && normalisedToInteger (description.parameters[lengthIndex],
                                                          normalisedValues[lengthIndex]) == noteLengthLegatoIndex;
        if (parameterIndex == overlapIndex)
            return ! isLegato;
        if (parameterIndex == maximumIndex)
            return ! isLegato && mode != stretchMode;
        return parameterIndex == velocityIndex && isLegato;
    }
    if (moduleType == (int) ModuleType::Bounce)
    {
        // Bounce: Height (A) and Gravity (H) set the timing with Height Sync (E) on Height, First Gap (F) on Tempo.
        constexpr int heightIndex = 0, syncIndex = 4, firstGapIndex = 5, gravityIndex = 7, tempoSync = 1;
        const bool followsTempo =
            normalisedToInteger (description.parameters[syncIndex], normalisedValues[syncIndex]) == tempoSync;
        if (parameterIndex == heightIndex || parameterIndex == gravityIndex)
            return followsTempo;
        return parameterIndex == firstGapIndex && ! followsTempo;
    }
    return false;
}

const ParameterDescription& getEffectiveParameterDescription (int moduleType, int parameterIndex,
                                                              const float* normalisedValues) noexcept
{
    const auto& description = getModuleDescription (moduleType);
    const int safeIndex = std::clamp (parameterIndex, 0, numParametersPerSlot - 1);
    if (moduleType == (int) ModuleType::NoteEcho)
    {
        // Note Echo: Pre-Delay (E) counts milliseconds when Pre-Delay Sync (F) is on "ms".
        constexpr int preDelayIndex = 4, syncIndex = 5, millisecondsSync = 1;
        if (safeIndex == preDelayIndex &&
            normalisedToInteger (description.parameters[syncIndex], normalisedValues[syncIndex]) == millisecondsSync)
        {
            static const ParameterDescription inMilliseconds = preDelayInMilliseconds();
            return inMilliseconds;
        }
    }
    if (moduleType == (int) ModuleType::NoteLength)
    {
        // Note Length: Length (B) counts milliseconds or a stretch share, as Mode (A) says.
        constexpr int modeIndex = 0, lengthIndex = 1, millisecondsMode = 1, stretchMode = 2;
        const int mode = normalisedToInteger (description.parameters[modeIndex], normalisedValues[modeIndex]);
        if (safeIndex == lengthIndex && mode == millisecondsMode)
        {
            static const ParameterDescription inMilliseconds = noteLengthInMilliseconds();
            return inMilliseconds;
        }
        if (safeIndex == lengthIndex && mode == stretchMode)
        {
            static const ParameterDescription asStretch = noteLengthAsStretch();
            return asStretch;
        }
    }
    return description.parameters[safeIndex];
}

SearchRelevance getSearchRelevance (const ModuleDescription& description, const std::string& searchText)
{
    std::string text = toLowerCase (description.name);
    if (containsEveryWord (text, searchText))
        return SearchRelevance::inName;

    text += ' ' + toLowerCase (std::string (getCategoryName (description.category)) + ' ' + description.summary);
    if (containsEveryWord (text, searchText))
        return SearchRelevance::inSummary;

    text += ' ' + toLowerCase (description.details);
    for (const auto& parameter : description.parameters)
        if (parameter.isUsed())
            text += ' ' + toLowerCase (parameter.name);
    return containsEveryWord (text, searchText) ? SearchRelevance::inDetails : SearchRelevance::noMatch;
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

        case ParameterKind::Trigger: return normalisedToInteger (parameter, normalisedValue) != 0 ? "On" : "Off";

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

    if (parameter.kind == ParameterKind::Trigger && (text == "On" || text == "Off"))
        return text == "On" ? 1.0f : 0.0f;

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
