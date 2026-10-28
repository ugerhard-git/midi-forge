/**
 * @file PluginTests.cpp
 * @brief Tests of the JUCE plug-in layer: parameters, presets, state, slot editing and MIDI routing.
 *
 * Usage: midi_forge_plugin_tests [name filter] [--snapshot-dir folder]
 * With --snapshot-dir both editor pages are rendered to editor.png and modulation.png in that folder,
 * plus the Steps sub-tab with the "Breathing Steps" preset (modulation-steps.png), the module browser
 * (module-browser.png) and the module info pop-up (module-info.png). CI publishes
 * them for visual review and they are used in the manual.
 */
#include "../TestFramework.h"

#include "Plugin/FactoryPresets.h"
#include "Plugin/ParameterIds.h"
#include "Plugin/PluginProcessor.h"
#include "Plugin/RecentModuleList.h"
#include "Plugin/UserPresetLibrary.h"
#include "UI/ModuleBrowser.h"
#include "UI/PluginEditor.h"
#include "UI/PresetBar.h"
#include "UI/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

using namespace midiforge;

namespace
{
constexpr double testSampleRate = 48000.0;
constexpr int testBlockSize = 512;

/** Runs one block through the processor and returns the MIDI it produced. */
juce::MidiBuffer processBlock (MidiForgeProcessor& processor, const juce::MidiBuffer& input)
{
    juce::AudioBuffer<float> noAudio (0, testBlockSize);
    juce::MidiBuffer midi (input);
    processor.processBlock (noAudio, midi);
    return midi;
}

std::vector<juce::MidiMessage> toMessages (const juce::MidiBuffer& buffer)
{
    std::vector<juce::MidiMessage> messages;
    for (const auto metadata : buffer)
        messages.push_back (metadata.getMessage());
    return messages;
}
} // namespace

TEST_CASE ("plugin: exposes slot, LFO and route parameters with stable IDs")
{
    MidiForgeProcessor processor;
    int numOurParameters = 0;
    for (auto* parameter : processor.getParameters())
        if (dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter) != nullptr)
            ++numOurParameters;
    constexpr int parametersPerSlot = 10, parametersPerLfo = 7, parametersPerRoute = 4, parametersPerRandom = 3,
                  parametersPerStepModulator = 4 + maximumSteps + (maximumSteps - 1); // incl. ties (0.16.0)
    EXPECT (numOurParameters == numSlots * parametersPerSlot + numLfos * parametersPerLfo +
                                    numModulationRoutes * parametersPerRoute + numNoteRandoms * parametersPerRandom +
                                    numStepModulators * parametersPerStepModulator);

    // Parameters added later are appended: older ones keep their position, the newest come last.
    const auto idAt = [&processor] (int index)
    { return dynamic_cast<juce::AudioProcessorParameterWithID*> (processor.getParameters()[index])->paramID; };
    const int numStepParameters = numStepModulators * parametersPerStepModulator;
    constexpr int numNewRouteParameters = (numModulationRoutes - 8) * parametersPerRoute; // routes 9..12 (0.20.0)
    const int beforeNewRoutes = numOurParameters - numNewRouteParameters;
    EXPECT (idAt (numSlots * 6 + numLfos * 7 + 8 * 3 - 1) == "r8_amount");                    // last of 0.2.0
    EXPECT (idAt (beforeNewRoutes - 1 - numStepParameters - numSlots * 4) == "r8_polarity");  // last of 0.6.0
    EXPECT (idAt (beforeNewRoutes - 1 - numStepParameters) == "s8_h");                        // last of 0.7.0
    EXPECT (idAt (beforeNewRoutes - 1 - numStepModulators * (maximumSteps - 1)) == "q4_v16"); // last of 0.14.0
    EXPECT (idAt (beforeNewRoutes - 1) == "q4_t16");                                          // ties (0.16.0)
    EXPECT (idAt (beforeNewRoutes) == "r9_source");                                           // routes 9..12 last
    EXPECT (idAt (numOurParameters - 1) == "r12_polarity");

    for (const char* id : { "s1_type",    "s8_d",     "l1_wave", "l4_restart", "r1_source", "r8_amount", "r1_polarity",
                            "n1_window",  "n4_steps", "s1_e",    "s8_h",       "q1_steps",  "q2_rate",   "q3_glide",
                            "q4_restart", "q1_v1",    "q4_v16",  "q1_t2",      "q4_t16",    "r9_target", "r12_amount" })
        EXPECT (processor.parameters.getParameter (id) != nullptr);
    EXPECT (processor.isMidiEffect() && processor.acceptsMidi() && processor.producesMidi());

    // Slot parameters stay at the start, so hosts that address parameters by index keep working.
    EXPECT (dynamic_cast<juce::AudioProcessorParameterWithID*> (processor.getParameters()[0])->paramID == "s1_type");
}

TEST_CASE ("plugin: loading a module sets its defaults and the host text follows the module")
{
    MidiForgeProcessor processor;
    processor.loadModule (2, (int) ModuleType::Transpose);
    EXPECT (processor.getModuleType (2) == (int) ModuleType::Transpose);

    auto* semitones = processor.parameters.getParameter (ParameterIds::moduleParameter (2, 0));
    EXPECT (semitones->getCurrentValueAsText() == "0 st");
    semitones->setValueNotifyingHost (semitones->getValueForText ("+7 st"));
    EXPECT (semitones->getCurrentValueAsText() == "+7 st");

    processor.loadModule (2, (int) ModuleType::Arpeggiator);
    EXPECT (semitones->getCurrentValueAsText() == "1/16"); // same host parameter, new meaning
}

TEST_CASE ("plugin: every factory preset loads and only uses valid values")
{
    MidiForgeProcessor processor;
    const auto& presets = getFactoryPresets();
    EXPECT (processor.getNumPrograms() == (int) presets.size());
    EXPECT (presets.front().slots.empty());

    for (int presetIndex = 0; presetIndex < (int) presets.size(); ++presetIndex)
    {
        const auto& preset = presets[(size_t) presetIndex];
        EXPECT ((int) preset.slots.size() <= numSlots);
        for (const auto& slot : preset.slots)
        {
            const auto& description = getModuleDescription (slot.moduleType);
            for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            {
                const float value = slot.valueOf (parameterIndex);
                if (std::isnan (value))
                    continue;
                EXPECT (description.parameters[parameterIndex].isUsed());
                EXPECT (value >= description.parameters[parameterIndex].minimum);
                EXPECT (value <= description.parameters[parameterIndex].maximum);
            }
        }

        processor.setCurrentProgram (presetIndex);
        EXPECT (processor.getCurrentProgram() == presetIndex);
        for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        {
            const int expectedType =
                slotIndex < (int) preset.slots.size() ? (int) preset.slots[(size_t) slotIndex].moduleType : 0;
            EXPECT (processor.getModuleType (slotIndex) == expectedType);
        }
    }
}

TEST_CASE ("plugin: state survives a save and load round-trip")
{
    MidiForgeProcessor original;
    original.setCurrentProgram (4);
    original.swapSlots (0, 6);
    juce::MemoryBlock savedState;
    original.getStateInformation (savedState);

    MidiForgeProcessor restored;
    restored.setStateInformation (savedState.getData(), (int) savedState.getSize());
    EXPECT (restored.getCurrentProgram() == 4);
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        EXPECT (restored.getModuleType (slotIndex) == original.getModuleType (slotIndex));
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            EXPECT (std::abs (restored.getParameterValue (slotIndex, parameterIndex) -
                              original.getParameterValue (slotIndex, parameterIndex)) < 1.0e-6f);
    }
}

TEST_CASE ("plugin: damaged or foreign state data is ignored")
{
    MidiForgeProcessor processor;
    processor.setCurrentProgram (1);
    const int typeBefore = processor.getModuleType (0);

    const char garbage[] = "this is not a plug-in state";
    processor.setStateInformation (garbage, (int) sizeof (garbage));

    juce::MemoryBlock foreignState;
    juce::XmlElement foreign ("SomeOtherPlugin");
    juce::AudioProcessor::copyXmlToBinary (foreign, foreignState);
    processor.setStateInformation (foreignState.getData(), (int) foreignState.getSize());

    processor.setStateInformation (nullptr, 0);
    EXPECT (processor.getModuleType (0) == typeBefore);
}

TEST_CASE ("plugin: swapping slots moves module, switch and parameters")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Chord);
    processor.loadModule (1, (int) ModuleType::Transpose);
    processor.parameters.getParameter (ParameterIds::slotEnabled (1))->setValueNotifyingHost (0.0f);
    const float chordValue = processor.getParameterValue (0, 3);

    processor.swapSlots (0, 1);
    EXPECT (processor.getModuleType (0) == (int) ModuleType::Transpose);
    EXPECT (processor.getModuleType (1) == (int) ModuleType::Chord);
    EXPECT (! processor.isSlotEnabled (0) && processor.isSlotEnabled (1));
    EXPECT (std::abs (processor.getParameterValue (1, 3) - chordValue) < 1.0e-6f);

    processor.swapSlots (0, -1); // out of range: ignored
    processor.swapSlots (7, 8);
    EXPECT (processor.getModuleType (0) == (int) ModuleType::Transpose);
}

TEST_CASE ("plugin: processes MIDI through the chain and passes SysEx through untouched")
{
    MidiForgeProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processor.loadModule (0, (int) ModuleType::Transpose);
    auto* semitones = processor.parameters.getParameter (ParameterIds::moduleParameter (0, 0));
    semitones->setValueNotifyingHost (semitones->getValueForText ("12"));

    juce::MidiBuffer input;
    input.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
    const juce::uint8 sysexData[] = { 0x7E, 0x7F, 0x06, 0x01 };
    input.addEvent (juce::MidiMessage::createSysExMessage (sysexData, (int) sizeof (sysexData)), 20);

    const auto messages = toMessages (processBlock (processor, input));
    EXPECT (messages.size() == 2);
    bool foundTransposedNote = false;
    bool foundSysex = false;
    for (const auto& message : messages)
    {
        foundTransposedNote = foundTransposedNote || (message.isNoteOn() && message.getNoteNumber() == 72);
        foundSysex = foundSysex || (message.isSysEx() && message.getSysExDataSize() == (int) sizeof (sysexData));
    }
    EXPECT (foundTransposedNote && foundSysex);
    processor.releaseResources();
}

