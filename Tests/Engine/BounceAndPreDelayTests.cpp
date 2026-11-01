/**
 * @file BounceAndPreDelayTests.cpp
 * @brief Note Echo's Pre-Delay (note values or ms, echo lengths, the meter) and the Bounce module (physics of the
 *        gaps and velocities, materials, gravity, tempo sync, bounce limits, chords, no stuck notes).
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>

using namespace testing;

namespace
{
constexpr int samplesPerQuarter = 24000; // 120 bpm at 48 kHz

/** Note Echo knob positions used below. */
namespace Echo
{
constexpr float eighth = 5;
constexpr float preDelayOff = 0, preDelayQuarter = 12, preDelayOneBar = 17; // see slideDelayLabels
constexpr float syncTempo = 0, syncMilliseconds = 1;
constexpr int preDelayIndex = 4;
} // namespace Echo

/** Bounce knob positions used below. */
namespace Ball
{
constexpr float oneMetre = 13, fiveCentimetres = 0;
constexpr float superBall = 0, rubber = 1, clay = 7;
constexpr float autoBounces = 0;
constexpr float syncHeight = 0, syncTempo = 1;
constexpr float firstGapQuarter = 8, firstGapOneBar = 11; // see echoDivisionLabels
constexpr float moon = 1;                                 // see bounceGravityLabels (Earth is the default)
} // namespace Ball

/** A note-on at the chain's output, with its absolute time. */
struct PlayedNote
{
    int time;
    int noteNumber;
    int velocity;
};

std::vector<PlayedNote> noteOnsOf (const ChainTestRig& rig)
{
    std::vector<PlayedNote> notes;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            notes.push_back ({ event.samplePosition, event.noteNumber(), event.velocity() });
    return notes;
}

/** @return The absolute times of all note-offs of one note number. */
std::vector<int> noteOffTimesOf (const ChainTestRig& rig, int noteNumber)
{
    std::vector<int> times;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOff() && event.noteNumber() == noteNumber)
            times.push_back (event.samplePosition);
    return times;
}

/** Sets Note Echo's Pre-Delay in milliseconds (the knob's ms unit applies with Pre-Delay Sync on ms). */
void setPreDelayInMilliseconds (ChainTestRig& rig, float milliseconds)
{
    auto& values = rig.slots[0].parameterValues;
    const auto& inMilliseconds =
        getEffectiveParameterDescription ((int) ModuleType::NoteEcho, Echo::preDelayIndex, values);
    values[Echo::preDelayIndex] = plainValueToNormalised (inMilliseconds, milliseconds);
}

/** @return Bounce's first gap with Height Sync on Height, in samples: 2 · sqrt (2 g h) · e / g. */
double physicalFirstGap (double heightInMetres, double restitution, double gravity)
{
    return 2.0 * std::sqrt (2.0 * gravity * heightInMetres) * restitution / gravity * testSampleRate;
}
} // namespace

//==============================================================================
// Note Echo: Pre-Delay

TEST_CASE ("note echo pre-delay: the whole series starts one Pre-Delay later; echoes keep their length")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteEcho, { Echo::eighth, 2, 50, 0, Echo::preDelayQuarter, Echo::syncTempo });
    rig.processBlock ({ noteOnAt (0, 60, 100) });
    rig.processSeconds (0.1); // 18 more blocks: the key is held for 19 blocks
    rig.processBlock ({ noteOffAt (0, 60) });
    rig.processSeconds (2.0);

    const auto notes = noteOnsOf (rig);
    EXPECT (notes.size() == 3);
    if (notes.size() == 3)
    {
        EXPECT (notes[0].time == 0); // the note itself plays right away
        EXPECT (notes[1].time == samplesPerQuarter + samplesPerQuarter / 2);
        EXPECT (notes[2].time == samplesPerQuarter + 2 * (samplesPerQuarter / 2));
    }
    const auto noteOffs = noteOffTimesOf (rig, 60);
    EXPECT (noteOffs.size() == 3);
    if (notes.size() == 3 && noteOffs.size() == 3)
        for (size_t index = 0; index < 3; ++index)
            EXPECT (noteOffs[index] - notes[index].time == 19 * testBlockSize); // as long as the original
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note echo pre-delay: in ms, Off changes nothing, and 1 Bar follows the time signature")
{
    ChainTestRig milliseconds;
    milliseconds.loadModule (0, ModuleType::NoteEcho, { Echo::eighth, 1, 50, 0, 0, Echo::syncMilliseconds });
    setPreDelayInMilliseconds (milliseconds, 250.0f); // 12000 samples
    milliseconds.processBlock ({ noteOnAt (0, 60) });
    milliseconds.processBlock ({ noteOffAt (10, 60) });
    milliseconds.processSeconds (1.0);
    auto notes = noteOnsOf (milliseconds);
    EXPECT (notes.size() == 2 && notes.back().time == 12000 + samplesPerQuarter / 2);
    EXPECT (milliseconds.notesStillSounding().empty());

    ChainTestRig off;
    off.loadModule (0, ModuleType::NoteEcho, { Echo::eighth, 1, 50, 0, Echo::preDelayOff });
    off.processBlock ({ noteOnAt (0, 60) });
    off.processBlock ({ noteOffAt (10, 60) });
    off.processSeconds (1.0);
    notes = noteOnsOf (off);
    EXPECT (notes.size() == 2 && notes.back().time == samplesPerQuarter / 2);

    ChainTestRig waltz;
    waltz.context.timeSignature = { 3, 4 };
    waltz.loadModule (0, ModuleType::NoteEcho, { Echo::eighth, 1, 50, 0, Echo::preDelayOneBar, Echo::syncTempo });
    waltz.processBlock ({ noteOnAt (0, 60) });
    waltz.processBlock ({ noteOffAt (10, 60) });
    waltz.processSeconds (3.0);
    notes = noteOnsOf (waltz);
    EXPECT (notes.size() == 2 && notes.back().time == 3 * samplesPerQuarter + samplesPerQuarter / 2);
    EXPECT (waltz.notesStillSounding().empty());
}

