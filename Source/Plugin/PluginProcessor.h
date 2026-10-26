/**
 * @file PluginProcessor.h
 * @brief The JUCE AudioProcessor: host parameters, state saving, presets, and the bridge between
 *        JUCE's MidiBuffer and the engine's ModuleChain.
 */
#pragma once

#include "Engine/ModuleChain.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace midiforge
{

class MidiForgeProcessor final : public juce::AudioProcessor
{
public:
    MidiForgeProcessor();
    ~MidiForgeProcessor() override = default;

    //==============================================================================
    // juce::AudioProcessor

    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPresetIndex; }
    void setCurrentProgram (int presetIndex) override;
    const juce::String getProgramName (int presetIndex) override;
    void changeProgramName (int /*presetIndex*/, const juce::String& /*newName*/) override {}

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Slot access for the editor (message thread)

    /** @return The ModuleType loaded in a slot. Safe from any thread. */
    int getModuleType (int slotIndex) const noexcept;
    /** @return Whether a slot is switched on. Safe from any thread. */
    bool isSlotEnabled (int slotIndex) const noexcept;
    /** @return The normalised value (0..1) of parameter A..D of a slot. Safe from any thread. */
    float getParameterValue (int slotIndex, int parameterIndex) const noexcept;

    /** Loads a module into a slot and resets its four parameters to that module's defaults. */
    void loadModule (int slotIndex, int moduleType);
    /** Exchanges everything (module, on/off, parameters) between two slots. */
    void swapSlots (int firstSlotIndex, int secondSlotIndex);
    /** Empties a slot and switches it on. */
    void clearSlot (int slotIndex);
    /** Releases all sounding notes at the start of the next block. Safe from any thread. */
    void panic() noexcept { moduleChain->requestPanic(); }

    /** All host parameters. Public so the editor can attach its controls. */
    juce::AudioProcessorValueTreeState parameters;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** Sets a host parameter as one complete user gesture, so hosts record it as automation. */
    void setParameterAsGesture (const juce::String& parameterId, float normalisedValue);
    void readSlotSettings();
    BlockContext readBlockContext (int numSamples);

    /** Direct pointers to the parameter values, readable without locks on the audio thread. */
    struct SlotParameterValues
    {
        std::atomic<float>* moduleType = nullptr;
        std::atomic<float>* isEnabled = nullptr;
        std::atomic<float>* values[numParametersPerSlot] {};
    };

    std::array<SlotParameterValues, numSlots> slotParameterValues;
    std::array<SlotSettings, numSlots> slotSettings;
    std::unique_ptr<ModuleChain> moduleChain;
    MidiEventList engineEvents;
    juce::MidiBuffer longMessagesPassedThrough; ///< SysEx and other messages the engine does not handle.
    int currentPresetIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiForgeProcessor)
};

} // namespace midiforge