TEST_CASE ("plugin: panic sends All Notes Off on all 16 channels")
{
    MidiForgeProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processor.panic();
    int numAllNotesOff = 0;
    for (const auto& message : toMessages (processBlock (processor, {})))
        numAllNotesOff += message.isAllNotesOff() ? 1 : 0;
    EXPECT (numAllNotesOff == 16);
}

TEST_CASE ("plugin: the editor opens, follows the processor and closes cleanly")
{
    MidiForgeProcessor processor;
    processor.setCurrentProgram (1);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    EXPECT (editor != nullptr);
    if (editor != nullptr)
    {
        EXPECT (editor->getWidth() > 0 && editor->getHeight() > 0);
    }
}

//==============================================================================
// User presets

namespace
{
/** A fresh, empty folder for one test; deleted again when the test ends. */
struct TemporaryPresetFolder
{
    TemporaryPresetFolder()
        : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("midi-forge-tests-" + juce::Uuid().toString()))
    {
    }
    ~TemporaryPresetFolder() { folder.deleteRecursively(); }

    juce::File folder;
};

juce::ValueTree stateOf (MidiForgeProcessor& processor)
{
    return processor.parameters.copyState();
}
} // namespace

TEST_CASE ("user presets: names are cleaned to safe file names")
{
    EXPECT (UserPresetLibrary::cleanName ("My Preset") == "My Preset");
    EXPECT (UserPresetLibrary::cleanName ("  Bass: low/high?  ") == "Bass low high");
    EXPECT (UserPresetLibrary::cleanName ("..hidden..") == "hidden");
    EXPECT (UserPresetLibrary::cleanName ("back\\slash") == "back slash");
    EXPECT (UserPresetLibrary::cleanName ("tab\there") == "tab here"); // control characters become spaces
    EXPECT (UserPresetLibrary::cleanName ("../../etc/passwd") == "etc passwd");
    EXPECT (UserPresetLibrary::cleanName ("CON").isEmpty());
    EXPECT (UserPresetLibrary::cleanName ("lpt1").isEmpty());
    EXPECT (UserPresetLibrary::cleanName ("   ").isEmpty());
    EXPECT (UserPresetLibrary::cleanName ("***").isEmpty());
    EXPECT (UserPresetLibrary::cleanName (juce::String::repeatedString ("x", 200)).length() ==
            UserPresetLibrary::maximumNameLength);
    EXPECT (UserPresetLibrary::cleanName (juce::String::fromUTF8 ("Gro\xc3\x9f"
                                                                  "e Akkorde")) ==
            juce::String::fromUTF8 ("Gro\xc3\x9f"
                                    "e Akkorde"));
}

TEST_CASE ("user presets: library saves, lists, loads, renames and deletes")
{
    TemporaryPresetFolder temporary;
    const UserPresetLibrary library (temporary.folder.getChildFile ("Presets"));
    MidiForgeProcessor processor;

    EXPECT (library.getPresetNames().isEmpty()); // folder does not exist yet
    EXPECT (library.save ("beta", stateOf (processor)).isEmpty());
    EXPECT (library.save ("Alpha", stateOf (processor)).isEmpty());
    EXPECT (library.save ("Gamma 10", stateOf (processor)).isEmpty());
    EXPECT (library.save ("Gamma 9", stateOf (processor)).isEmpty());
    EXPECT (library.getPresetNames() == juce::StringArray ({ "Alpha", "beta", "Gamma 9", "Gamma 10" }));
    EXPECT (library.save ("   ", stateOf (processor)).isNotEmpty()); // no usable name

    EXPECT (library.load ("Alpha").has_value());
    EXPECT (! library.load ("Missing").has_value());

    EXPECT (library.rename ("Alpha", "Omega").isEmpty());
    EXPECT (library.contains ("Omega") && ! library.contains ("Alpha"));
    EXPECT (library.rename ("Omega", "beta").isNotEmpty()); // name taken
    EXPECT (library.rename ("Omega", "omega").isEmpty());   // only the case changes

    EXPECT (library.remove ("beta"));
    EXPECT (! library.contains ("beta"));
    EXPECT (! library.remove ("beta"));
}

TEST_CASE ("user presets: damaged, foreign and oversized files are rejected")
{
    TemporaryPresetFolder temporary;
    temporary.folder.createDirectory();
    const UserPresetLibrary library (temporary.folder);

    temporary.folder.getChildFile ("Garbage.mfpreset").replaceWithText ("this is not xml <<<");
    temporary.folder.getChildFile ("Foreign.mfpreset").replaceWithText ("<SomethingElse><MidiForge/></SomethingElse>");
    temporary.folder.getChildFile ("Empty.mfpreset").replaceWithText ("<MidiForgePreset formatVersion=\"1\"/>");
    temporary.folder.getChildFile ("Huge.mfpreset")
        .replaceWithText ("<MidiForgePreset><MidiForge>" + juce::String::repeatedString ("<PARAM/>", 60000) +
                          "</MidiForge></MidiForgePreset>");

    for (const char* name : { "Garbage", "Foreign", "Empty", "Huge" })
        EXPECT (! library.load (name).has_value());

    juce::String importedName;
    EXPECT (library.importFile (temporary.folder.getChildFile ("Garbage.mfpreset"), importedName).isNotEmpty());
}

TEST_CASE ("user presets: import copies valid files without overwriting")
{
    TemporaryPresetFolder temporary;
    const UserPresetLibrary source (temporary.folder.getChildFile ("Somewhere"));
    const UserPresetLibrary library (temporary.folder.getChildFile ("Presets"));
    MidiForgeProcessor processor;
    EXPECT (source.save ("Shared Arp", stateOf (processor)).isEmpty());
    const auto sharedFile = source.getFolder().getChildFile ("Shared Arp.mfpreset");

    juce::String firstName, secondName;
    EXPECT (library.importFile (sharedFile, firstName).isEmpty());
    EXPECT (library.importFile (sharedFile, secondName).isEmpty());
    EXPECT (firstName == "Shared Arp" && secondName == "Shared Arp (2)");
    EXPECT (library.getPresetNames().size() == 2);
}

TEST_CASE ("user presets: save, modify, reload and the modified marker")
{
    TemporaryPresetFolder temporary;
    MidiForgeProcessor processor;
    processor.setUserPresetFolder (temporary.folder);
    processor.setCurrentProgram (6); // a factory preset with LFOs
    EXPECT (! processor.isCurrentPresetModified());

    auto* semitones = processor.parameters.getParameter (ParameterIds::moduleParameter (0, 2));
    semitones->setValueNotifyingHost (0.9f);
    EXPECT (processor.isCurrentPresetModified());

    EXPECT (processor.saveUserPreset ("My Arp").isEmpty());
    EXPECT (processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user);
    EXPECT (processor.getCurrentPresetName() == "My Arp");
    EXPECT (! processor.isCurrentPresetModified());
    const auto savedValues = stateOf (processor).createXml()->toString();

    processor.setCurrentProgram (0); // Init
    EXPECT (processor.loadUserPreset ("My Arp"));
    EXPECT (stateOf (processor).createXml()->toString() == savedValues);
    EXPECT (! processor.isCurrentPresetModified());
    EXPECT (processor.getLfoSettings (1).waveform == LfoWaveform::SampleAndHold); // LFOs are part of it

    EXPECT (! processor.loadUserPreset ("Does Not Exist"));
    EXPECT (processor.getCurrentPresetName() == "My Arp");
}

TEST_CASE ("user presets: values from files are clamped, missing ones get defaults")
{
    TemporaryPresetFolder temporary;
    temporary.folder.createDirectory();
    temporary.folder.getChildFile ("Edited.mfpreset")
        .replaceWithText ("<MidiForgePreset formatVersion=\"1\"><MidiForge>"
                          "<PARAM id=\"s1_type\" value=\"999\"/>"
                          "<PARAM id=\"r1_amount\" value=\"-5000\"/>"
                          "<PARAM id=\"l1_rate\" value=\"nan\"/>"
                          "<PARAM id=\"unknown_parameter\" value=\"1\"/>"
                          "</MidiForge></MidiForgePreset>");

    MidiForgeProcessor processor;
    processor.setUserPresetFolder (temporary.folder);
    processor.setCurrentProgram (1); // something that differs from the defaults
    EXPECT (processor.loadUserPreset ("Edited"));
    EXPECT (processor.getModuleType (0) == numModuleTypes - 1); // clamped to the last module
    EXPECT (processor.parameters.getParameter (ParameterIds::routeAmount (0))->getCurrentValueAsText() == "-100%");
    EXPECT (std::abs (processor.getLfoSettings (0).rateHz - 1.0) < 1.0e-3); // NaN -> default
    EXPECT (processor.getModuleType (1) == 0);                              // missing -> default (Empty)
}

TEST_CASE ("user presets: stepping goes through factory and user presets and wraps")
{
    TemporaryPresetFolder temporary;
    MidiForgeProcessor processor;
    processor.setUserPresetFolder (temporary.folder);
    EXPECT (processor.saveUserPreset ("Zeta").isEmpty());
    EXPECT (processor.saveUserPreset ("Alpha").isEmpty());

    processor.setCurrentProgram (processor.getNumPrograms() - 1); // last factory preset
    processor.stepPreset (+1);
    EXPECT (processor.getCurrentPresetName() == "Alpha");
    processor.stepPreset (+1);
    EXPECT (processor.getCurrentPresetName() == "Zeta");
    processor.stepPreset (+1);
    EXPECT (processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::factory);
    EXPECT (processor.getCurrentProgram() == 0); // wrapped around
    processor.stepPreset (-1);
    EXPECT (processor.getCurrentPresetName() == "Zeta");
}

