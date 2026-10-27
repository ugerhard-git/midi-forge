/**
 * @file StepModulatorPanel.h
 * @brief The controls of one step modulator: the step editor (draw the values with the mouse), the number of
 *        steps, the rate, glide and restart on key.
 */
#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

/**
 * Shows the steps of a step modulator as bars around a centre line and lets the user draw them:
 * click or drag to set values, hold Shift to snap to quarters (−100, −75 … +100), double-click a step to set
 * it to 0. The step playing right now is highlighted.
 */
class StepEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StepEditor (MidiForgeProcessor& processor, int stepModulatorIndex);

    void paint (juce::Graphics& graphics) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

    /** Repaints when the steps or the current step changed. Call regularly from a timer. */
    void refreshFromProcessor();

    /** @return The step (0-based) under an x position, for the number of steps shown. */
    int stepAt (float x) const noexcept;
    /** @return The value (-100..+100) for a y position; with snapping, rounded to quarters. */
    float valueAt (float y, bool snapsToQuarters) const noexcept;
    /** Sets one step's value (-100..+100) as a host parameter gesture. */
    void setStepValue (int step, float valuePercent);

private:
    /** Sets the step under the mouse to the value under the mouse. */
    void drawAt (const juce::MouseEvent& event);
    juce::Rectangle<float> barArea() const noexcept { return getLocalBounds().toFloat().reduced (4.0f, 4.0f); }

    MidiForgeProcessor& processor;
    const int stepModulatorIndex;
    StepModulatorSettings shownSettings;
    int shownStep = -1;
};

/** All controls of one step modulator. */
class StepModulatorPanel final : public juce::Component
{
public:
    StepModulatorPanel (MidiForgeProcessor& processor, int stepModulatorIndex);

    void paint (juce::Graphics& graphics) override;
    void resized() override;
    /** Updates the editor, the step count and the title line. Call regularly from a timer. */
    void refreshFromProcessor();

    StepEditor editor;

private:
    /** Adds or removes steps (within 4..16), as a host parameter gesture. */
    void changeStepCount (int difference);

    MidiForgeProcessor& processor;
    const int stepModulatorIndex;

    juce::TextButton fewerStepsButton, moreStepsButton;
    juce::Label stepCountLabel, stepCountName;
    juce::ComboBox rateMenu;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rateAttachment;
    juce::Label glideName;
    juce::Slider glideSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment glideAttachment;
    juce::Label glideValue;
    juce::ToggleButton restartSwitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment restartAttachment;
    juce::Label restartLabel;
    juce::String shownTitleValue;
};

} // namespace midiforge
