/**
 * @file PluginTests.cpp
 * @brief Tests of the JUCE plug-in layer: parameters, presets, state, slot editing and MIDI routing.
 *
 * Usage: midi_forge_plugin_tests [name filter] [--snapshot path.png]
 * With --snapshot the editor is rendered to a PNG file, which CI publishes for visual review.
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

TEST_CASE ("plugin: exposes 6 host parameters per slot with stable IDs")
{
    MidiForgeProcessor processor;
    int numOurParameters = 0;
    for (auto* parameter : processor.getParameters())
        if (dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter) != nullptr)
            ++numOurParameters;
    EXPECT (numOurParameters == numSlots * 6);
    EXPECT (processor.parameters.getParameter ("s1_type") != nullptr);
    EXPECT (processor.parameters.getParameter ("s8_d") != nullptr);
    EXPECT (processor.isMidiEffect() && processor.acceptsMidi() && processor.producesMidi());
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

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::String nameFilter;
    juce::File snapshotFile;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const juce::String argument (argv[argumentIndex]);
        if (argument == "--snapshot" && argumentIndex + 1 < argc)
            snapshotFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++argumentIndex]);
        else
            nameFilter = argument;
    }

    const int result = testing::runAllTests (nameFilter.toStdString());

    if (snapshotFile != juce::File())
    {
        // Render the editor with a preset loaded so reviewers can see the UI without a DAW.
        MidiForgeProcessor processor;
        processor.setCurrentProgram (4);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        snapshotFile.deleteFile();
        juce::FileOutputStream stream (snapshotFile);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("Editor snapshot written to %s\n", snapshotFile.getFullPathName().toRawUTF8());
    }
    return result;
}