TEST_CASE ("user presets: rename and delete the current preset; projects remember the name")
{
    TemporaryPresetFolder temporary;
    MidiForgeProcessor processor;
    processor.setUserPresetFolder (temporary.folder);
    EXPECT (processor.saveUserPreset ("Draft").isEmpty());
    EXPECT (processor.renameCurrentUserPreset ("Final").isEmpty());
    EXPECT (processor.getCurrentPresetName() == "Final");

    juce::MemoryBlock projectState;
    processor.getStateInformation (projectState);
    MidiForgeProcessor reopened;
    reopened.setStateInformation (projectState.getData(), (int) projectState.getSize());
    EXPECT (reopened.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user);
    EXPECT (reopened.getCurrentPresetName() == "Final");
    EXPECT (! reopened.isCurrentPresetModified());

    EXPECT (processor.deleteCurrentUserPreset());
    EXPECT (processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::none);
    EXPECT (processor.getCurrentPresetName() == "Untitled");
    EXPECT (! processor.getUserPresetLibrary().contains ("Final"));
    EXPECT (processor.renameCurrentUserPreset ("Again").isNotEmpty()); // nothing to rename now
}

//==============================================================================
// LFOs and modulation

TEST_CASE ("plugin: route targets are shown with the module's parameter names")
{
    MidiForgeProcessor processor;
    processor.loadModule (1, (int) ModuleType::Arpeggiator);
    EXPECT (processor.getModulationTargetName (0) == "None");
    EXPECT (processor.getModulationTargetName (modulationTargetIndex (1, 3)) ==
            juce::String::fromUTF8 ("Slot 2 \xc2\xb7 Gate"));
    EXPECT (processor.getModulationTargetName (modulationTargetIndex (0, 0)).endsWith ("A (unused)"));

    auto* target = processor.parameters.getParameter (ParameterIds::routeTarget (0));
    target->setValueNotifyingHost (target->getValueForText ("Slot 2 D"));
    EXPECT (target->getCurrentValueAsText() == juce::String::fromUTF8 ("Slot 2 \xc2\xb7 Gate"));
    target->setValueNotifyingHost (target->getValueForText (juce::String::fromUTF8 ("Slot 2 \xc2\xb7 Octaves")));
    EXPECT (target->getCurrentValueAsText() == juce::String::fromUTF8 ("Slot 2 \xc2\xb7 Octaves"));

    auto* rate = processor.parameters.getParameter (ParameterIds::lfoRate (0));
    rate->setValueNotifyingHost (rate->convertTo0to1 (0.25f));
    EXPECT (rate->getCurrentValueAsText() == "0.25 Hz");
}

TEST_CASE ("plugin: presets set up their LFOs and routes and clear them again")
{
    MidiForgeProcessor processor;
    const auto& presets = getFactoryPresets();
    int breathingArpIndex = -1;
    for (int index = 0; index < (int) presets.size(); ++index)
        if (juce::String (presets[(size_t) index].name) == "Breathing Arp")
            breathingArpIndex = index;
    EXPECT (breathingArpIndex > 0);

    processor.setCurrentProgram (breathingArpIndex);
    EXPECT (processor.isParameterModulated (1, 3)); // arpeggiator gate
    EXPECT (processor.isParameterModulated (0, 1)); // chord inversion
    EXPECT (processor.getLfoSettings (0).waveform == LfoWaveform::Triangle);
    EXPECT (processor.getLfoSettings (1).waveform == LfoWaveform::SampleAndHold);

    processor.setCurrentProgram (0); // Init
    EXPECT (! processor.isParameterModulated (1, 3) && ! processor.isParameterModulated (0, 1));
    EXPECT (processor.getLfoSettings (0).waveform == LfoWaveform::Sine);
}

