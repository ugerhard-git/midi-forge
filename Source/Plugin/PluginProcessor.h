/**
 * @file PluginProcessor.h
 * @brief The JUCE AudioProcessor: host parameters, state saving, presets, and the bridge between
 *        JUCE's MidiBuffer and the MidiForgeEngine (module chain plus LFOs).
 */
#pragma once

#include "Engine/MidiForgeEngine.h"
#include "FactoryPresets.h"

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
    void panic() noexcept { engine->requestPanic(); }

    //==============================================================================
    // Modulation access for the editor (message thread)

    /** @return True if at least one active route targets this slot parameter. */
    bool isParameterModulated (int slotIndex, int parameterIndex) const noexcept;
    /** @return The value (0..1) of a slot parameter after modulation, as last used by the engine. */
    float getModulatedParameterValue (int slotIndex, int parameterIndex) const noexcept;
    /** @return An LFO's latest output (-1..+1). */
    float getLfoValue (int lfoIndex) const noexcept { return lfoValueReadouts[(size_t) lfoIndex].load(); }
    /** @return An LFO's latest position in its cycle (0..1). */
    float getLfoPhase (int lfoIndex) const noexcept { return lfoPhaseReadouts[(size_t) lfoIndex].load(); }
    /** @return The current settings of one LFO, read from the host parameters. */
    LfoSettings getLfoSettings (int lfoIndex) const noexcept;
    /**
     * @return A readable name for a modulation target, using the module currently in that slot,
     *         e.g. "Slot 2 · Gate". Target 0 is "None".
     */
    juce::String getModulationTargetName (int targetIndex) const;

    /** All host parameters. Public so the editor can attach its controls. */
    juce::AudioProcessorValueTreeState parameters;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** Sets a host parameter as one complete user gesture, so hosts record it as automation. */
    void setParameterAsGesture (const juce::String& parameterId, float normalisedValue);
    void addSlotParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void addLfoParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void addRouteParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void readEngineSettings();
    BlockContext readBlockContext (int numSamples);
    void publishReadoutsForEditor();
    /** Sets every LFO and route parameter back to its default. */
    void resetModulationToDefaults();
    /** Sets the LFOs and routes of a factory preset (everything else back to defaults). */
    void applyPresetModulation (const FactoryPreset& preset);
    /** Sets a host parameter from a plain value (e.g. Hz, a choice index), as one gesture. */
    void setPlainParameterValue (const juce::String& parameterId, float plainValue);

    /** Direct pointers to the parameter values, readable without locks on the audio thread. */
    struct SlotParameterValues
    {
        std::atomic<float>* moduleType = nullptr;
        std::atomic<float>* isEnabled = nullptr;
        std::atomic<float>* values[numParametersPerSlot] {};
    };

    struct LfoParameterValues
    {
        std::atomic<float>* waveform = nullptr;
        std::atomic<float>* syncMode = nullptr;
        std::atomic<float>* rateHz = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* phaseDegrees = nullptr;
        std::atomic<float>* pulseWidthPercent = nullptr;
        std::atomic<float>* restartsOnKey = nullptr;
    };

    struct RouteParameterValues
    {
        std::atomic<float>* source = nullptr;
        std::atomic<float>* target = nullptr;
        std::atomic<float>* amountPercent = nullptr;
    };

    std::array<SlotParameterValues, numSlots> slotParameterValues;
    std::array<LfoParameterValues, numLfos> lfoParameterValues;
    std::array<RouteParameterValues, numModulationRoutes> routeParameterValues;
    EngineSettings engineSettings;
    std::unique_ptr<MidiForgeEngine> engine;

    // Written by the audio thread after each block, read by the editor.
    std::array<std::atomic<float>, numSlots * numParametersPerSlot> modulatedValueReadouts {};
    std::array<std::atomic<float>, numLfos> lfoValueReadouts {};
    std::array<std::atomic<float>, numLfos> lfoPhaseReadouts {};
    MidiEventList engineEvents;
    juce::MidiBuffer longMessagesPassedThrough; ///< SysEx and other messages the engine does not handle.
    int currentPresetIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiForgeProcessor)
};

} // namespace midiforge
