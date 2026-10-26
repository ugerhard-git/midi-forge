/**
 * @file SlotRow.h
 * @brief One row of the editor: power switch, module menu, four knobs and move buttons for one slot.
 */
#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

/** A rotary knob with the parameter name and its current value next to it. */
class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob();
    void resized() override;

    juce::Slider knob;
    juce::Label nameLabel;
    juce::Label valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> parameterAttachment;
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