TEST_CASE ("plugin: the modulated value shown on a knob swings below and above the knob position")
{
    MidiForgeProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processor.loadModule (0, (int) ModuleType::Transpose); // Semitones knob sits in the middle (0 st)

    auto setPlain = [&] (const juce::String& id, float plainValue)
    {
        auto* parameter = processor.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    setPlain (ParameterIds::lfoWaveform (0), (float) LfoWaveform::Sine);
    setPlain (ParameterIds::lfoSyncMode (0), 0.0f); // free
    setPlain (ParameterIds::lfoRate (0), 2.0f);     // 0.5 s per cycle
    setPlain (ParameterIds::routeTarget (0), (float) modulationTargetIndex (0, 0));
    setPlain (ParameterIds::routeAmount (0), 40.0f);

    const float knobValue = processor.getParameterValue (0, 0);
    float lowest = 1.0f, highest = 0.0f;
    const int blocksPerCycle = (int) (0.5 * testSampleRate / testBlockSize) + 1;
    for (int block = 0; block < blocksPerCycle; ++block)
    {
        processBlock (processor, {});
        lowest = std::min (lowest, processor.getModulatedParameterValue (0, 0));
        highest = std::max (highest, processor.getModulatedParameterValue (0, 0));
    }
    EXPECT (lowest < knobValue - 0.35f);  // 40 % below ...
    EXPECT (highest > knobValue + 0.35f); // ... and above the knob position
}

TEST_CASE ("plugin: an LFO route changes what the chain plays, the knob itself stays put")
{
    MidiForgeProcessor processor;
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processor.loadModule (0, (int) ModuleType::Transpose);

    auto setPlain = [&] (const juce::String& id, float plainValue)
    {
        auto* parameter = processor.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    setPlain (ParameterIds::lfoWaveform (0), (float) LfoWaveform::Square);
    setPlain (ParameterIds::lfoSyncMode (0), 0.0f); // free
    setPlain (ParameterIds::lfoRate (0), 0.01f);    // so slow it stays high during the test
    setPlain (ParameterIds::lfoPhase (0), 1.0f);
    setPlain (ParameterIds::routeTarget (0), (float) modulationTargetIndex (0, 0));
    setPlain (ParameterIds::routeAmount (0), 25.0f); // +25 % of 48 semitone steps = +12

    juce::MidiBuffer input;
    input.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    const auto messages = toMessages (processBlock (processor, input));
    EXPECT (messages.size() == 1 && messages[0].getNoteNumber() == 72);

    EXPECT (processor.isParameterModulated (0, 0));
    EXPECT (std::abs (processor.getParameterValue (0, 0) - 0.5f) < 1.0e-6f);           // the knob: unchanged
    EXPECT (std::abs (processor.getModulatedParameterValue (0, 0) - 0.75f) < 1.0e-3f); // what the chain used
    EXPECT (processor.getLfoValue (0) > 0.99f);
}

TEST_CASE ("plugin: route sources list the LFOs and the Random modulators; old projects keep their sources")
{
    MidiForgeProcessor processor;
    auto* source = processor.parameters.getParameter (ParameterIds::routeSource (0));
    source->setValueNotifyingHost (source->getValueForText ("Random 3"));
    EXPECT (source->getCurrentValueAsText() == "Random 3");

    // A project saved by 0.5.0: source index 2 (LFO 3), no polarity parameter yet.
    juce::MemoryBlock oldProject;
    juce::XmlElement oldState ("MidiForge");
    auto* oldSource = oldState.createNewChildElement ("PARAM");
    oldSource->setAttribute ("id", "r1_source");
    oldSource->setAttribute ("value", 2);
    juce::AudioProcessor::copyXmlToBinary (oldState, oldProject);
    processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
    EXPECT (source->getCurrentValueAsText() == "LFO 3");
    EXPECT (processor.parameters.getParameter (ParameterIds::routePolarity (0))->getCurrentValueAsText() == "Bipolar");
}

TEST_CASE ("plugin: Note Random settings map to the engine, and the preset uses them")
{
    MidiForgeProcessor processor;
    auto setPlain = [&] (const juce::String& id, float plainValue)
    {
        auto* parameter = processor.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    EXPECT (processor.getNoteRandomSettings (0).numSteps == 0);
    EXPECT (std::abs (processor.getNoteRandomSettings (0).chordWindowMilliseconds - 30.0) < 1.0e-3);
    setPlain (ParameterIds::noteRandomSteps (0), 3.0f); // choices: Smooth, 2, 3, 4 ...
    setPlain (ParameterIds::noteRandomTrigger (0), 1.0f);
    setPlain (ParameterIds::noteRandomWindow (0), 75.0f);
    EXPECT (processor.getNoteRandomSettings (0).numSteps == 4);
    EXPECT (processor.getNoteRandomSettings (0).trigger == NoteRandomTrigger::FirstNoteOfPhrase);
    EXPECT (std::abs (processor.getNoteRandomSettings (0).chordWindowMilliseconds - 75.0) < 1.0e-3);

    const auto& presets = getFactoryPresets();
    for (int index = 0; index < (int) presets.size(); ++index)
        if (juce::String (presets[(size_t) index].name) == "Random Voicings")
            processor.setCurrentProgram (index);
    EXPECT (processor.getCurrentPresetName() == "Random Voicings");
    EXPECT (processor.isParameterModulated (0, 1)); // chord inversion
    EXPECT (processor.parameters.getParameter (ParameterIds::routeSource (0))->getCurrentValueAsText() == "Random 1");
    EXPECT (processor.parameters.getParameter (ParameterIds::routePolarity (0))->getCurrentValueAsText() == "Unipolar");
    EXPECT (processor.getNoteRandomSettings (0).numSteps == 4);
    EXPECT (processor.getNoteRandomSettings (0).trigger == NoteRandomTrigger::EveryNote); // reset by the preset

    // Playing notes rolls values the editor can show.
    processor.prepareToPlay (testSampleRate, testBlockSize);
    std::set<float> values;
    for (int note = 0; note < 10; ++note)
    {
        juce::MidiBuffer input;
        input.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        processBlock (processor, input);
        values.insert (processor.getNoteRandomValue (0));
        juce::MidiBuffer release;
        release.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        for (int block = 0; block < 10; ++block)
            processBlock (processor, block == 0 ? release : juce::MidiBuffer());
    }
    EXPECT (values.size() > 1);
    for (float value : values)
        EXPECT (std::abs (std::abs (value) - 1.0f) < 1.0e-5f || std::abs (std::abs (value) - 1.0f / 3.0f) < 1.0e-5f);
}

TEST_CASE ("plugin: LFO and route settings survive a save and load round-trip")
{
    MidiForgeProcessor original;
    auto* wave = original.parameters.getParameter (ParameterIds::lfoWaveform (2));
    wave->setValueNotifyingHost (wave->convertTo0to1 ((float) LfoWaveform::SmoothRandom));
    auto* amount = original.parameters.getParameter (ParameterIds::routeAmount (5));
    amount->setValueNotifyingHost (amount->convertTo0to1 (-40.0f));
    juce::MemoryBlock savedState;
    original.getStateInformation (savedState);

    MidiForgeProcessor restored;
    restored.setStateInformation (savedState.getData(), (int) savedState.getSize());
    EXPECT (restored.getLfoSettings (2).waveform == LfoWaveform::SmoothRandom);
    EXPECT (restored.parameters.getParameter (ParameterIds::routeAmount (5))->getCurrentValueAsText() == "-40%");
}

namespace
{
/** Renders both editor pages to PNG files at twice the size, for the manual and for CI review. */
void writeEditorSnapshots (MidiForgeProcessor& processor, const juce::File& directory)
{
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    if (forgeEditor == nullptr)
        return;

    const std::pair<MidiForgeEditor::Page, const char*> pages[] = {
        { MidiForgeEditor::Page::chain, "editor.png" }, { MidiForgeEditor::Page::modulation, "modulation.png" }
    };
    for (const auto& [page, fileName] : pages)
    {
        forgeEditor->showPage (page);
        // Pick up the current state (knob texts, LFO positions, modulation rings), as the editor's timer would.
        forgeEditor->refreshFromProcessor();

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = directory.getChildFile (fileName);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("Editor snapshot written to %s\n", file.getFullPathName().toRawUTF8());
    }
}

/** Renders a pop-up component the way it looks inside the editor (dark panel, editor look and feel). */
void writeComponentSnapshot (juce::Component& component, const juce::File& file)
{
    ForgeLookAndFeel lookAndFeel;
    component.setLookAndFeel (&lookAndFeel);
    juce::Image image (juce::Image::ARGB, component.getWidth() * 2, component.getHeight() * 2, true);
    {
        juce::Graphics graphics (image);
        graphics.addTransform (juce::AffineTransform::scale (2.0f));
        graphics.fillAll (Theme::Colours::popupBackground);
        component.paintEntireComponent (graphics, true);
        graphics.setColour (Theme::Colours::slotOutline.brighter (0.3f));
        graphics.drawRect (component.getLocalBounds());
    }
    component.setLookAndFeel (nullptr);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("Snapshot written to %s\n", file.getFullPathName().toRawUTF8());
}

/** The module browser (as opened on an empty slot) and the Repeater's info pop-up. */
void writePopupSnapshots (const juce::File& directory)
{
    ModuleBrowser browser ((int) ModuleType::Empty, { (int) ModuleType::Repeater, (int) ModuleType::Chord },
                           [] (int) {});
    browser.setBounds (0, 0, ModuleBrowser::preferredWidth, ModuleBrowser::preferredHeight);
    browser.moveHighlight (1); // as if the mouse rested on the Repeater
    writeComponentSnapshot (browser, directory.getChildFile ("module-browser.png"));

    ModuleInfoView info;
    info.showModule ((int) ModuleType::Repeater);
    info.setSize (380, info.getHeightForWidth (380));
    writeComponentSnapshot (info, directory.getChildFile ("module-info.png"));
}

/** The Steps sub-tab of the Modulation tab, with the "Breathing Steps" preset playing for a moment. */
void writeStepsSnapshot (const juce::File& directory)
{
    MidiForgeProcessor processor;
    const auto& presets = getFactoryPresets();
    for (int index = 0; index < (int) presets.size(); ++index)
        if (juce::String (presets[(size_t) index].name) == "Breathing Steps")
            processor.setCurrentProgram (index);
    processor.prepareToPlay (testSampleRate, testBlockSize);
    for (int block = 0; block < 60; ++block)
        processBlock (processor, {});

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    if (forgeEditor == nullptr)
        return;
    forgeEditor->showPage (MidiForgeEditor::Page::modulation);
    forgeEditor->getModulationPage().showSourcePage (ModulationPage::SourcePage::steps);
    forgeEditor->refreshFromProcessor();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto file = directory.getChildFile ("modulation-steps.png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    std::printf ("Editor snapshot written to %s\n", file.getFullPathName().toRawUTF8());
}

/** Loads a preset with LFOs and runs the engine for a moment, so the snapshots show modulation. */
void prepareProcessorForSnapshots (MidiForgeProcessor& processor)
{
    const auto& presets = getFactoryPresets();
    for (int index = 0; index < (int) presets.size(); ++index)
        if (juce::String (presets[(size_t) index].name) == "Ratchet Chords")
            processor.setCurrentProgram (index);

    processor.prepareToPlay (testSampleRate, testBlockSize);
    for (int block = 0; block < 150; ++block)
        processBlock (processor, {});
}
} // namespace

//==============================================================================
// Knob pages and parameters E-H

TEST_CASE ("pages: the Repeater's page-2 knobs exist on the host with their own units")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Repeater);
    EXPECT (processor.parameters.getParameter ("s1_e")->getCurrentValueAsText() == "25");     // accent boost
    EXPECT (processor.parameters.getParameter ("s1_f")->getCurrentValueAsText() == "50%");    // note length
    EXPECT (processor.parameters.getParameter ("s1_g")->getCurrentValueAsText() == "100%");   // chance
    EXPECT (processor.parameters.getParameter ("s1_h")->getCurrentValueAsText() == "Steady"); // grid

    // Factory presets with a Repeater keep the module defaults on page 2.
    const auto& presets = getFactoryPresets();
    for (int index = 0; index < (int) presets.size(); ++index)
        if (juce::String (presets[(size_t) index].name) == "Hi-Hat Ratchet (Drums)")
            processor.setCurrentProgram (index);
    EXPECT (processor.getModuleType (1) == (int) ModuleType::Repeater);
    EXPECT (processor.parameters.getParameter ("s2_e")->getCurrentValueAsText() == "25");
    EXPECT (processor.parameters.getParameter ("s2_f")->getCurrentValueAsText() == "50%");
}

TEST_CASE ("pages: the shown page is per slot, reset by a new module, moved by swapping, and saved")
{
    juce::File presetFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("midi-forge-pages-" + juce::Uuid().toString());
    MidiForgeProcessor processor;
    processor.setUserPresetFolder (presetFolder);
    processor.loadModule (2, (int) ModuleType::Repeater);
    processor.loadModule (3, (int) ModuleType::Transpose);

    processor.setShownPage (2, 1);
    processor.setShownPage (3, 1);
    EXPECT (processor.getShownPage (2) == 1);
    EXPECT (processor.getShownPage (3) == 0); // Transpose has only one page

    juce::MemoryBlock project;
    processor.getStateInformation (project);
    EXPECT (processor.saveUserPreset ("Paged").isEmpty());

    processor.swapSlots (2, 5);
    EXPECT (processor.getShownPage (5) == 1 && processor.getShownPage (2) == 0);
    processor.loadModule (5, (int) ModuleType::Repeater); // a new module starts on page 1
    EXPECT (processor.getShownPage (5) == 0);

    MidiForgeProcessor reopened;
    reopened.setStateInformation (project.getData(), (int) project.getSize());
    EXPECT (reopened.getShownPage (2) == 1);

    EXPECT (processor.loadUserPreset ("Paged"));
    EXPECT (processor.getModuleType (2) == (int) ModuleType::Repeater && processor.getShownPage (2) == 1);
    presetFolder.deleteRecursively();
}

TEST_CASE ("stepped knobs: knobs snap to the steps of choice and whole-number parameters")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Repeater);
    const auto& repeater = getModuleDescription (ModuleType::Repeater);

    ParameterKnob rateKnob, swingKnob, timingKnob;
    rateKnob.attachTo (processor.parameters, "s1_a", repeater.parameters[0]); // 12 choices
    EXPECT (std::abs (rateKnob.knob.getInterval() - 1.0 / 11.0) < 1.0e-9);
    swingKnob.attachTo (processor.parameters, "s1_c", repeater.parameters[2]); // 50..75 %
    EXPECT (std::abs (swingKnob.knob.getInterval() - 1.0 / 25.0) < 1.0e-9);

    processor.loadModule (1, (int) ModuleType::Humanize);
    timingKnob.attachTo (processor.parameters, "s2_a", getModuleDescription (ModuleType::Humanize).parameters[0]);
    EXPECT (timingKnob.knob.getInterval() < 1.0e-12); // continuous (milliseconds)

    // Dragging lands on steps: a value between two choices snaps to one of them.
    rateKnob.knob.setValue (0.3);
    EXPECT (std::abs (rateKnob.knob.getValue() * 11.0 - std::round (rateKnob.knob.getValue() * 11.0)) < 1.0e-9);
}

namespace
{
/** @return The text the host shows for a slot parameter, in the units of the loaded module. */
juce::String hostTextOf (MidiForgeProcessor& processor, int slotIndex, int parameterIndex)
{
    return processor.parameters.getParameter (ParameterIds::moduleParameter (slotIndex, parameterIndex))
        ->getCurrentValueAsText();
}
} // namespace

TEST_CASE ("pages: projects from 0.6.0 load with page-2 defaults and keep their route targets")
{
    // A 0.6.0 project: slot 8 holds a Repeater, route 1 targets "Slot 8 D" (target number 32).
    juce::XmlElement oldState ("MidiForge");
    const auto addParameter = [&oldState] (const char* id, double value)
    {
        auto* parameter = oldState.createNewChildElement ("PARAM");
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    };
    addParameter ("s8_type", (double) ModuleType::Repeater);
    addParameter ("r1_target", 32);
    addParameter ("r1_amount", 30);
    juce::MemoryBlock oldProject;
    juce::AudioProcessor::copyXmlToBinary (oldState, oldProject);

    MidiForgeProcessor processor;
    processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
    EXPECT (processor.getModuleType (7) == (int) ModuleType::Repeater);
    EXPECT (processor.isParameterModulated (7, 3)); // still the Accent knob (D)
    EXPECT (processor.getModulationTargetName (32) == juce::String::fromUTF8 ("Slot 8 \xc2\xb7 Accent"));
    EXPECT (processor.getModulationTargetName (modulationTargetIndex (7, 5)) ==
            juce::String::fromUTF8 ("Slot 8 \xc2\xb7 Length"));
    // The knobs the Repeater gained later start at their defaults, not at the 0 an old project implies.
    EXPECT (hostTextOf (processor, 7, 4) == "25");
    EXPECT (hostTextOf (processor, 7, 5) == "50%");
    EXPECT (hostTextOf (processor, 7, 6) == "100%");
}

