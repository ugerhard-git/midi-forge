/**
 * @file PluginProcessor.h
 * @brief The JUCE AudioProcessor: host parameters, state saving, presets, and the bridge between
 *        JUCE's MidiBuffer and the MidiForgeEngine (module chain plus LFOs).
 */
#pragma once

#include "Engine/MidiForgeEngine.h"
#include "FactoryPresets.h"
#include "UserPresetLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

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
    /**
     * Exchanges everything (module, on/off, parameters, shown page) between two slots. Modulation routes
     * follow their modules.
     */
    void swapSlots (int firstSlotIndex, int secondSlotIndex);
    /**
     * Removes the module of a slot: the modules below move up one slot and the empty slot goes to the end of
     * the chain. Routes follow their modules; routes to the removed module are set to None. The removal can
     * be undone with undoModuleRemoval() while the last slot is still empty.
     */
    void removeModule (int slotIndex);
    /** @return True if the last removal can still be undone (there is one, and the last slot is empty). */
    bool canUndoModuleRemoval() const noexcept;
    /** Puts the last removed module back where it was, with its settings and routes. */
    void undoModuleRemoval();
    /** @return A number that grows with every removal, so the editor notices new ones. */
    int getModuleRemovalCount() const noexcept { return moduleRemovalCount; }
    /** @return The name of the last removed module and the slot (1-based) it was in. */
    juce::String getLastRemovedModuleName() const;
    int getLastRemovedSlotNumber() const noexcept { return lastRemovedModule ? lastRemovedModule->slotIndex + 1 : 0; }
    /** Empties a slot and switches it on. */
    void clearSlot (int slotIndex);
    /** @return The knob page (0 = A-D, 1 = E-H) the editor shows for a slot; always a page the module has. */
    int getShownPage (int slotIndex) const noexcept;
    /** Remembers which knob page the editor shows for a slot (saved in projects and user presets). */
    void setShownPage (int slotIndex, int page) noexcept;

    /** Releases all sounding notes at the start of the next block. Safe from any thread. */
    void panic() noexcept { engine->requestPanic(); }

    //==============================================================================
    // Presets (message thread). The host's "programs" are the factory presets; user presets are files
    // managed by UserPresetLibrary.

    /** What the preset menu shows as loaded. */
    enum class PresetKind
    {
        none, ///< Settings that were never saved as a preset (or the preset was deleted)
        factory,
        user
    };

    PresetKind getCurrentPresetKind() const noexcept { return currentPresetKind; }
    /** @return The name of the loaded preset, or "Untitled" if there is none. */
    juce::String getCurrentPresetName() const;
    /** @return True if any parameter differs from the values the current preset was loaded or saved with. */
    bool isCurrentPresetModified() const;

    UserPresetLibrary& getUserPresetLibrary() noexcept { return userPresets; }
    /** Uses another folder for user presets (the tests use a temporary folder). */
    void setUserPresetFolder (const juce::File& folder) { userPresets = UserPresetLibrary (folder); }

    /** Loads a user preset. @return False if it is missing or not a valid preset file. */
    bool loadUserPreset (const juce::String& name);
    /** Saves the current settings as a user preset and makes it the current preset. @return Error text or "". */
    juce::String saveUserPreset (const juce::String& name);
    /** Renames the current user preset. @return Error text or "". */
    juce::String renameCurrentUserPreset (const juce::String& newName);
    /** Deletes the current user preset file; the settings stay as they are, now "Untitled". */
    bool deleteCurrentUserPreset();
    /** Copies a preset file into the library and loads it. @return Error text or "". */
    juce::String importUserPreset (const juce::File& file);
    /** Loads the next (+1) or previous (-1) preset: factory presets first, then user presets, wrapping around. */
    void stepPreset (int direction);

    //==============================================================================
    // Modulation access for the editor (message thread)

    /**
     * @return True if at least one active route targets this slot parameter (parameterIndex
     *         slotSwitchParameterIndex: the slot's on/off switch).
     */
    bool isParameterModulated (int slotIndex, int parameterIndex) const noexcept;
    /** @return True if at least one active route moves this slot's on/off switch. */
    bool isSlotSwitchModulated (int slotIndex) const noexcept;
    /** @return Whether the slot was on after modulation, as last used by the engine. */
    bool isSlotEnabledAfterModulation (int slotIndex) const noexcept;
    /**
     * @return True if the host has run the engine recently (within half a second), so the modulated values
     *         below are current. While the host is not processing they are stale.
     */
    bool isEngineRunning() const noexcept
    {
        return juce::Time::getMillisecondCounter() - lastBlockTimeMilliseconds.load (std::memory_order_relaxed) <
               engineRunningTimeoutMilliseconds;
    }
    static constexpr juce::uint32 engineRunningTimeoutMilliseconds = 500;
    /** @return The value (0..1) of a slot parameter after modulation, as last used by the engine. */
    float getModulatedParameterValue (int slotIndex, int parameterIndex) const noexcept;
    /** @return An LFO's latest output (-1..+1). */
    float getLfoValue (int lfoIndex) const noexcept { return lfoValueReadouts[(size_t) lfoIndex].load(); }
    /** @return An LFO's latest position in its cycle (0..1). */
    float getLfoPhase (int lfoIndex) const noexcept { return lfoPhaseReadouts[(size_t) lfoIndex].load(); }
    /** @return The current settings of one LFO, read from the host parameters. */
    LfoSettings getLfoSettings (int lfoIndex) const noexcept;
    /** @return The current settings of one Note Random modulator, read from the host parameters. */
    NoteRandomSettings getNoteRandomSettings (int randomIndex) const noexcept;
    /** @return The current settings of one step modulator, read from the host parameters. */
    StepModulatorSettings getStepModulatorSettings (int stepModulatorIndex) const noexcept;
    /** @return A step modulator's latest output (-1..+1). */
    float getStepModulatorValue (int stepModulatorIndex) const noexcept
    {
        return stepModulatorValueReadouts[(size_t) stepModulatorIndex].load();
    }
    /** @return The step (0-based) a step modulator played at the end of the last block. */
    int getCurrentStep (int stepModulatorIndex) const noexcept
    {
        return currentStepReadouts[(size_t) stepModulatorIndex].load();
    }
    /** @return True if an active route uses this modulation source (for the dots on the sub-tabs). */
    bool isSourceInUse (int sourceIndex) const noexcept;
    /** @return A Note Random modulator's current value (-1..+1). */
    float getNoteRandomValue (int randomIndex) const noexcept
    {
        return noteRandomValueReadouts[(size_t) randomIndex].load();
    }
    /**
     * @return A readable name for a modulation target, using the module currently in that slot,
     *         e.g. "Slot 2 · Gate". Target 0 is "None".
     */
    juce::String getModulationTargetName (int targetIndex) const;

    /** All host parameters. Public so the editor can attach its controls. */
    juce::AudioProcessorValueTreeState parameters;

