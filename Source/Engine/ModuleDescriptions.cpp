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
const char* const slideModeLabels[] = { "Slide In", "Slide Out" };
const char* const slideDivisionLabels[] = { "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8",
                                            "1/4T", "1/8D",  "1/4",  "1/2T",  "1/4D",  "1/2",  "1/2D", "1 Bar" };
const char* const slideDelayLabels[] = { "Off", "1/64", "1/32T", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D",
                                         "1/8", "1/4T", "1/8D",  "1/4",  "1/2T",  "1/4D",  "1/2",  "1/2D", "1 Bar" };
const char* const dispatchAlgorithmLabels[] = { "Round Robin", "Random", "Ping Pong" };
const char* const dispatchModeLabels[] = { "Single Notes", "Chords" };
const char* const mutateDirectionLabels[] = { "Up", "Down", "Both" };
const char* const mutateMemoryLabels[] = { "4 Bars", "2 Bars", "1 Bar", "Per Pitch" };
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

ParameterDescription triggerParameter (const char* name)
{
    return { name, ParameterKind::Trigger, 0, 1, 0, nullptr, "", nullptr };
}

template <size_t NumLabels>
ParameterDescription choiceParameter (const char* name, const char* const (&labels)[NumLabels], int defaultIndex)
{
    return { name, ParameterKind::Choice, 0, (float) (NumLabels - 1), (float) defaultIndex, labels, "", nullptr };
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
            percentParameter ("Gate", 5, 100, 50).withHelp ("Note length as a share of the step.") },
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
            integerParameter ("Pitch", -12, 12, 0, " st").withHelp ("Transposition added with every echo.") },
          "A MIDI delay: the note plays immediately, then each echo follows one Time later, quieter and "
          "optionally transposed. Every echo is as long as the original note; echoes stop when they get too "
          "quiet." },

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
                .addedInLayout (2) },
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
          "Slides notes in from, or out to, another pitch (pitch bend).",
          { integerParameter ("Offset", -24, 24, -2, " st")
                .withHelp ("Where the slide starts (Slide In) or ends (Slide Out), relative to the note."),
            choiceParameter ("Mode", slideModeLabels, 0)
                .withHelp ("Slide In: from Offset to the note. Slide Out: from the note to Offset."),
            choiceParameter ("Length", slideDivisionLabels, 5)
                .withHelp ("How long the slide takes, synced to the tempo."),
            percentParameter ("Chance", 0, 100, 100).withHelp ("Probability that a note (or chord) slides."),
            // Page 2
            integerParameter ("Curve", -100, 100, 0)
                .withHelp ("0: even slide. Negative: fast start, slow end. Positive: slow start, fast end."),
            integerParameter ("Bend Range", 1, 48, 2, " st")
                .withHelp ("Set this to your synth's pitch bend range, so the slide hits the right pitch."),
            choiceParameter ("Delay", slideDelayLabels, 0)
                .withHelp ("When the slide starts after the note begins (Off = right away).") },
          "Slides the pitch with pitch bend messages. Slide In starts the note at Offset and glides to its own "
          "pitch; Slide Out starts at the note's pitch and glides to Offset. Pitch bend moves the whole channel, "
          "so a chord slides together (notes within 30 ms count as one chord). Set Bend Range to your synth's "
          "pitch bend range. Your own pitch bend is added to the slide." },

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

const NoteDivision slideDivisions[] = {
    { "1/64", 0.0625 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 0.125 },     { "1/16T", 1.0 / 6.0 }, { "1/32D", 0.1875 },
    { "1/16", 0.25 },   { "1/8T", 1.0 / 3.0 },   { "1/16D", 0.375 },    { "1/8", 0.5 },         { "1/4T", 2.0 / 3.0 },
    { "1/8D", 0.75 },   { "1/4", 1.0 },          { "1/2T", 4.0 / 3.0 }, { "1/4D", 1.5 },        { "1/2", 2.0 },
    { "1/2D", 3.0 },    { "1 Bar", 4.0 },
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

static_assert (std::size (echoDivisionLabels) == std::size (echoDivisions), "echo labels and table differ");
static_assert (std::size (arpeggiatorDivisionLabels) == std::size (arpeggiatorDivisions), "arp labels differ");
static_assert (std::size (repeaterDivisionLabels) == std::size (repeaterDivisions), "repeater labels differ");
static_assert (std::size (slideDivisionLabels) == std::size (slideDivisions), "slide labels differ");
static_assert (std::size (slideDelayLabels) == std::size (slideDivisions) + 1, "slide delay labels differ");
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