namespace
{
/** A Repeater project as 0.7.0 saved it: pages and E-H stored, but no knob layout version. */
juce::MemoryBlock createRepeaterProjectFrom070()
{
    juce::XmlElement oldState ("MidiForge");
    oldState.setAttribute ("shownPages", "1,0,0,0,0,0,0,0");
    const auto addParameter = [&oldState] (const juce::String& id, double value)
    {
        auto* parameter = oldState.createNewChildElement ("PARAM");
        parameter->setAttribute ("id", id);
        parameter->setAttribute ("value", value);
    };
    addParameter ("s1_type", (double) ModuleType::Repeater);
    addParameter ("s1_e", 40.0 / 64.0); // Accent Boost 40
    addParameter ("s1_f", 0.5);         // Length 55 %
    addParameter ("s1_g", 0.0);         // unused in 0.7.0
    juce::MemoryBlock project;
    juce::AudioProcessor::copyXmlToBinary (oldState, project);
    return project;
}
} // namespace

TEST_CASE ("repeater chance: 0.7.0 projects keep page 2 and get Chance 100 %; new projects keep their Chance")
{
    const auto oldProject = createRepeaterProjectFrom070();
    MidiForgeProcessor processor;
    processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
    EXPECT (processor.getModuleType (0) == (int) ModuleType::Repeater);
    EXPECT (hostTextOf (processor, 0, 4) == "40");
    EXPECT (hostTextOf (processor, 0, 5) == "55%");
    EXPECT (hostTextOf (processor, 0, 6) == "100%");
    EXPECT (processor.getShownPage (0) == 1);

    // Saved now, a Chance of 30 % survives.
    processor.parameters.getParameter (ParameterIds::moduleParameter (0, 6))->setValueNotifyingHost (0.3f);
    juce::MemoryBlock newProject;
    processor.getStateInformation (newProject);
    MidiForgeProcessor reloaded;
    reloaded.setStateInformation (newProject.getData(), (int) newProject.getSize());
    EXPECT (hostTextOf (reloaded, 0, 6) == "30%");
}

TEST_CASE ("repeater chance: user presets from 0.7.0 get Chance 100 %")
{
    TemporaryPresetFolder temporaryFolder;
    temporaryFolder.folder.createDirectory();
    const auto oldProject = createRepeaterProjectFrom070();
    const auto oldState = juce::ValueTree::fromXml (
        *juce::AudioProcessor::getXmlFromBinary (oldProject.getData(), (int) oldProject.getSize()));
    UserPresetLibrary library (temporaryFolder.folder);
    EXPECT (library.save ("Old Ratchet", oldState).isEmpty());

    MidiForgeProcessor processor;
    processor.setUserPresetFolder (temporaryFolder.folder);
    EXPECT (processor.loadUserPreset ("Old Ratchet"));
    EXPECT (hostTextOf (processor, 0, 4) == "40");
    EXPECT (hostTextOf (processor, 0, 6) == "100%");
}

//==============================================================================
// Module browser, module info and the recently used modules

TEST_CASE ("recent modules: newest first, no duplicates, no Empty, at most five, saved for the next session")
{
    TemporaryPresetFolder temporaryFolder;
    const auto settingsFile = temporaryFolder.folder.getChildFile ("Settings.xml");
    {
        RecentModuleList recent (settingsFile);
        EXPECT (recent.getModuleTypes().empty());
        recent.moduleWasPicked (3);
        recent.moduleWasPicked (5);
        recent.moduleWasPicked (3);
        recent.moduleWasPicked (0);              // Empty is not remembered
        recent.moduleWasPicked (numModuleTypes); // nor unknown modules
        EXPECT (recent.getModuleTypes() == std::vector<int> ({ 3, 5 }));
        for (int type : { 7, 8, 9, 10, 11 })
            recent.moduleWasPicked (type);
        EXPECT (recent.getModuleTypes() == std::vector<int> ({ 11, 10, 9, 8, 7 }));
    }
    RecentModuleList reloaded (settingsFile);
    EXPECT (reloaded.getModuleTypes() == std::vector<int> ({ 11, 10, 9, 8, 7 }));

    // Two instances share the file: a pick in one is seen by the next pick in the other.
    RecentModuleList otherInstance (settingsFile);
    reloaded.moduleWasPicked (2);
    otherInstance.moduleWasPicked (4);
    EXPECT (otherInstance.getModuleTypes() == std::vector<int> ({ 4, 2, 11, 10, 9 }));
}

TEST_CASE ("recent modules: hand-edited, damaged and oversized settings files are handled safely")
{
    TemporaryPresetFolder temporaryFolder;
    temporaryFolder.folder.createDirectory();
    const auto settingsFile = temporaryFolder.folder.getChildFile ("Settings.xml");

    settingsFile.replaceWithText ("<MidiForgeSettings><Other keep=\"yes\"/><RecentModules>"
                                  "<Module type=\"5\"/><Module type=\"-3\"/><Module type=\"5\"/>"
                                  "<Module type=\"999\"/><Module type=\"abc\"/><Module type=\"6\"/>"
                                  "</RecentModules></MidiForgeSettings>");
    RecentModuleList recent (settingsFile);
    EXPECT (recent.getModuleTypes() == std::vector<int> ({ 5, 6 }));
    recent.moduleWasPicked (7);
    const auto saved = juce::parseXML (settingsFile);
    EXPECT (saved != nullptr && saved->getChildByName ("Other") != nullptr); // other settings are kept

    settingsFile.replaceWithText ("<NotOurs><RecentModules><Module type=\"5\"/></RecentModules></NotOurs>");
    EXPECT (RecentModuleList (settingsFile).getModuleTypes().empty());
    settingsFile.replaceWithText ("not xml at all <<<");
    EXPECT (RecentModuleList (settingsFile).getModuleTypes().empty());
    settingsFile.replaceWithText ("<MidiForgeSettings><RecentModules><Module type=\"5\"/></RecentModules>" +
                                  juce::String::repeatedString (" ", (int) RecentModuleList::maximumFileSizeInBytes) +
                                  "</MidiForgeSettings>");
    EXPECT (RecentModuleList (settingsFile).getModuleTypes().empty());
}

TEST_CASE ("module browser: lists Empty, the recent modules and all categories; highlights the slot's module")
{
    int chosenType = -1;
    ModuleBrowser browser ((int) ModuleType::Chord, { (int) ModuleType::Repeater, (int) ModuleType::Chord },
                           [&] (int type) { chosenType = type; });
    browser.setBounds (0, 0, ModuleBrowser::preferredWidth, ModuleBrowser::preferredHeight);

    const auto listed = browser.getListedModuleTypes();
    EXPECT ((int) listed.size() == numModuleTypes + 2); // every module once, plus the two recent ones
    EXPECT (listed[0] == 0 && listed[1] == (int) ModuleType::Repeater && listed[2] == (int) ModuleType::Chord);
    EXPECT (std::set<int> (listed.begin(), listed.end()).size() == (size_t) numModuleTypes);
    EXPECT (browser.getHighlightedModuleType() == (int) ModuleType::Chord);

    // The arrow keys walk through the modules, skipping headings, and stop at the ends.
    browser.moveHighlight (-100);
    EXPECT (browser.getHighlightedModuleType() == 0);
    browser.moveHighlight (1);
    EXPECT (browser.getHighlightedModuleType() == (int) ModuleType::Repeater);
    browser.moveHighlight (2); // over the "Filters" heading
    EXPECT (browser.getHighlightedModuleType() == (int) ModuleType::NoteRange);
    browser.moveHighlight (100);
    EXPECT (browser.getHighlightedModuleType() == listed.back());

    browser.chooseHighlightedModule();
    EXPECT (chosenType == listed.back());
}

TEST_CASE ("module browser: the search filters the list and highlights the best name match")
{
    int chosenType = -1;
    ModuleBrowser browser ((int) ModuleType::Empty, { (int) ModuleType::Transpose },
                           [&] (int type) { chosenType = type; });
    browser.setBounds (0, 0, ModuleBrowser::preferredWidth, ModuleBrowser::preferredHeight);

    browser.setSearchText ("RATCH");
    const auto ratchetResults = browser.getListedModuleTypes();
    EXPECT (std::count (ratchetResults.begin(), ratchetResults.end(), (int) ModuleType::Repeater) == 1);
    EXPECT (std::count (ratchetResults.begin(), ratchetResults.end(), (int) ModuleType::Chord) == 0);
    EXPECT (browser.getHighlightedModuleType() == (int) ModuleType::Repeater);

    // "chord" finds the Arpeggiator (it mentions chords) before the Chord, but highlights the Chord.
    browser.setSearchText ("chord");
    const auto chordResults = browser.getListedModuleTypes();
    EXPECT (std::find (chordResults.begin(), chordResults.end(), (int) ModuleType::Arpeggiator) != chordResults.end());
    EXPECT (browser.getHighlightedModuleType() == (int) ModuleType::Chord);
    browser.chooseHighlightedModule();
    EXPECT (chosenType == (int) ModuleType::Chord);

    // Recently used modules are hidden while searching, so no module is listed twice.
    browser.setSearchText ("transpose");
    const auto transposeResults = browser.getListedModuleTypes();
    EXPECT (std::count (transposeResults.begin(), transposeResults.end(), (int) ModuleType::Transpose) == 1);
    EXPECT (browser.getHighlightedModuleType() == (int) ModuleType::Transpose);

    chosenType = -1;
    browser.setSearchText ("no such module");
    EXPECT (browser.getListedModuleTypes().empty());
    EXPECT (browser.getHighlightedModuleType() == -1);
    browser.moveHighlight (1);
    browser.chooseHighlightedModule();
    EXPECT (chosenType == -1);

    browser.setSearchText ("");
    EXPECT ((int) browser.getListedModuleTypes().size() == numModuleTypes + 1);
}

