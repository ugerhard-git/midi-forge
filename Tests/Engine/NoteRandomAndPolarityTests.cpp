/**
 * @file NoteRandomAndPolarityTests.cpp
 * @brief Route polarity (bipolar / unipolar) and the Note Random modulators.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"

#include <cmath>
#include <set>

using namespace testing;

namespace
{
constexpr int random1 = firstNoteRandomSource; // source index of Random 1
constexpr int samplesPerMillisecond = 48;      // at the test sample rate

bool isClose (double actual, double expected, double allowedDifference = 1.0e-4)
{
    return std::abs (actual - expected) <= allowedDifference;
}

NoteRandomSettings randomSettings (double windowMilliseconds, NoteRandomTrigger trigger, int numSteps)
{
    NoteRandomSettings settings;
    settings.chordWindowMilliseconds = windowMilliseconds;
    settings.trigger = trigger;
    settings.numSteps = numSteps;
    return settings;
}

/** Transpose in slot 1 with a centred knob, so the played note shows the modulation: +-24 st at 100 %. */
void setUpTransposeProbe (ChainTestRig& rig)
{
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
}

int transposedNoteOf (const MidiEventList& output)
{
    for (const auto& event : output)
        if (event.isNoteOn())
            return event.noteNumber();
    return -1;
}
} // namespace

//==============================================================================
// Polarity

TEST_CASE ("polarity: unipolar routes only push up, or only down with a negative amount")
{
    SlotSettings base[numSlots];
    base[0].parameterValues[0] = 0.5f;
    base[0].parameterValues[1] = 0.5f;
    ModulationRoute routes[numModulationRoutes];
    routes[0] = { 0, modulationTargetIndex (0, 0), 0.25f, true };
    routes[1] = { 0, modulationTargetIndex (0, 1), -0.25f, true };

    ModulationSourceValues values;
    SlotSettings modulated[numSlots];
    for (float unipolar : { 0.0f, 0.5f, 1.0f })
    {
        values.unipolar[0] = unipolar;
        values.bipolar[0] = unipolar * 2.0f - 1.0f;
        applyModulationRoutes (values, routes, base, modulated);
        EXPECT (isClose (modulated[0].parameterValues[0], 0.5 + 0.25 * unipolar)); // never below the knob
        EXPECT (isClose (modulated[0].parameterValues[1], 0.5 - 0.25 * unipolar)); // never above the knob
    }
}

TEST_CASE ("polarity: unipolar sine and triangle start their cycle at the lowest point")
{
    Lfo lfo;
    LfoSettings settings;
    BlockContext atStart;
    atStart.isPlaying = atStart.hasPpqPosition = true;
    atStart.ppqPositionAtBlockStart = 0.0;

    for (auto waveform : { LfoWaveform::Sine, LfoWaveform::Triangle })
    {
        settings.waveform = waveform;
        EXPECT (isClose (lfo.unipolarValueAt (settings, atStart), 0.0)); // lowest point at the start
        BlockContext halfCycle = atStart;
        halfCycle.ppqPositionAtBlockStart = 2.0;                           // half of a one-bar cycle
        EXPECT (isClose (lfo.unipolarValueAt (settings, halfCycle), 1.0)); // highest point half way
        EXPECT (isClose (lfo.valueAt (settings, atStart), 0.0));           // bipolar unchanged: starts at 0
    }

    settings.waveform = LfoWaveform::SawUp;
    EXPECT (isClose (lfo.unipolarValueAt (settings, atStart), 0.0));
    settings.waveform = LfoWaveform::Square;
    EXPECT (isClose (lfo.unipolarValueAt (settings, atStart), 1.0));
}

TEST_CASE ("polarity: with restart on key every phrase starts at the knob position and rises")
{
    ChainTestRig rig;
    setUpTransposeProbe (rig);
    rig.lfos[0].waveform = LfoWaveform::Sine;
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 0.43;
    rig.lfos[0].restartsOnFirstKey = true;
    rig.routeLfo (0, 0, 0, 0, 50.0f, true);

    for (int phrase = 0; phrase < 3; ++phrase)
    {
        EXPECT (transposedNoteOf (rig.processBlock ({ noteOnAt (0, 60) })) == 60); // starts at the knob (0 st)
        rig.processSeconds (0.5);
        const int later = transposedNoteOf (rig.processBlock ({ noteOnAt (0, 64) }));
        EXPECT (later > 64); // has risen above the knob
        rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64) });
        rig.processSeconds (0.9);
    }
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("polarity: one LFO drives a bipolar and a unipolar route at the same time")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
    rig.loadModule (1, ModuleType::Transpose, { 0, 0 });
    rig.lfos[0].waveform = LfoWaveform::Square;
    rig.lfos[0].isTempoSynced = false;
    rig.lfos[0].rateHz = 0.01;
    rig.lfos[0].phaseDegrees = 200.0;        // in the low half of the square: -1 bipolar, 0 unipolar
    rig.routeLfo (0, 0, 0, 0, 25.0f, false); // slot 1: -12 st
    rig.routeLfo (1, 0, 1, 0, 25.0f, true);  // slot 2: +0 st
    EXPECT (transposedNoteOf (rig.processBlock ({ noteOnAt (0, 60) })) == 48);
}

//==============================================================================
// Note Random