TEST_CASE ("note echo pre-delay: the knob shows note values or ms, as Pre-Delay Sync says")
{
    const auto& noteEcho = getModuleDescription (ModuleType::NoteEcho);
    float values[numParametersPerSlot] {};
    for (int index = 0; index < numParametersPerSlot; ++index)
        values[index] = getNormalisedDefault (noteEcho.parameters[index]);
    EXPECT (formatParameterValue (noteEcho.parameters[Echo::preDelayIndex], values[Echo::preDelayIndex]) == "Off");
    EXPECT (std::string (noteEcho.parameters[5].name) == "Pre-Delay Sync");
    EXPECT (noteEcho.parameters[4].addedInKnobLayout == 4 && noteEcho.parameters[5].addedInKnobLayout == 4);

    const auto& inNoteValues = getEffectiveParameterDescription ((int) ModuleType::NoteEcho, 4, values);
    EXPECT (&inNoteValues == &noteEcho.parameters[4]);
    values[4] = plainValueToNormalised (inNoteValues, Echo::preDelayQuarter);
    EXPECT (formatParameterValue (inNoteValues, values[4]) == "1/4");

    values[5] = plainValueToNormalised (noteEcho.parameters[5], Echo::syncMilliseconds);
    const auto& inMilliseconds = getEffectiveParameterDescription ((int) ModuleType::NoteEcho, 4, values);
    EXPECT (inMilliseconds.kind == ParameterKind::Integer && inMilliseconds.maximum == 1000.0f);
    EXPECT (formatParameterValue (inMilliseconds, plainValueToNormalised (inMilliseconds, 250.0f)) == "250 ms");
    EXPECT (formatParameterValue (inMilliseconds, 0.0f) == "Off");
    EXPECT (std::abs (parseParameterText (inMilliseconds, "250 ms", -1.0f) -
                      plainValueToNormalised (inMilliseconds, 250.0f)) < 1.0e-6f);

    // Every other knob is the module table's own entry.
    EXPECT (&getEffectiveParameterDescription ((int) ModuleType::NoteEcho, 0, values) == &noteEcho.parameters[0]);
    EXPECT (&getEffectiveParameterDescription ((int) ModuleType::Bounce, 4, values) ==
            &getModuleDescription (ModuleType::Bounce).parameters[4]);
}

//==============================================================================
// Bounce