TEST_CASE ("module info: shows the details and one line per knob, with page hints for page 2")
{
    ModuleInfoView info;
    info.showModule ((int) ModuleType::Repeater);
    const auto text = info.getPlainText();
    const auto& repeater = getModuleDescription (ModuleType::Repeater);
    EXPECT (text.contains (repeater.details));
    EXPECT (text.contains (juce::String ("A  Rate")));
    EXPECT (text.contains (juce::String ("E  Accent Boost (page 2)")));
    EXPECT (text.contains (repeater.parameters[5].helpText));
    EXPECT (text.contains (juce::String ("G  Chance (page 2)")));
    EXPECT (text.contains (juce::String ("H  Grid (page 2)")));

    info.showModule ((int) ModuleType::Arpeggiator); // unused knobs (F … H) are not listed
    EXPECT (info.getPlainText().contains (juce::String ("E  Grid (page 2)")));
    EXPECT (! info.getPlainText().contains ("F  "));
    info.showModule ((int) ModuleType::Repeater);
    EXPECT (info.getHeightForWidth (380) > info.getHeightForWidth (2000));

    info.showModule (-5); // out of range shows the Empty slot
    EXPECT (info.getPlainText().startsWith ("Empty slot"));
    EXPECT (! info.getPlainText().contains ("KNOBS"));
}

TEST_CASE ("module browser: the module menu opens the browser every time, not just once")
{
    MidiForgeProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    std::vector<ModuleMenu*> moduleMenus;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& component)
    {
        if (auto* menu = dynamic_cast<ModuleMenu*> (&component))
            moduleMenus.push_back (menu);
        for (auto* child : component.getChildren())
            collect (*child);
    };
    collect (*editor);
    EXPECT ((int) moduleMenus.size() == numSlots);
    if (moduleMenus.empty())
        return;

    auto& menu = *moduleMenus[2];
    int timesOpened = 0;
    menu.onOpenBrowser = [&timesOpened] { ++timesOpened; };
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        // A click or Return marks the menu as open and then calls showPopup() (asynchronously).
        EXPECT (menu.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
        EXPECT (menu.isPopupActive());
        menu.showPopup();
        EXPECT (! menu.isPopupActive()); // ready for the next click
    }
    EXPECT (timesOpened == 3);
}

TEST_CASE ("module info: the knobs of every module get their help text as tooltip")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Repeater);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    int numberOfKnobsWithHelp = 0;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& component)
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (&component))
            if (slider->getTooltip() ==
                juce::String (getModuleDescription (ModuleType::Repeater).parameters[0].helpText))
                ++numberOfKnobsWithHelp;
        for (auto* child : component.getChildren())
            visit (*child);
    };
    visit (*editor);
    EXPECT (numberOfKnobsWithHelp == 1);
}

//==============================================================================
// Pitch Slide

TEST_CASE ("pitch slide: the host shows its knobs, and its pitch bends reach the plug-in's output")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::PitchSlide);
    EXPECT (hostTextOf (processor, 0, 0) == "-2 st");
    EXPECT (hostTextOf (processor, 0, 1) == "Slide In");
    EXPECT (hostTextOf (processor, 0, 2) == "1/16");
    EXPECT (hostTextOf (processor, 0, 5) == "2 st");
    EXPECT (hostTextOf (processor, 0, 6) == "Off");

    processor.prepareToPlay (testSampleRate, testBlockSize);
    juce::MidiBuffer input;
    input.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
    const auto messages = toMessages (processBlock (processor, input));
    EXPECT (messages.size() >= 2);
    if (messages.size() >= 2)
    {
        EXPECT (messages[0].isPitchWheel() && messages[0].getPitchWheelValue() == 0); // two semitones down
        EXPECT (messages[1].isNoteOn() && messages[1].getNoteNumber() == 60);
    }
}

//==============================================================================
// Removing modules

namespace
{
/** Sets a route (plain values: target number, amount -100..100). */
void setRoute (MidiForgeProcessor& processor, int routeIndex, int targetIndex, float amountPercent = 50.0f)
{
    const auto setPlain = [&processor] (const juce::String& id, float plainValue)
    {
        auto* parameter = processor.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    setPlain (ParameterIds::routeTarget (routeIndex), (float) targetIndex);
    setPlain (ParameterIds::routeAmount (routeIndex), amountPercent);
}

int routeTargetOf (MidiForgeProcessor& processor, int routeIndex)
{
    return (int) std::lround (
        processor.parameters.getRawParameterValue (ParameterIds::routeTarget (routeIndex))->load());
}

/** Slot 1 Chord, slot 2 Repeater (switched off, on page 2, Swing changed), slot 3 Transpose; routes on all three. */
void setUpThreeModules (MidiForgeProcessor& processor)
{
    processor.loadModule (0, (int) ModuleType::Chord);
    processor.loadModule (1, (int) ModuleType::Repeater);
    processor.loadModule (2, (int) ModuleType::Transpose);
    processor.parameters.getParameter (ParameterIds::slotEnabled (1))->setValueNotifyingHost (0.0f);
    processor.parameters.getParameter (ParameterIds::moduleParameter (1, 2))->setValueNotifyingHost (0.8f);
    processor.setShownPage (1, 1);
    setRoute (processor, 0, modulationTargetIndex (0, 1)); // Chord B
    setRoute (processor, 1, modulationTargetIndex (1, 5)); // Repeater F (page 2)
    setRoute (processor, 2, modulationTargetIndex (2, 0)); // Transpose A
    setRoute (processor, 3, modulationTargetIndex (2, 4)); // Transpose E (unused knob, but a valid target)
}
} // namespace

TEST_CASE ("remove module: the modules below move up, the empty slot goes to the end, routes follow")
{
    MidiForgeProcessor processor;
    setUpThreeModules (processor);
    const float transposeValue = processor.getParameterValue (2, 0);

    processor.removeModule (1);
    EXPECT (processor.getModuleType (0) == (int) ModuleType::Chord);
    EXPECT (processor.getModuleType (1) == (int) ModuleType::Transpose);
    EXPECT (std::abs (processor.getParameterValue (1, 0) - transposeValue) < 1.0e-6f);
    for (int slotIndex = 2; slotIndex < numSlots; ++slotIndex)
    {
        EXPECT (processor.getModuleType (slotIndex) == 0);
        EXPECT (processor.isSlotEnabled (slotIndex));
    }

    EXPECT (routeTargetOf (processor, 0) == modulationTargetIndex (0, 1)); // above: unchanged
    EXPECT (routeTargetOf (processor, 1) == 0);                            // the removed module: None
    EXPECT (routeTargetOf (processor, 2) == modulationTargetIndex (1, 0)); // moved up with Transpose
    EXPECT (routeTargetOf (processor, 3) == modulationTargetIndex (1, 4)); // page-2 numbers move too

    EXPECT (processor.getLastRemovedModuleName() == "Repeater");
    EXPECT (processor.getLastRemovedSlotNumber() == 2);
    EXPECT (processor.canUndoModuleRemoval());

    // Removing an empty slot does nothing.
    const int removals = processor.getModuleRemovalCount();
    processor.removeModule (5);
    processor.removeModule (-1);
    EXPECT (processor.getModuleRemovalCount() == removals);
}

TEST_CASE ("remove module: undo puts it back with knobs, switch, page and routes")
{
    MidiForgeProcessor processor;
    setUpThreeModules (processor);
    juce::MemoryBlock before;
    processor.getStateInformation (before);

    processor.removeModule (1);
    processor.undoModuleRemoval();
    EXPECT (! processor.canUndoModuleRemoval()); // only once

    EXPECT (processor.getModuleType (1) == (int) ModuleType::Repeater);
    EXPECT (! processor.isSlotEnabled (1));
    EXPECT (std::abs (processor.getParameterValue (1, 2) - 0.8f) < 1.0e-6f);
    EXPECT (processor.getShownPage (1) == 1);
    EXPECT (processor.getModuleType (2) == (int) ModuleType::Transpose);
    EXPECT (routeTargetOf (processor, 0) == modulationTargetIndex (0, 1));
    EXPECT (routeTargetOf (processor, 1) == modulationTargetIndex (1, 5));
    EXPECT (routeTargetOf (processor, 2) == modulationTargetIndex (2, 0));
    EXPECT (routeTargetOf (processor, 3) == modulationTargetIndex (2, 4));

    // Everything is exactly as before.
    juce::MemoryBlock after;
    processor.getStateInformation (after);
    EXPECT (before == after);
}

TEST_CASE ("remove module: undo is no longer possible once the last slot is used")
{
    MidiForgeProcessor processor;
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
        processor.loadModule (slotIndex, (int) ModuleType::Transpose);
    processor.removeModule (7); // the last slot itself
    EXPECT (processor.getModuleType (7) == 0 && processor.canUndoModuleRemoval());
    processor.loadModule (7, (int) ModuleType::Chord);
    EXPECT (! processor.canUndoModuleRemoval());
    processor.undoModuleRemoval(); // ignored
    EXPECT (processor.getModuleType (7) == (int) ModuleType::Chord);
}

TEST_CASE ("remove module: moving modules up and down takes their routes along")
{
    MidiForgeProcessor processor;
    setUpThreeModules (processor);
    processor.swapSlots (1, 2);
    EXPECT (routeTargetOf (processor, 1) == modulationTargetIndex (2, 5)); // Repeater F, now in slot 3
    EXPECT (routeTargetOf (processor, 2) == modulationTargetIndex (1, 0)); // Transpose A, now in slot 2
    EXPECT (routeTargetOf (processor, 0) == modulationTargetIndex (0, 1));
}

TEST_CASE ("remove module: the editor shows the undo strip, and Undo hides it")
{
    MidiForgeProcessor processor;
    setUpThreeModules (processor);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    EXPECT (forgeEditor != nullptr);
    if (forgeEditor == nullptr)
        return;

    EXPECT (! forgeEditor->isUndoStripVisible());
    processor.removeModule (0);
    forgeEditor->refreshFromProcessor();
    EXPECT (forgeEditor->isUndoStripVisible());

    // Using the last slot ends the chance to undo, and the strip goes away.
    processor.loadModule (7, (int) ModuleType::Chord);
    forgeEditor->refreshFromProcessor();
    EXPECT (! forgeEditor->isUndoStripVisible());
}

//==============================================================================
// Note Dispatcher

TEST_CASE ("note dispatcher: the host shows its knobs, and notes leave on the dispatched channels")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::NoteDispatcher);
    EXPECT (hostTextOf (processor, 0, 0) == "Round Robin");
    EXPECT (hostTextOf (processor, 0, 1) == "Single Notes");
    EXPECT (hostTextOf (processor, 0, 3) == "4");
    EXPECT (hostTextOf (processor, 0, 4) == "All");
    EXPECT (hostTextOf (processor, 0, 5) == "30 ms");

    processor.prepareToPlay (testSampleRate, testBlockSize);
    juce::MidiBuffer input;
    input.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    input.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 1);
    input.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 2);
    std::vector<int> channels;
    for (const auto& message : toMessages (processBlock (processor, input)))
        if (message.isNoteOn())
            channels.push_back (message.getChannel());
    EXPECT (channels == std::vector<int> ({ 1, 2, 3 }));
}