TEST_CASE ("note random: steps give evenly spaced values, smooth gives any value")
{
    EXPECT (isClose (NoteRandom::valueFromUniform (0.0, 0), -1.0));
    EXPECT (isClose (NoteRandom::valueFromUniform (0.75, 0), 0.5));
    EXPECT (isClose (NoteRandom::valueFromUniform (0.0, 2), -1.0));
    EXPECT (isClose (NoteRandom::valueFromUniform (0.99, 2), 1.0));
    EXPECT (isClose (NoteRandom::valueFromUniform (0.5, 3), 0.0));
    EXPECT (isClose (NoteRandom::valueFromUniform (0.3, 5), -0.5));

    NoteRandom random (17);
    std::set<float> seenValues;
    for (int note = 0; note < 400; ++note)
    {
        random.noteStarted (note * 100000, true, randomSettings (0, NoteRandomTrigger::EveryNote, 4), 48000.0);
        seenValues.insert (random.getValue());
    }
    EXPECT (seenValues == std::set<float> ({ -1.0f, -1.0f / 3.0f, 1.0f / 3.0f, 1.0f }));
}

TEST_CASE ("note random: a new value per note, held until the next note")
{
    NoteRandom random (5);
    const auto settings = randomSettings (30, NoteRandomTrigger::EveryNote, 0);
    EXPECT (random.getValue() == 0.0f); // nothing played yet

    std::set<float> values;
    for (int note = 0; note < 20; ++note)
    {
        EXPECT (random.noteStarted (note * 48000, true, settings, 48000.0));
        values.insert (random.getValue());
        EXPECT (random.getValue() >= -1.0f && random.getValue() <= 1.0f);
    }
    EXPECT (values.size() == 20);
}

TEST_CASE ("note random: notes within the chord window share the first note's value")
{
    NoteRandom random (9);
    const auto settings = randomSettings (30, NoteRandomTrigger::EveryNote, 0);
    const int window = 30 * samplesPerMillisecond;

    EXPECT (random.noteStarted (1000, true, settings, 48000.0));
    const float chordValue = random.getValue();
    EXPECT (! random.noteStarted (1000, false, settings, 48000.0));          // same moment
    EXPECT (! random.noteStarted (1000 + window, false, settings, 48000.0)); // exactly at the edge
    EXPECT (random.getValue() == chordValue);
    EXPECT (random.noteStarted (1000 + window + 1, false, settings, 48000.0)); // just outside
    EXPECT (random.getValue() != chordValue);

    // The window is measured from the note that rolled, not from the latest note.
    const int64_t secondChord = 1000 + window + 1;
    EXPECT (! random.noteStarted (secondChord + window - 10, false, settings, 48000.0));
    EXPECT (random.noteStarted (secondChord + window + 10, false, settings, 48000.0));

    NoteRandom noWindow (9);
    const auto zeroWindow = randomSettings (0, NoteRandomTrigger::EveryNote, 0);
    EXPECT (noWindow.noteStarted (500, true, zeroWindow, 48000.0));
    EXPECT (! noWindow.noteStarted (500, false, zeroWindow, 48000.0)); // exactly together: one chord
    EXPECT (noWindow.noteStarted (501, false, zeroWindow, 48000.0));
}

TEST_CASE ("note random: phrase mode rolls only on a key pressed while no other key is held")
{
    NoteRandom random (3);
    const auto settings = randomSettings (0, NoteRandomTrigger::FirstNoteOfPhrase, 0);
    EXPECT (random.noteStarted (0, true, settings, 48000.0));
    const float phraseValue = random.getValue();
    EXPECT (! random.noteStarted (48000, false, settings, 48000.0)); // legato note: no new value
    EXPECT (random.getValue() == phraseValue);
    EXPECT (random.noteStarted (96000, true, settings, 48000.0)); // new phrase
}

TEST_CASE ("note random: an uneven chord gets one value; the next chord a new one")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 0, 0 });
    rig.noteRandoms[0] = randomSettings (30, NoteRandomTrigger::EveryNote, 0);
    rig.routeLfo (0, random1, 0, 0, 50.0f);

    // A chord with its notes spread over 20 ms (960 samples), across block boundaries.
    MidiEventList chordOutput;
    for (const auto& output : { rig.processBlock ({ noteOnAt (100, 60) }), rig.processBlock ({ noteOnAt (50, 64) }),
                                rig.processBlock(), rig.processBlock ({ noteOnAt (200, 67) }) })
        chordOutput.insert (chordOutput.end(), output.begin(), output.end());

    std::vector<int> shifts;
    const int playedNotes[] = { 60, 64, 67 };
    int index = 0;
    for (const auto& event : chordOutput)
        if (event.isNoteOn())
            shifts.push_back (event.noteNumber() - playedNotes[index++]);
    EXPECT (shifts.size() == 3);
    if (shifts.size() == 3)
        EXPECT (shifts[0] == shifts[1] && shifts[1] == shifts[2]);

    rig.processSeconds (0.2);
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 64), noteOffAt (0, 67) });
    std::set<int> laterShifts;
    for (int chord = 0; chord < 10; ++chord)
    {
        laterShifts.insert (transposedNoteOf (rig.processBlock ({ noteOnAt (0, 60) })) - 60);
        rig.processSeconds (0.1);
        rig.processBlock ({ noteOffAt (0, 60) });
    }
    EXPECT (laterShifts.size() > 5); // new values for new notes
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("note random: notes the modules generate do not roll new values")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Arpeggiator, { 3, 0, 1, 50 }); // 1/16 up, one held key
    rig.loadModule (1, ModuleType::Transpose, { 0, 0 });
    rig.noteRandoms[0] = randomSettings (0, NoteRandomTrigger::EveryNote, 0);
    rig.routeLfo (0, random1, 1, 0, 50.0f);
    rig.processBlock ({ noteOnAt (0, 60) });
    rig.processSeconds (1.0);

    std::set<int> arpeggioNotes;
    for (const auto& event : rig.everyOutputEvent)
        if (event.isNoteOn())
            arpeggioNotes.insert (event.noteNumber());
    EXPECT (arpeggioNotes.size() == 1); // all steps share the value rolled by the played key
}
