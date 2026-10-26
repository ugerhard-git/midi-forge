/**
 * @file PluginProcessor.cpp
 * @brief Implementation of MidiForgeProcessor.
 */
#include "PluginProcessor.h"

#include "FactoryPresets.h"
#include "ParameterIds.h"
#include "UI/PluginEditor.h"

#include <cmath>

namespace midiforge
{

namespace
{
constexpr size_t reservedEngineEvents = 8192;
constexpr int reservedLongMessageBytes = 4096;
constexpr int largestShortMessage = 3;
constexpr juce::uint8 sysexStart = 0xF0;
} // namespace

MidiForgeProcessor::MidiForgeProcessor()
    : AudioProcessor (BusesProperties()), // MIDI only: no audio inputs or outputs
      parameters (*this, nullptr, "MidiForge", createParameterLayout())
{
    // Seed the randomizer modules differently in every session, then build the chain.
    setRandomSeedForNewModules ((unsigned) juce::Time::currentTimeMillis());
    moduleChain = std::make_unique<ModuleChain>();

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        auto& values = slotParameterValues[(size_t) slotIndex];
        values.moduleType = parameters.getRawParameterValue (ParameterIds::moduleType (slotIndex));
        values.isEnabled = parameters.getRawParameterValue (ParameterIds::slotEnabled (slotIndex));
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            values.values[parameterIndex] =
                parameters.getRawParameterValue (ParameterIds::moduleParameter (slotIndex, parameterIndex));
    }

    engineEvents.reserve (reservedEngineEvents);
    longMessagesPassedThrough.ensureSize (reservedLongMessageBytes);
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiForgeProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray moduleNames;
    for (int type = 0; type < numModuleTypes; ++type)
        moduleNames.add (getModuleDescription (type).name);

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const juce::String slotName = "Slot " + juce::String (slotIndex + 1);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("slot" + juce::String (slotIndex + 1),
                                                                           slotName, " | ");

        group->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParameterIds::moduleType (slotIndex), ParameterIds::parameterVersion },
            slotName + " Module", moduleNames, 0));

        group->addChild (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParameterIds::slotEnabled (slotIndex), ParameterIds::parameterVersion },
            slotName + " On", true));

        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        {
            // The host shows these values in the units of whichever module is loaded in the slot.
            auto attributes =
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction (
                        [this, slotIndex, parameterIndex] (float normalisedValue, int)
                        {
                            const auto& description =
                                getModuleDescription (getModuleType (slotIndex)).parameters[parameterIndex];
                            return juce::String (formatParameterValue (description, normalisedValue));
                        })
                    .withValueFromStringFunction (
                        [this, slotIndex, parameterIndex] (const juce::String& text)
                        {
                            const auto& description =
                                getModuleDescription (getModuleType (slotIndex)).parameters[parameterIndex];
                            return parseParameterText (description, text.trim().toStdString(),
                                                       getParameterValue (slotIndex, parameterIndex));
                        });

            group->addChild (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                    ParameterIds::parameterVersion },
                slotName + " " + juce::String (ParameterIds::valueSuffixes[parameterIndex]).toUpperCase(),
                juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, attributes));
        }
        layout.add (std::move (group));
    }
    return layout;
}

//==============================================================================
// Audio thread

void MidiForgeProcessor::prepareToPlay (double sampleRate, int /*maximumBlockSize*/)
{
    moduleChain->prepare (sampleRate);
}

void MidiForgeProcessor::releaseResources()
{
    moduleChain->reset();
}

bool MidiForgeProcessor::isBusesLayoutSupported (const BusesLayout& /*layouts*/) const
{
    return true; // no audio is processed, so any layout a host insists on is fine
}

void MidiForgeProcessor::readSlotSettings()
{
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        const auto& values = slotParameterValues[(size_t) slotIndex];
        auto& settings = slotSettings[(size_t) slotIndex];
        settings.moduleType = getModuleType (slotIndex);
        settings.isEnabled = values.isEnabled->load() >= 0.5f;
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            settings.parameterValues[parameterIndex] = juce::jlimit (0.0f, 1.0f, values.values[parameterIndex]->load());
    }
}

BlockContext MidiForgeProcessor::readBlockContext (int numSamples)
{
    BlockContext context;
    context.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    context.numSamples = std::max (1, numSamples);

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm(); bpm && std::isfinite (*bpm) && *bpm > 0.0)
                context.tempoBpm = juce::jlimit (10.0, 1000.0, *bpm); // guard against nonsense from hosts
            context.isPlaying = position->getIsPlaying();
            if (auto ppq = position->getPpqPosition(); ppq && std::isfinite (*ppq))
            {
                context.hasPpqPosition = true;
                context.ppqPositionAtBlockStart = *ppq;
            }
        }
    }
    return context;
}

void MidiForgeProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    readSlotSettings();
    const BlockContext context = readBlockContext (audio.getNumSamples());

    // Split the incoming MIDI: short messages go through the engine, long ones pass untouched.
    engineEvents.clear();
    longMessagesPassedThrough.clear();
    for (const auto metadata : midi)
    {
        const int numBytes = metadata.numBytes;
        if (numBytes >= 1 && numBytes <= largestShortMessage && metadata.data[0] != sysexStart &&
            engineEvents.size() < engineEvents.capacity())
        {
            MidiEvent event;
            event.samplePosition = juce::jlimit (0, context.numSamples - 1, metadata.samplePosition);
            event.status = metadata.data[0];
            event.data1 = numBytes > 1 ? metadata.data[1] : 0;
            event.data2 = numBytes > 2 ? metadata.data[2] : 0;
            event.numBytes = (uint8_t) numBytes;
            engineEvents.push_back (event);
        }
        else
        {
            longMessagesPassedThrough.addEvent (metadata.data, numBytes, metadata.samplePosition);
        }
    }

    moduleChain->process (engineEvents, context, slotSettings.data());

    midi.clear();
    for (const auto& event : engineEvents)
    {
        const juce::uint8 bytes[largestShortMessage] = { event.status, event.data1, event.data2 };
        midi.addEvent (bytes, event.numBytes, event.samplePosition);
    }
    midi.addEvents (longMessagesPassedThrough, 0, -1, 0);
}

//==============================================================================
// Slot access

int MidiForgeProcessor::getModuleType (int slotIndex) const noexcept
{
    const float rawValue = slotParameterValues[(size_t) slotIndex].moduleType->load();
    return juce::jlimit (0, numModuleTypes - 1, (int) std::lround (rawValue));
}

bool MidiForgeProcessor::isSlotEnabled (int slotIndex) const noexcept
{
    return slotParameterValues[(size_t) slotIndex].isEnabled->load() >= 0.5f;
}

float MidiForgeProcessor::getParameterValue (int slotIndex, int parameterIndex) const noexcept
{
    return slotParameterValues[(size_t) slotIndex].values[parameterIndex]->load();
}

void MidiForgeProcessor::setParameterAsGesture (const juce::String& parameterId, float normalisedValue)
{
    if (auto* parameter = parameters.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalisedValue));
        parameter->endChangeGesture();
    }
}

void MidiForgeProcessor::loadModule (int slotIndex, int moduleType)
{
    moduleType = juce::jlimit (0, numModuleTypes - 1, moduleType);
    if (auto* typeParameter = parameters.getParameter (ParameterIds::moduleType (slotIndex)))
        setParameterAsGesture (ParameterIds::moduleType (slotIndex), typeParameter->convertTo0to1 ((float) moduleType));

    const auto& description = getModuleDescription (moduleType);
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                               getNormalisedDefault (description.parameters[parameterIndex]));
}

void MidiForgeProcessor::swapSlots (int firstSlotIndex, int secondSlotIndex)
{
    const auto isValid = [] (int slotIndex) { return slotIndex >= 0 && slotIndex < numSlots; };
    if (firstSlotIndex == secondSlotIndex || ! isValid (firstSlotIndex) || ! isValid (secondSlotIndex))
        return;

    juce::StringArray firstIds { ParameterIds::moduleType (firstSlotIndex),
                                 ParameterIds::slotEnabled (firstSlotIndex) };
    juce::StringArray secondIds { ParameterIds::moduleType (secondSlotIndex),
                                  ParameterIds::slotEnabled (secondSlotIndex) };
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
    {
        firstIds.add (ParameterIds::moduleParameter (firstSlotIndex, parameterIndex));
        secondIds.add (ParameterIds::moduleParameter (secondSlotIndex, parameterIndex));
    }

    for (int index = 0; index < firstIds.size(); ++index)
    {
        const float firstValue = parameters.getParameter (firstIds[index])->getValue();
        const float secondValue = parameters.getParameter (secondIds[index])->getValue();
        setParameterAsGesture (firstIds[index], secondValue);
        setParameterAsGesture (secondIds[index], firstValue);
    }
}

void MidiForgeProcessor::clearSlot (int slotIndex)
{
    loadModule (slotIndex, 0);
    setParameterAsGesture (ParameterIds::slotEnabled (slotIndex), 1.0f);
}

//==============================================================================
// Presets and state

int MidiForgeProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const juce::String MidiForgeProcessor::getProgramName (int presetIndex)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (presetIndex, (int) presets.size()) ? presets[(size_t) presetIndex].name : "";
}

void MidiForgeProcessor::setCurrentProgram (int presetIndex)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (presetIndex, (int) presets.size()))
        return;

    currentPresetIndex = presetIndex;
    const auto& preset = presets[(size_t) presetIndex];
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        if (slotIndex >= (int) preset.slots.size())
        {
            clearSlot (slotIndex);
            continue;
        }

        const auto& presetSlot = preset.slots[(size_t) slotIndex];
        loadModule (slotIndex, (int) presetSlot.moduleType);
        const auto& description = getModuleDescription (presetSlot.moduleType);
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
            if (! std::isnan (presetSlot.plainValues[parameterIndex]))
                setParameterAsGesture (ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                       plainValueToNormalised (description.parameters[parameterIndex],
                                                               presetSlot.plainValues[parameterIndex]));
        setParameterAsGesture (ParameterIds::slotEnabled (slotIndex), 1.0f);
    }
}

void MidiForgeProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty ("preset", currentPresetIndex, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void MidiForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // State comes from project files, which may be damaged or crafted: accept only our own tree type.
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    currentPresetIndex = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("preset", 0));
    parameters.replaceState (state);
}

juce::AudioProcessorEditor* MidiForgeProcessor::createEditor()
{
    return new MidiForgeEditor (*this);
}

} // namespace midiforge

/** Called by the JUCE plug-in wrappers to create the plug-in. */
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new midiforge::MidiForgeProcessor();
}