//==============================================================================
// Modulated on/off switches

TEST_CASE ("switch targets: named per slot, listed for the host, and they follow moved modules")
{
    MidiForgeProcessor processor;
    processor.loadModule (2, (int) ModuleType::Transpose);
    const int switchOfSlot3 = modulationTargetIndex (2, slotSwitchParameterIndex);
    EXPECT (processor.getModulationTargetName (switchOfSlot3) == juce::String::fromUTF8 ("Slot 3 \xc2\xb7 On/Off"));

    auto* target = processor.parameters.getParameter (ParameterIds::routeTarget (0));
    target->setValueNotifyingHost (target->getValueForText ("Slot 3 On/Off"));
    EXPECT (routeTargetOf (processor, 0) == switchOfSlot3);
    EXPECT (target->getCurrentValueAsText() == juce::String::fromUTF8 ("Slot 3 \xc2\xb7 On/Off"));

    // Projects from before keep their targets: the knob targets keep their numbers.
    EXPECT (processor.getModulationTargetName (modulationTargetIndex (2, 0)).endsWith ("Semitones"));

    setRoute (processor, 0, switchOfSlot3);
    processor.swapSlots (2, 3);
    EXPECT (routeTargetOf (processor, 0) == modulationTargetIndex (3, slotSwitchParameterIndex));
    EXPECT (processor.isSlotSwitchModulated (3) && ! processor.isSlotSwitchModulated (2));
}

TEST_CASE ("switch targets: a route switches a slot off, and the editor's readout shows it")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Transpose);
    // A square LFO at 100 % on slot 1's switch, inverted (negative amount): off during the high half.
    auto* lfoWave = processor.parameters.getParameter (ParameterIds::lfoWaveform (0));
    lfoWave->setValueNotifyingHost (lfoWave->convertTo0to1 ((float) LfoWaveform::Square));
    setRoute (processor, 0, modulationTargetIndex (0, slotSwitchParameterIndex), -100.0f);
    processor.prepareToPlay (testSampleRate, testBlockSize);

    bool sawOff = false, sawOn = false;
    for (int block = 0; block < 400; ++block)
    {
        processBlock (processor, {});
        (processor.isSlotEnabledAfterModulation (0) ? sawOn : sawOff) = true;
    }
    EXPECT (sawOn && sawOff);
    EXPECT (processor.isSlotEnabled (0)); // the switch itself stays as the user set it
}

//==============================================================================
// Step modulators

TEST_CASE ("step modulators: host parameters map to the engine settings; Steps are new route sources")
{
    MidiForgeProcessor processor;
    const auto setPlain = [&processor] (const juce::String& id, float plainValue)
    {
        auto* parameter = processor.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    setPlain (ParameterIds::stepCount (1), 7);
    setPlain (ParameterIds::stepRate (1), 5); // 1/8
    setPlain (ParameterIds::stepGlide (1), 25);
    setPlain (ParameterIds::stepRestartOnKey (1), 1);
    setPlain (ParameterIds::stepValue (1, 2), -40);

    const auto settings = processor.getStepModulatorSettings (1);
    EXPECT (settings.numSteps == 7);
    EXPECT (juce::String (stepDivisions[settings.divisionIndex].name) == "1/8");
    EXPECT (std::abs (settings.glide - 0.25) < 1.0e-6);
    EXPECT (settings.restartsOnFirstKey);
    EXPECT (std::abs (settings.stepValues[2] - -0.4f) < 1.0e-6f);
    EXPECT (processor.parameters.getParameter (ParameterIds::stepGlide (0))->getCurrentValueAsText() == "Off");

    // Old projects keep their sources (LFO 1-4 = 0-3, Random 1-4 = 4-7); Steps 1-4 are 8-11.
    auto* source = processor.parameters.getParameter (ParameterIds::routeSource (0));
    source->setValueNotifyingHost (source->getValueForText ("Steps 3"));
    EXPECT ((int) std::lround (processor.parameters.getRawParameterValue (ParameterIds::routeSource (0))->load()) ==
            firstStepModulatorSource + 2);

    setRoute (processor, 0, modulationTargetIndex (0, 0));
    EXPECT (processor.isSourceInUse (firstStepModulatorSource + 2));
    EXPECT (! processor.isSourceInUse (0));
}

TEST_CASE ("step modulators: their steps drive a knob and show the current step")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Transpose);
    const auto setPlain = [&processor] (const juce::String& id, float plainValue)
    {
        auto* parameter = processor.parameters.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
    };
    setPlain (ParameterIds::stepCount (0), 4);
    for (int step = 0; step < 4; ++step)
        setPlain (ParameterIds::stepValue (0, step), step % 2 == 0 ? 100.0f : -100.0f);
    setPlain (ParameterIds::routeSource (0), (float) firstStepModulatorSource);
    setRoute (processor, 0, modulationTargetIndex (0, 0), 50.0f);
    processor.prepareToPlay (testSampleRate, testBlockSize);

    std::set<int> stepsSeen;
    bool sawHigh = false, sawLow = false;
    for (int block = 0; block < 200; ++block)
    {
        processBlock (processor, {});
        stepsSeen.insert (processor.getCurrentStep (0));
        const float knob = processor.getModulatedParameterValue (0, 0);
        sawHigh = sawHigh || knob > 0.9f;
        sawLow = sawLow || knob < 0.1f;
    }
    EXPECT (stepsSeen == std::set<int> ({ 0, 1, 2, 3 }));
    EXPECT (sawHigh && sawLow);
}

TEST_CASE ("step modulators: the Steps sub-tab shows the panels, and drawing sets the step values")
{
    MidiForgeProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    EXPECT (forgeEditor != nullptr);
    if (forgeEditor == nullptr)
        return;
    forgeEditor->showPage (MidiForgeEditor::Page::modulation);
    auto& page = forgeEditor->getModulationPage();
    EXPECT (page.getShownSourcePage() == ModulationPage::SourcePage::lfos);
    page.showSourcePage (ModulationPage::SourcePage::steps);
    auto& stepEditor = page.getStepModulatorPanel (1).editor;
    EXPECT (page.getStepModulatorPanel (1).isVisible());

    // 16 steps across the bar area: the 4th step is under x at 3.5/16 of it; its top is +100, its middle 0.
    const auto bars = stepEditor.barArea();
    EXPECT (stepEditor.stepAt (bars.getX() + bars.getWidth() * 3.5f / 16.0f) == 3);
    EXPECT (std::abs (stepEditor.valueAt (bars.getY(), false) - 100.0f) < 0.5f);
    EXPECT (std::abs (stepEditor.valueAt (bars.getCentreY(), false)) < 1.0f);
    EXPECT (std::abs (std::fmod (stepEditor.valueAt (bars.getY() + 13.0f, true), 25.0f)) < 1.0e-3f); // Shift snaps
    EXPECT (stepEditor.tieStripArea().getY() > bars.getBottom());

    stepEditor.setStepValue (3, -60.0f);
    EXPECT (std::abs (processor.getStepModulatorSettings (1).stepValues[3] - -0.6f) < 1.0e-6f);

    // Ties: step 5 tied to step 4; drawing on step 5 changes step 4's value; step 1 cannot be tied.
    stepEditor.toggleTie (4);
    stepEditor.toggleTie (0);
    EXPECT (processor.getStepModulatorSettings (1).isTied[4] && ! processor.getStepModulatorSettings (1).isTied[0]);
    stepEditor.setStepValue (4, 80.0f);
    EXPECT (std::abs (processor.getStepModulatorSettings (1).stepValues[3] - 0.8f) < 1.0e-6f);
    EXPECT (std::abs (processor.getStepModulatorSettings (1).stepValues[4]) < 1.0e-6f);
    stepEditor.toggleTie (4);
    EXPECT (! processor.getStepModulatorSettings (1).isTied[4]);
    EXPECT (processor.parameters.getParameter (ParameterIds::stepTie (1, 4)) != nullptr);
}

//==============================================================================
// Mutate and trigger buttons

