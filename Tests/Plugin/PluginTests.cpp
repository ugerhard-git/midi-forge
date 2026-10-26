/**
 * @file PluginTests.cpp
 * @brief Tests of the JUCE plug-in layer: parameters, presets, state, slot editing and MIDI routing.
 *
 * Usage: midi_forge_plugin_tests [name filter] [--snapshot-dir folder]
 * With --snapshot-dir both editor pages are rendered to editor.png and modulation.png in that folder,
 * which CI publishes for visual review and which are used in the manual.
 */
#include "../TestFramework.h"

#include "Plugin/FactoryPresets.h"
#include "Plugin/ParameterIds.h"
#include "Plugin/PluginProcessor.h"
#include "UI/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

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
    constexpr int parametersPerSlot = 6, parametersPerLfo = 7, parametersPerRoute = 3;
    EXPECT (numOurParameters ==
            numSlots * parametersPerSlot + numLfos * parametersPerLfo + numModulationRoutes * parametersPerRoute);

    for (const char* id : { "s1_type", "s8_d", "l1_wave", "l4_restart", "r1_source", "r8_amount" })
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
                const float value = slot.plainValues[parameterIndex];
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

/** Loads a preset with LFOs and runs the engine for a moment, so the snapshots show modulation. */
void prepareProcessorForSnapshots (MidiForgeProcessor& processor)
{
    const auto& presets = getFactoryPresets();
    for (int index = 0; index < (int) presets.size(); ++index)
        if (juce::String (presets[(size_t) index].name) == "Breathing Arp")
            processor.setCurrentProgram (index);

    processor.prepareToPlay (testSampleRate, testBlockSize);
    for (int block = 0; block < 150; ++block)
        processBlock (processor, {});
}
} // namespace

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
    }
    return result;
}