private:
    /** Everything that makes up one slot, as normalised host parameter values plus the shown page. */
    struct SlotContents
    {
        float moduleTypeValue = 0.0f;
        float enabledValue = 1.0f;
        std::array<float, numParametersPerSlot> parameterValues {};
        int shownPage = 0;
    };
    /** A removed module, kept for undo. */
    struct RemovedModule
    {
        int slotIndex = 0;
        SlotContents contents;
        std::array<int, numModulationRoutes> targetsOfRoutesToIt {}; ///< Old target per route, or 0
    };

    SlotContents copySlotContents (int slotIndex) const;
    void setSlotContents (int slotIndex, const SlotContents& contents);
    /** @return A route's target number (0 = none). */
    int getRouteTarget (int routeIndex) const noexcept;
    /**
     * Moves route targets along with moved modules.
     * @param newSlotOfOldSlot  For every slot, where its module went (-1: removed, the route gets None).
     */
    void moveRouteTargets (const std::array<int, numSlots>& newSlotOfOldSlot);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    /** Sets a host parameter as one complete user gesture, so hosts record it as automation. */
    void setParameterAsGesture (const juce::String& parameterId, float normalisedValue);
    void addSlotParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void addLfoParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    void addRouteParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Added in 0.6.0, after all older parameters: Note Random modulators and the route polarities. */
    void addNoteRandomAndPolarityParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Added in 0.7.0, after all older parameters: slot parameters E-H. */
    void addSecondPageSlotParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Added in 0.14.0, after all older parameters: the step modulators. */
    void addStepModulatorParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    /** Added in 0.16.0, after all older parameters: the step ties. */
    void addStepTieParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    std::unique_ptr<juce::AudioParameterFloat> createModuleParameter (int slotIndex, int parameterIndex, int version);
    /**
     * The parameter state plus the shown knob pages and the knob layout version, as saved in projects and
     * user presets.
     */
    juce::ValueTree copyStateWithPages();
    void restoreShownPages (const juce::ValueTree& state);
    /**
     * @return The knob layout version a saved state was written with (see currentKnobLayoutVersion).
     *         States from before the version was stored are recognised by what they contain.
     */
    static int getKnobLayoutVersionOf (const juce::ValueTree& state);
    /**
     * After loading a state saved with an older knob layout: sets the knobs that the loaded modules gained
     * since then to their defaults (the state holds meaningless values for them).
     */
    void resetKnobsAddedAfter (int savedKnobLayoutVersion);
    void readEngineSettings();
    /**
     * Reports the fixed delays of the modules (Groove's Early, Strum's Window) to the host as latency, so it
     * can compensate.
     */
    void reportLatencyOfModules (double sampleRate);
    static constexpr int grooveEarlyParameterIndex = 4; ///< Groove's "Early" knob (E)
    static constexpr int strumWindowParameterIndex = 3; ///< Strum's "Window" knob (D)
    BlockContext readBlockContext (int numSamples);
    void publishReadoutsForEditor();
    /**
     * Sets every host parameter from a saved state (preset file or project). Values are clamped to each
     * parameter's range; parameters missing from the state get their defaults. Unknown entries are ignored.
     */
    void applyStateValues (const juce::ValueTree& state);
    /** Remembers the current parameter values, to detect later changes (the "*" in the preset menu). */
    void rememberValuesOfCurrentPreset();
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
        std::atomic<float>* polarity = nullptr;
    };

    struct NoteRandomParameterValues
    {
        std::atomic<float>* chordWindowMilliseconds = nullptr;
        std::atomic<float>* trigger = nullptr;
        std::atomic<float>* steps = nullptr;
    };

    std::array<SlotParameterValues, numSlots> slotParameterValues;
    std::array<LfoParameterValues, numLfos> lfoParameterValues;
    std::array<RouteParameterValues, numModulationRoutes> routeParameterValues;
    std::array<NoteRandomParameterValues, numNoteRandoms> noteRandomParameterValues;

    struct StepModulatorParameterValues
    {
        std::atomic<float>* numSteps = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* glidePercent = nullptr;
        std::atomic<float>* restartsOnKey = nullptr;
        std::atomic<float>* stepValuesPercent[maximumSteps] {};
        std::atomic<float>* isTied[maximumSteps] {}; ///< index 0 unused (step 1 cannot be tied)
    };
    std::array<StepModulatorParameterValues, numStepModulators> stepModulatorParameterValues;
    EngineSettings engineSettings;
    std::unique_ptr<MidiForgeEngine> engine;

    // Written by the audio thread after each block, read by the editor.
    std::array<std::atomic<float>, numSlots * numParametersPerSlot> modulatedValueReadouts {};
    std::array<std::atomic<bool>, numSlots> modulatedSwitchReadouts {};
    std::atomic<juce::uint32> lastBlockTimeMilliseconds { 0 }; ///< When the host last called processBlock
    std::array<std::atomic<float>, numLfos> lfoValueReadouts {};
    std::array<std::atomic<float>, numLfos> lfoPhaseReadouts {};
    std::array<std::atomic<float>, numNoteRandoms> noteRandomValueReadouts {};
    std::array<std::atomic<float>, numStepModulators> stepModulatorValueReadouts {};
    std::array<std::atomic<int>, numStepModulators> currentStepReadouts {};
    MidiEventList engineEvents;
    juce::MidiBuffer longMessagesPassedThrough; ///< SysEx and other messages the engine does not handle.
    int currentPresetIndex = 0;                 ///< Last factory preset, reported to the host as the current program
    PresetKind currentPresetKind = PresetKind::factory;
    juce::String currentUserPresetName;
    std::vector<float> valuesOfCurrentPreset;
    UserPresetLibrary userPresets;
    std::array<std::atomic<int>, numSlots> shownPages {};
    static constexpr const char* shownPagesProperty = "shownPages";
    static constexpr const char* knobLayoutProperty = "knobLayout";
    std::optional<RemovedModule> lastRemovedModule;
    int moduleRemovalCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiForgeProcessor)
};

} // namespace midiforge
