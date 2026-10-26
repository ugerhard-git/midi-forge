/**
 * @file SlotRow.h
 * @brief One row of the editor: power switch, module menu, four knobs and move buttons for one slot.
 */
#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

/**
 * A rotary knob with the parameter name and its current value next to it.
 * When an LFO modulates the parameter, a dot on an outer ring shows the value the chain actually uses.
 */
class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob();
    void resized() override;
    void paintOverChildren (juce::Graphics& graphics) override;

    /**
     * Shows or hides the modulation ring.
     * @param isModulated          True if an active route targets this parameter.
     * @param modulatedValue01     The value (0..1) after modulation.
     */
    void setModulation (bool isModulated, float modulatedValue01);

    juce::Slider knob;
    juce::Label nameLabel;
    juce::Label valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> parameterAttachment;

private:
    bool showsModulation = false;
    float modulatedValue = 0.0f;
};

/**
 * Shows and edits one slot. The row polls the processor (refreshFromProcessor) instead of listening,
 * so changes from host automation, presets and other rows all show up the same way.
 */
class SlotRow final : public juce::Component
{
public:
    SlotRow (MidiForgeProcessor& processor, int slotIndex);

    void paint (juce::Graphics& graphics) override;
    void resized() override;

    /** Updates the row from the processor's current parameter values. Call regularly from a timer. */
    void refreshFromProcessor();

private:
    void fillModuleMenu();
    void showModule (int moduleType);
    void updateValueTexts();
    void updateModulationRings();

    MidiForgeProcessor& processor;
    const int slotIndex;
    int displayedModuleType = -1;
    bool displayedAsEnabled = true;

    juce::ToggleButton powerSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerSwitchAttachment;
    juce::ComboBox moduleMenu;
    juce::Label moduleSummary;
    ParameterKnob parameterKnobs[numParametersPerSlot];
    juce::TextButton moveUpButton, moveDownButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotRow)
};

} // namespace midiforge