TEST_CASE ("bounce: 1 m of rubber on Earth - gaps and velocities shrink by the same factor")
{
    EXPECT (getModuleDescription (ModuleType::Bounce).category == ModuleCategory::Generator);

    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::rubber, 0, 4 });
    rig.processBlock ({ noteOnAt (0, 60, 100) });
    rig.processBlock ({ noteOffAt (0, 60) });
    rig.processSeconds (3.0);

    const auto notes = noteOnsOf (rig);
    EXPECT (notes.size() == 5); // the played note and 4 bounces
    if (notes.size() == 5)
    {
        EXPECT (notes[0].time == 0);
        const double firstGap = physicalFirstGap (1.0, 0.8, 9.81); // about 0.72 s
        EXPECT (std::abs (notes[1].time - firstGap) <= 1.0);
        EXPECT (std::abs ((notes[2].time - notes[1].time) - 0.8 * firstGap) <= 2.0);
        EXPECT (std::abs ((notes[4].time - notes[3].time) - 0.8 * 0.8 * 0.8 * firstGap) <= 2.0);
        EXPECT (notes[1].velocity == 80 && notes[2].velocity == 64 && notes[3].velocity == 51);
        for (const auto& note : notes)
            EXPECT (note.noteNumber == 60);
    }
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("bounce: same-pitch bounces never overlap, and releasing the key does not stop the ball")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::superBall, 0, Ball::autoBounces });
    rig.processBlock ({ noteOnAt (0, 60, 127) });
    rig.processBlock ({ noteOffAt (0, 60) }); // released long before the first bounce
    rig.processSeconds (20.0);

    const auto notes = noteOnsOf (rig);
    const auto noteOffs = noteOffTimesOf (rig, 60);
    EXPECT (notes.size() > 20);
    EXPECT (notes.size() == noteOffs.size());
    // Every bounce ends before the next one starts: off number n lies between on n and on n + 1.
    for (size_t index = 1; index + 1 < notes.size() && index < noteOffs.size(); ++index)
        EXPECT (noteOffs[index] > notes[index].time && noteOffs[index] < notes[index + 1].time);
    // Auto stops before a gap shorter than 20 ms (960 samples), and after 64 bounces at the latest.
    EXPECT (notes.size() <= 65);
    if (notes.size() >= 3)
        EXPECT (notes[notes.size() - 1].time - notes[notes.size() - 2].time >= 960);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("bounce: Auto stops soft and fast balls early; Bounces sets the most")
{
    // Clay keeps 30 %: gaps of about 0.27, 0.08 and 0.024 s, then the next (0.007 s) is too short.
    ChainTestRig clay;
    clay.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::clay, 0, Ball::autoBounces });
    clay.processBlock ({ noteOnAt (0, 60, 100) });
    clay.processBlock ({ noteOffAt (0, 60) });
    clay.processSeconds (1.0);
    EXPECT (noteOnsOf (clay).size() == 4);
    EXPECT (clay.notesStillSounding().empty());

    ChainTestRig twoBounces;
    twoBounces.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::superBall, 0, 2 });
    twoBounces.processBlock ({ noteOnAt (0, 60, 100) });
    twoBounces.processBlock ({ noteOffAt (0, 60) });
    twoBounces.processSeconds (5.0);
    EXPECT (noteOnsOf (twoBounces).size() == 3);

    // Bounciness raises the share of speed kept: +10 points on Rubber is 90 %.
    ChainTestRig bouncier;
    bouncier.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::rubber, 10, 1 });
    bouncier.processBlock ({ noteOnAt (0, 60, 100) });
    bouncier.processBlock ({ noteOffAt (0, 60) });
    bouncier.processSeconds (2.0);
    const auto notes = noteOnsOf (bouncier);
    EXPECT (notes.size() == 2 && notes.back().velocity == 90);
    if (notes.size() == 2)
        EXPECT (std::abs (notes.back().time - physicalFirstGap (1.0, 0.9, 9.81)) <= 1.0);
}

TEST_CASE ("bounce: gravity and height set the timing; Tempo sync makes the first gap a note value")
{
    ChainTestRig moon;
    moon.loadModule (0, ModuleType::Bounce,
                     { Ball::oneMetre, Ball::rubber, 0, 1, Ball::syncHeight, Ball::firstGapQuarter, 0, Ball::moon });
    moon.processBlock ({ noteOnAt (0, 60) });
    moon.processBlock ({ noteOffAt (0, 60) });
    moon.processSeconds (3.0);
    auto notes = noteOnsOf (moon);
    EXPECT (notes.size() == 2);
    if (notes.size() == 2)
        EXPECT (std::abs (notes.back().time - physicalFirstGap (1.0, 0.8, 1.62)) <= 1.0); // about 1.78 s

    ChainTestRig quarter;
    quarter.loadModule (
        0, ModuleType::Bounce,
        { Ball::fiveCentimetres, Ball::rubber, 0, 3, Ball::syncTempo, Ball::firstGapQuarter, 0, Ball::moon });
    quarter.processBlock ({ noteOnAt (0, 60) });
    quarter.processBlock ({ noteOffAt (0, 60) });
    quarter.processSeconds (2.0);
    notes = noteOnsOf (quarter);
    EXPECT (notes.size() == 4);
    if (notes.size() == 4)
    {
        EXPECT (notes[1].time == samplesPerQuarter); // only the first gap follows the beat
        EXPECT (std::abs ((notes[2].time - notes[1].time) - 0.8 * samplesPerQuarter) <= 1.0);
    }

    // With Tempo, "1 Bar" is a real bar of the time signature: 3 quarter notes in 3/4.
    ChainTestRig waltz;
    waltz.context.timeSignature = { 3, 4 };
    waltz.loadModule (0, ModuleType::Bounce,
                      { Ball::oneMetre, Ball::rubber, 0, 1, Ball::syncTempo, Ball::firstGapOneBar });
    waltz.processBlock ({ noteOnAt (0, 60) });
    waltz.processBlock ({ noteOffAt (0, 60) });
    waltz.processSeconds (3.0); // the bounce at 1.5 s lasts half the next gap: 0.6 s
    notes = noteOnsOf (waltz);
    EXPECT (notes.size() == 2 && notes.back().time == 3 * samplesPerQuarter);
    EXPECT (waltz.notesStillSounding().empty());
}