TEST_CASE ("mutate: the host shows its knobs; Reset is a button that is on while held")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Mutate);
    EXPECT (hostTextOf (processor, 0, 0) == "25%");
    EXPECT (hostTextOf (processor, 0, 2) == "Both");
    EXPECT (hostTextOf (processor, 0, 5) == "Minor");
    EXPECT (hostTextOf (processor, 0, 6) == "4 Bars");
    EXPECT (hostTextOf (processor, 0, 7) == "Off");
    processor.setShownPage (0, 1);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    if (forgeEditor == nullptr)
        return;
    forgeEditor->refreshFromProcessor();

    // Find the Reset button of slot 1 (page 2, fourth knob).
    juce::TextButton* resetButton = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& component)
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (&component))
            if (button->getButtonText() == "Reset" && button->isVisible() && resetButton == nullptr)
                resetButton = button;
        for (auto* child : component.getChildren())
            find (*child);
    };
    find (*editor);
    EXPECT (resetButton != nullptr);
    if (resetButton == nullptr)
        return;
    resetButton->setState (juce::Button::buttonDown);
    EXPECT (hostTextOf (processor, 0, 7) == "On");
    resetButton->setState (juce::Button::buttonNormal);
    EXPECT (hostTextOf (processor, 0, 7) == "Off");
}

//==============================================================================
// Live values on modulated knobs

TEST_CASE ("live values: a modulated knob shows the value the module uses now, in the modulation colour")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Transpose);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    if (forgeEditor == nullptr)
        return;

    std::vector<ParameterKnob*> knobs;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& component)
    {
        if (auto* knob = dynamic_cast<ParameterKnob*> (&component))
            knobs.push_back (knob);
        for (auto* child : component.getChildren())
            collect (*child);
    };
    collect (*editor);
    EXPECT (knobs.size() >= 1);
    if (knobs.empty())
        return;
    auto& semitones = *knobs[0]; // slot 1, knob A

    // A square LFO at 50 % on Semitones: the module uses +24 or 0 (the knob itself stays at 0).
    auto* lfoWave = processor.parameters.getParameter (ParameterIds::lfoWaveform (0));
    lfoWave->setValueNotifyingHost (lfoWave->convertTo0to1 ((float) LfoWaveform::Square));
    setRoute (processor, 0, modulationTargetIndex (0, 0), 50.0f);

    // Without the host processing, the knob shows the user's setting.
    forgeEditor->refreshFromProcessor();
    EXPECT (semitones.valueLabel.getText() == "0 st");
    EXPECT (semitones.valueLabel.findColour (juce::Label::textColourId) == Theme::Colours::primaryText);

    processor.prepareToPlay (testSampleRate, testBlockSize);
    processBlock (processor, {});
    forgeEditor->refreshFromProcessor();
    const auto expected = juce::String (formatParameterValue (
        getModuleDescription (ModuleType::Transpose).parameters[0], processor.getModulatedParameterValue (0, 0)));
    EXPECT (semitones.valueLabel.getText() == expected);
    EXPECT (expected == "+24 st" || expected == "0 st");
    EXPECT (semitones.valueLabel.findColour (juce::Label::textColourId) == Theme::Colours::modulation);
}

//==============================================================================
// Groove

TEST_CASE ("groove: the host shows its knobs, and Early is reported to the host as latency")
{
    MidiForgeProcessor processor;
    processor.loadModule (2, (int) ModuleType::Groove);
    EXPECT (hostTextOf (processor, 2, 0) == "Swing");
    EXPECT (hostTextOf (processor, 2, 1) == "100%");
    EXPECT (hostTextOf (processor, 2, 2) == "1/16");
    EXPECT (hostTextOf (processor, 2, 4) == "Off");

    processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processBlock (processor, {});
    EXPECT (processor.getLatencySamples() == 0);

    auto* early = processor.parameters.getParameter (ParameterIds::moduleParameter (2, 4));
    early->setValueNotifyingHost (early->getValueForText ("20 ms"));
    EXPECT (hostTextOf (processor, 2, 4) == "20 ms");
    processBlock (processor, {});
    EXPECT (processor.getLatencySamples() == 960); // 20 ms at 48 kHz

    processor.parameters.getParameter (ParameterIds::slotEnabled (2))->setValueNotifyingHost (0.0f);
    processBlock (processor, {});
    EXPECT (processor.getLatencySamples() == 0);
}

//==============================================================================
// Strum and Ghost Notes

TEST_CASE ("strum and ghost notes: host texts, and Strum's window adds to the reported latency")
{
    MidiForgeProcessor processor;
    processor.loadModule (0, (int) ModuleType::Strum);
    processor.loadModule (1, (int) ModuleType::GhostNotes);
    processor.loadModule (2, (int) ModuleType::Groove);
    EXPECT (hostTextOf (processor, 0, 0) == "15 ms");
    EXPECT (hostTextOf (processor, 0, 1) == "Up");
    EXPECT (hostTextOf (processor, 0, 3) == "10 ms");
    EXPECT (hostTextOf (processor, 1, 1) == "After");
    EXPECT (hostTextOf (processor, 1, 2) == "1/16");
    EXPECT (hostTextOf (processor, 1, 4) == "30%");

    auto* early = processor.parameters.getParameter (ParameterIds::moduleParameter (2, 4));
    early->setValueNotifyingHost (early->getValueForText ("20 ms"));
    processor.setRateAndBufferSizeDetails (testSampleRate, testBlockSize);
    processor.prepareToPlay (testSampleRate, testBlockSize);
    processBlock (processor, {});
    EXPECT (processor.getLatencySamples() == 1440); // 10 ms Strum window + 20 ms Groove Early at 48 kHz
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::String nameFilter;
    juce::File snapshotDirectory;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const juce::String argument (argv[argumentIndex]);
        if (argument == "--snapshot-dir" && argumentIndex + 1 < argc)
            snapshotDirectory = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++argumentIndex]);
        else
            nameFilter = argument;
    }

    const int result = testing::runAllTests (nameFilter.toStdString());

    if (snapshotDirectory != juce::File())
    {
        snapshotDirectory.createDirectory();
        MidiForgeProcessor processor;
        prepareProcessorForSnapshots (processor);
        writeEditorSnapshots (processor, snapshotDirectory);
        writeStepsSnapshot (snapshotDirectory);
        writePopupSnapshots (snapshotDirectory);
    }
    return result;
}

TEST_CASE ("about: shows version, copyright, licence, the missing warranty and the source code link")
{
    const auto text = getAboutText();
    EXPECT (text.contains (juce::String ("MIDI Forge ") + JucePlugin_VersionString));
    EXPECT (text.contains ("Copyright (C) 2026 Udo Gerhards"));
    EXPECT (text.contains ("GNU Affero General Public License, version 3"));
    EXPECT (text.contains ("NO WARRANTY"));
    EXPECT (text.contains ("https://github.com/ugerhard-git/midi-forge"));
}

TEST_CASE ("modulating modulation: targets have names, and the editor sees the modulated values")
{
    MidiForgeProcessor processor;
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    EXPECT (processor.getModulationTargetName (routeAmountTarget (2)) == "Route 3" + dot + "Amount");
    EXPECT (processor.getModulationTargetName (lfoSettingTarget (1, LfoSettingTarget::width)) ==
            "LFO 2" + dot + "Width");
    EXPECT (processor.getModulationTargetName (stepSettingTarget (3, StepSettingTarget::glide)) ==
            "Steps 4" + dot + "Glide");
    auto* target = processor.parameters.getParameter (ParameterIds::routeTarget (11));
    EXPECT (target->getText (target->convertTo0to1 ((float) lfoSettingTarget (0, LfoSettingTarget::rate)), 0) ==
            "LFO 1" + dot + "Rate");
    EXPECT (std::abs (target->getValueForText ("Steps 2 Rate") -
                      target->convertTo0to1 ((float) stepSettingTarget (1, StepSettingTarget::rate))) < 1.0e-6f);

    // Route 2 (LFO 1, square, 50 %) moves route 1's amount; route 1 sits at 0 % but counts as modulated.
    processor.parameters.getParameter (ParameterIds::routeTarget (0))
        ->setValueNotifyingHost (target->convertTo0to1 ((float) modulationTargetIndex (0, 0)));
    processor.parameters.getParameter (ParameterIds::routeAmount (0))->setValueNotifyingHost (0.5f); // 0 %
    processor.parameters.getParameter (ParameterIds::routeTarget (1))
        ->setValueNotifyingHost (target->convertTo0to1 ((float) routeAmountTarget (0)));
    EXPECT (processor.isTargetModulated (routeAmountTarget (0)));
    EXPECT (processor.isParameterModulated (0, 0));
    EXPECT (! processor.isTargetModulated (lfoSettingTarget (0, LfoSettingTarget::rate)));

    processor.setRateAndBufferSizeDetails (48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> audio (0, 512);
    juce::MidiBuffer midi;
    processor.processBlock (audio, midi);
    const float amount = processor.getModulatedSettingValue (routeAmountTarget (0));
    EXPECT (std::abs (amount) <= 50.5f);
    EXPECT (processor.getModulatedSettingText (routeAmountTarget (0)).endsWith ("%"));
    EXPECT (processor.getModulatedSettingText (lfoSettingTarget (0, LfoSettingTarget::rate)) == "1 Bar");
    EXPECT (processor.getModulatedSettingText (modulationTargetIndex (0, 0)).isEmpty());
}

TEST_CASE ("modulation page: the routes are on two pages of six")
{
    MidiForgeProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto* forgeEditor = dynamic_cast<MidiForgeEditor*> (editor.get());
    EXPECT (forgeEditor != nullptr);
    if (forgeEditor == nullptr)
        return;
    forgeEditor->showPage (MidiForgeEditor::Page::modulation);
    auto& page = forgeEditor->getModulationPage();
    EXPECT (page.getShownRoutePage() == 0);

    // Count the visible route rows and check which are shown.
    const auto visibleRouteRows = [&page]
    {
        std::vector<int> rows;
        int routeIndex = 0;
        for (auto* child : page.getChildren())
            if (dynamic_cast<RouteRow*> (child) != nullptr)
            {
                if (child->isVisible())
                    rows.push_back (routeIndex);
                ++routeIndex;
            }
        return rows;
    };
    EXPECT ((visibleRouteRows() == std::vector<int> { 0, 1, 2, 3, 4, 5 }));
    page.showRoutePage (1);
    EXPECT (page.getShownRoutePage() == 1);
    EXPECT ((visibleRouteRows() == std::vector<int> { 6, 7, 8, 9, 10, 11 }));
}
