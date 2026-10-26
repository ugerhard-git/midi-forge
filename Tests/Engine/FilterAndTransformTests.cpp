/**
 * @file FilterAndTransformTests.cpp
 * @brief Behaviour of the filter and transform modules.
 */
#include "../TestFramework.h"
#include "ChainTestRig.h"
#include "Engine/Modules/TransformModules.h"

using namespace testing;

//==============================================================================
// Filters

TEST_CASE ("note range: passes notes inside the range only")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteRange, { 48, 72, 0 });
    const auto output = rig.processBlock ({ noteOnAt (0, 40), noteOnAt (1, 60), noteOnAt (2, 80) });
    EXPECT (output.size() == 1 && output[0].noteNumber() == 60);
}

TEST_CASE ("note range: block mode inverts, and swapped Low/High still work")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::NoteRange, { 72, 48, 1 });
    const auto output = rig.processBlock ({ noteOnAt (0, 40), noteOnAt (1, 60), noteOnAt (2, 80) });
    EXPECT (output.size() == 2 && output[0].noteNumber() == 40 && output[1].noteNumber() == 80);
}

TEST_CASE ("velocity range: blocks soft notes, and their note-offs never sound")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::VelocityRange, { 20, 127, 0 });
    auto output = rig.processBlock ({ noteOnAt (0, 60, 10), noteOnAt (1, 62, 100) });
    EXPECT (output.size() == 1 && output[0].noteNumber() == 62);
    rig.processBlock ({ noteOffAt (0, 60), noteOffAt (0, 62) });
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("channel filter: keeps one channel, including its controllers")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::ChannelFilter, { 2, 0 });
    const auto output =
        rig.processBlock ({ noteOnAt (0, 60, 100, 0), noteOnAt (0, 62, 100, 1), MidiEvent::controlChange (1, 0, 1, 64),
                            MidiEvent::controlChange (1, 1, 1, 64) });
    EXPECT (output.size() == 2);
    for (const auto& event : output)
        EXPECT (event.channel() == 1);
}

TEST_CASE ("event blocker: blocks only the selected event classes")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::EventBlocker, { 0, 1, 0, 1 }); // block CC and aftertouch/program
    const MidiEvent pitchBend { 0, 0xE0, 0, 64, 3 };
    const MidiEvent programChange { 0, 0xC0, 5, 0, 2 };
    const MidiEvent clock { 0, 0xF8, 0, 0, 1 };
    const auto output = rig.processBlock (
        { MidiEvent::controlChange (0, 0, 1, 64), noteOnAt (0, 60), pitchBend, programChange, clock });
    EXPECT (output.size() == 3);
    for (const auto& event : output)
        EXPECT (! event.isControlChange() && ! event.isProgramChange());
}

//==============================================================================
// Transforms

TEST_CASE ("transpose: note-off follows the note-on even after the amount changed")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 7, 1 });
    auto output = rig.processBlock ({ noteOnAt (10, 60) });
    EXPECT (output.size() == 1 && output[0].isNoteOn() && output[0].noteNumber() == 79);

    rig.setParameter (0, 0, -3);
    output = rig.processBlock ({ noteOffAt (5, 60) });
    EXPECT (output.size() == 1 && output[0].isNoteOff() && output[0].noteNumber() == 79);
    EXPECT (rig.notesStillSounding().empty());
}

TEST_CASE ("transpose: notes pushed outside the MIDI range are dropped")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 24, 4 });
    EXPECT (rig.processBlock ({ noteOnAt (0, 100) }).empty());
}

TEST_CASE ("transpose: poly pressure follows the transposed note")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::Transpose, { 5, 0 });
    rig.processBlock ({ noteOnAt (0, 60) });
    const MidiEvent pressure { 0, 0xA0, 60, 90, 3 };
    const auto output = rig.processBlock ({ pressure });
    EXPECT (output.size() == 1 && output[0].isPolyPressure() && output[0].noteNumber() == 65);
}

TEST_CASE ("scale quantize: nearest, up and down snapping")
{
    const auto cMajor = scales[0].semitoneMask;
    EXPECT (quantizeToScale (61, 0, cMajor, 0) == 60); // C# -> C (tie goes down)
    EXPECT (quantizeToScale (61, 0, cMajor, 1) == 62); // C# -> D
    EXPECT (quantizeToScale (63, 0, cMajor, 2) == 62); // D# -> D
    EXPECT (quantizeToScale (66, 0, cMajor, 0) == 65); // F# -> F
    EXPECT (quantizeToScale (64, 0, cMajor, 0) == 64); // E stays

    const auto dMinorPentatonic = scales[10].semitoneMask;       // D F G A C
    EXPECT (quantizeToScale (64, 2, dMinorPentatonic, 1) == 65); // E -> F
}

TEST_CASE ("scale quantize: two keys on one scale note keep it sounding until both are released")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::ScaleQuantize, { 0, 0, 0 });
    rig.processBlock ({ noteOnAt (0, 60), noteOnAt (1, 61) });
    EXPECT (rig.processBlock ({ noteOffAt (0, 61) }).empty());
    const auto output = rig.processBlock ({ noteOffAt (0, 60) });
    EXPECT (output.size() == 1 && output[0].isNoteOff() && output[0].noteNumber() == 60);
}

TEST_CASE ("scale quantize: never leaves the MIDI range")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::ScaleQuantize, { 11, 0, 1 }); // B major, snap up
    const auto output = rig.processBlock ({ noteOnAt (0, 127), noteOnAt (0, 0) });
    for (const auto& event : output)
        EXPECT (event.noteNumber() <= 127);
}

TEST_CASE ("velocity curve: linear, curved, compressed and fixed")
{
    EXPECT (shapeVelocity (1, 0.0f, 1, 127, 0) == 1);
    EXPECT (shapeVelocity (127, 0.0f, 1, 127, 0) == 127);
    EXPECT (shapeVelocity (64, 0.0f, 1, 127, 0) == 64);
    EXPECT (shapeVelocity (64, 1.0f, 1, 127, 0) < 64);  // less sensitive
    EXPECT (shapeVelocity (64, -1.0f, 1, 127, 0) > 64); // more sensitive
    EXPECT (shapeVelocity (127, 0.0f, 40, 90, 0) == 90);
    EXPECT (shapeVelocity (1, 0.0f, 40, 90, 0) == 40);
    EXPECT (shapeVelocity (12, 0.0f, 1, 127, 100) == 100);
}

TEST_CASE ("channel remap: moves notes and controllers, from one channel or all")
{
    ChainTestRig rig;
    rig.loadModule (0, ModuleType::ChannelRemap, { 1, 5 });
    const auto output = rig.processBlock (
        { noteOnAt (0, 60, 100, 0), noteOnAt (0, 62, 100, 3), MidiEvent::controlChange (0, 0, 7, 100) });
    EXPECT (output.size() == 3);
    EXPECT (output[0].channel() == 4 && output[1].channel() == 3 && output[2].channel() == 4);

    rig.setParameter (0, 1, 16);
    const auto release = rig.processBlock ({ noteOffAt (0, 60, 0) });
    EXPECT (release.size() == 1 && release[0].channel() == 4); // note-off goes where the note went
}