TEST_CASE ("bounce: Pitch per Bounce climbs, a chord bounces together, and the knobs dim with Height Sync")
{
    ChainTestRig climbing;
    climbing.loadModule (0, ModuleType::Bounce,
                         { Ball::oneMetre, Ball::superBall, 0, Ball::autoBounces, Ball::syncHeight, 0, 12 });
    climbing.processBlock ({ noteOnAt (0, 100) });
    climbing.processBlock ({ noteOffAt (0, 100) });
    climbing.processSeconds (5.0);
    // 100, 112, 124; the next (136) would leave the MIDI range, so the ball stops there.
    EXPECT (climbing.playedNoteNumbers() == (std::vector<int> { 100, 112, 124 }));
    EXPECT (climbing.notesStillSounding().empty());

    ChainTestRig chord;
    chord.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::rubber, 0, 3 });
    chord.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64), noteOnAt (0, 67) });
    chord.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    chord.processSeconds (3.0);
    const auto notes = noteOnsOf (chord);
    EXPECT (notes.size() == 12);
    for (size_t index = 3; index + 2 < notes.size(); index += 3)
        EXPECT (notes[index].time == notes[index + 1].time && notes[index].time == notes[index + 2].time);
    EXPECT (chord.notesStillSounding().empty());

    // Height and Gravity work with Height Sync on Height, First Gap with Tempo.
    const auto& bounce = getModuleDescription (ModuleType::Bounce);
    float values[numParametersPerSlot] {};
    const auto isDimmed = [&] (int knob) { return isParameterInactive ((int) ModuleType::Bounce, knob, values); };
    values[4] = plainValueToNormalised (bounce.parameters[4], Ball::syncHeight);
    EXPECT (! isDimmed (0) && isDimmed (5) && ! isDimmed (7) && ! isDimmed (1));
    values[4] = plainValueToNormalised (bounce.parameters[4], Ball::syncTempo);
    EXPECT (isDimmed (0) && ! isDimmed (5) && isDimmed (7) && ! isDimmed (6));
}

TEST_CASE ("bounce: no stuck notes with retriggered keys, chains of bounces and a full queue")
{
    // A note-on for a note that is still held (on, on, off) ends the previous one first.
    ChainTestRig retriggered;
    retriggered.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::rubber, 0, 2 });
    retriggered.processBlock ({ noteOnAt (0, 60), noteOnAt (100, 60) });
    retriggered.processBlock ({ noteOffAt (0, 60) });
    retriggered.processSeconds (3.0);
    EXPECT (retriggered.notesStillSounding().empty());

    // A bounce feeding another bounce (it sends overlapping notes of one pitch).
    ChainTestRig twoBalls;
    twoBalls.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::superBall, 0, Ball::autoBounces });
    twoBalls.loadModule (1, ModuleType::Bounce, { Ball::oneMetre, Ball::rubber, 0, Ball::autoBounces });
    twoBalls.processBlock ({ noteOnAt (0, 60), noteOnAt (0, 64) });
    twoBalls.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64) });
    twoBalls.processSeconds (30.0);
    EXPECT (twoBalls.notesStillSounding().empty());

    // 128 notes of super balls ask for more bounces than the queue holds: some balls stop early, none sticks.
    ChainTestRig crowd;
    crowd.loadModule (0, ModuleType::Bounce, { Ball::oneMetre, Ball::superBall, 0, Ball::autoBounces });
    MidiEventList everyNote;
    for (int note = 0; note < 128; ++note)
        everyNote.push_back (noteOnAt (0, note, 127));
    crowd.processBlock (everyNote);
    for (auto& event : everyNote)
        event = noteOffAt (0, event.noteNumber());
    crowd.processBlock (everyNote);
    crowd.processSeconds (30.0);
    EXPECT (crowd.notesStillSounding().empty());
    EXPECT (noteOnsOf (crowd).size() <= 128 + 4096);
}
