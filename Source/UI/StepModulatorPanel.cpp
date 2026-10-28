/**
 * @file StepModulatorPanel.cpp
 * @brief Implementation of StepEditor and StepModulatorPanel.
 */
#include "StepModulatorPanel.h"

#include "Plugin/ParameterIds.h"
#include "Theme.h"

#include <cmath>

namespace midiforge
{

namespace
{
void styleLabel (juce::Label& label, juce::Colour colour, float height, bool isBold = false)
{
    label.setFont (Theme::font (height, isBold));
    label.setColour (juce::Label::textColourId, colour);
    label.setJustificationType (juce::Justification::centredLeft);
    label.setInterceptsMouseClicks (false, false);
}

/** Sets a host parameter from a plain value as one complete gesture. */
void setPlainValueAsGesture (juce::RangedAudioParameter& parameter, float plainValue)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (parameter.convertTo0to1 (plainValue));
    parameter.endChangeGesture();
}
} // namespace

//==============================================================================
// StepEditor

StepEditor::StepEditor (MidiForgeProcessor& processorToControl, int stepModulatorIndexToShow)
    : processor (processorToControl), stepModulatorIndex (stepModulatorIndexToShow)
{
    setTooltip ("Click or drag to draw the steps. Hold Shift to snap to quarters. Double-click a step to set it to 0. "
                "Click a box in the strip below (or Alt+click a step) to tie it to the step before.");
    shownSettings = processor.getStepModulatorSettings (stepModulatorIndex);
}

juce::Rectangle<float> StepEditor::barArea() const noexcept
{
    auto area = getLocalBounds().toFloat().reduced (4.0f, 4.0f);
    area.removeFromBottom (tieStripHeight + 3.0f);
    return area;
}

juce::Rectangle<float> StepEditor::tieStripArea() const noexcept
{
    const auto bars = barArea();
    return { bars.getX(), bars.getBottom() + 3.0f, bars.getWidth(), tieStripHeight };
}

int StepEditor::stepAt (float x) const noexcept
{
    const auto area = barArea();
    const int numSteps = shownSettings.numSteps;
    const int step = (int) std::floor ((x - area.getX()) / area.getWidth() * (float) numSteps);
    return juce::jlimit (0, numSteps - 1, step);
}

float StepEditor::valueAt (float y, bool snapsToQuarters) const noexcept
{
    const auto area = barArea();
    const float value = juce::jlimit (-100.0f, 100.0f, (area.getCentreY() - y) / (area.getHeight() / 2.0f) * 100.0f);
    return snapsToQuarters ? std::round (value / 25.0f) * 25.0f : std::round (value);
}

void StepEditor::setStepValue (int step, float valuePercent)
{
    // A tied step has no value of its own: drawing on it changes the first step of its group.
    const int groupStart = shownSettings.groupStartOf (step);
    if (auto* parameter = processor.parameters.getParameter (ParameterIds::stepValue (stepModulatorIndex, groupStart)))
        setPlainValueAsGesture (*parameter, valuePercent);
    refreshFromProcessor();
}

void StepEditor::toggleTie (int step)
{
    if (step <= 0) // step 1 cannot be tied
        return;
    if (auto* parameter = processor.parameters.getParameter (ParameterIds::stepTie (stepModulatorIndex, step)))
        setPlainValueAsGesture (*parameter, shownSettings.isTied[step] ? 0.0f : 1.0f);
    refreshFromProcessor();
}

void StepEditor::drawAt (const juce::MouseEvent& event)
{
    setStepValue (stepAt (event.position.x), valueAt (event.position.y, event.mods.isShiftDown()));
}

void StepEditor::mouseDown (const juce::MouseEvent& event)
{
    isEditingTies = event.position.y >= tieStripArea().getY() - 1.0f || event.mods.isAltDown();
    if (isEditingTies)
        toggleTie (stepAt (event.position.x));
    else
        drawAt (event);
}

void StepEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (! isEditingTies)
        drawAt (event);
}

void StepEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! isEditingTies)
        setStepValue (stepAt (event.position.x), 0.0f);
}

void StepEditor::refreshFromProcessor()
{
    const auto settings = processor.getStepModulatorSettings (stepModulatorIndex);
    const int step = processor.getCurrentStep (stepModulatorIndex);
    bool changed = settings.numSteps != shownSettings.numSteps || step != shownStep;
    for (int index = 0; index < maximumSteps && ! changed; ++index)
        changed = std::abs (settings.stepValues[index] - shownSettings.stepValues[index]) > 1.0e-4f ||
                  settings.isTied[index] != shownSettings.isTied[index];
    if (! changed)
        return;
    shownSettings = settings;
    shownStep = step;
    repaint();
}

void StepEditor::paint (juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour (Theme::Colours::windowBackground.darker (0.15f));
    graphics.fillRoundedRectangle (bounds, 5.0f);

    const auto area = barArea();
    const float centreY = area.getCentreY();
    graphics.setColour (Theme::Colours::controlTrack);
    graphics.fillRect (area.getX(), centreY - 0.5f, area.getWidth(), 1.0f);

    const int numSteps = shownSettings.numSteps;
    const float stepWidth = area.getWidth() / (float) numSteps;
    const float gap = juce::jmin (3.0f, stepWidth * 0.15f);
    const int currentGroupStart = shownStep >= 0 ? shownSettings.groupStartOf (shownStep) : -1;

    // One bar per group: a step and the steps tied to it are drawn as one wide bar.
    for (int groupStart = 0; groupStart < numSteps;)
    {
        const int groupEnd = shownSettings.groupEndOf (groupStart, numSteps);
        const float value = shownSettings.stepValues[groupStart];
        const float height = std::abs (value) * area.getHeight() / 2.0f;
        const float x = area.getX() + (float) groupStart * stepWidth + gap / 2.0f;
        const float width = (float) (groupEnd - groupStart) * stepWidth - gap;
        const auto bar =
            juce::Rectangle<float> (x, value >= 0.0f ? centreY - height : centreY, width, juce::jmax (height, 1.5f));
        const bool isCurrent = groupStart == currentGroupStart;
        graphics.setColour (isCurrent ? Theme::Colours::modulation : Theme::Colours::modulation.withAlpha (0.45f));
        graphics.fillRoundedRectangle (bar, 2.0f);
        groupStart = groupEnd;
    }

    // The tie strip: a box per step (from step 2); filled when the step is tied to the one before.
    const auto strip = tieStripArea();
    for (int step = 1; step < numSteps; ++step)
    {
        const auto box = juce::Rectangle<float> (strip.getX() + (float) step * stepWidth + gap / 2.0f, strip.getY(),
                                                 stepWidth - gap, strip.getHeight());
        graphics.setColour (shownSettings.isTied[step] ? Theme::Colours::modulation.withAlpha (0.8f)
                                                       : Theme::Colours::controlTrack);
        graphics.fillRoundedRectangle (box, 2.0f);
    }
    // The step playing now, under the strip's first box position for step 1
    if (shownStep >= 0 && shownStep < numSteps)
    {
        graphics.setColour (Theme::Colours::modulation);
        graphics.fillRect (area.getX() + (float) shownStep * stepWidth + gap / 2.0f, bounds.getBottom() - 2.0f,
                           stepWidth - gap, 2.0f);
    }
}

//==============================================================================
// StepModulatorPanel

StepModulatorPanel::StepModulatorPanel (MidiForgeProcessor& processorToControl, int stepModulatorIndexToShow)
    : editor (processorToControl, stepModulatorIndexToShow), processor (processorToControl),
      stepModulatorIndex (stepModulatorIndexToShow),
      glideAttachment (processor.parameters, ParameterIds::stepGlide (stepModulatorIndex), glideSlider),
      restartAttachment (processor.parameters, ParameterIds::stepRestartOnKey (stepModulatorIndex), restartSwitch)
{
    addAndMakeVisible (editor);

    stepCountName.setText ("Steps", juce::dontSendNotification);
    styleLabel (stepCountName, Theme::Colours::secondaryText, 11.5f);
    styleLabel (stepCountLabel, Theme::Colours::primaryText, 13.0f, true);
    stepCountLabel.setJustificationType (juce::Justification::centred);
    fewerStepsButton.setButtonText (juce::String::fromUTF8 ("\xe2\x88\x92")); // −
    moreStepsButton.setButtonText ("+");
    fewerStepsButton.setTooltip ("One step less (at least 4)");
    moreStepsButton.setTooltip ("One step more (at most 16)");
    fewerStepsButton.onClick = [this] { changeStepCount (-1); };
    moreStepsButton.onClick = [this] { changeStepCount (+1); };
    for (auto* component : std::initializer_list<juce::Component*> { &stepCountName, &stepCountLabel, &fewerStepsButton,
                                                                     &moreStepsButton })
        addAndMakeVisible (*component);
    for (auto* button : { &fewerStepsButton, &moreStepsButton })
        button->setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));

    if (auto* rateParameter = dynamic_cast<juce::AudioParameterChoice*> (
            processor.parameters.getParameter (ParameterIds::stepRate (stepModulatorIndex))))
        for (int index = 0; index < rateParameter->choices.size(); ++index)
            rateMenu.addItem (rateParameter->choices[index], index + 1);
    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::stepRate (stepModulatorIndex), rateMenu);
    rateMenu.setTooltip ("Length of one step, in time with the song (T = triplet, D = dotted)");
    addAndMakeVisible (rateMenu);

    glideName.setText ("Glide", juce::dontSendNotification);
    styleLabel (glideName, Theme::Colours::secondaryText, 11.5f);
    styleLabel (glideValue, Theme::Colours::primaryText, 12.5f, true);
    glideSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    glideSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    glideSlider.setColour (juce::Slider::trackColourId, Theme::Colours::modulation);
    glideSlider.setDoubleClickReturnValue (true, 0.0);
    glideSlider.setTooltip ("Off: the value jumps from step to step. Higher: it slides to the next step's value "
                            "during that last part of each step.");
    for (auto* component : std::initializer_list<juce::Component*> { &glideName, &glideSlider, &glideValue })
        addAndMakeVisible (*component);

    restartSwitch.setColour (juce::ToggleButton::tickColourId, Theme::Colours::modulation);
    restartSwitch.setTooltip ("Start again at step 1 when you press the first key after all keys were released");
    restartLabel.setText ("Restart", juce::dontSendNotification);
    styleLabel (restartLabel, Theme::Colours::secondaryText, 11.0f);
    addAndMakeVisible (restartSwitch);
    addAndMakeVisible (restartLabel);

    refreshFromProcessor();
}

void StepModulatorPanel::changeStepCount (int difference)
{
    const int newCount = juce::jlimit (minimumSteps, maximumSteps,
                                       processor.getStepModulatorSettings (stepModulatorIndex).numSteps + difference);
    if (auto* parameter = processor.parameters.getParameter (ParameterIds::stepCount (stepModulatorIndex)))
        setPlainValueAsGesture (*parameter, (float) newCount);
    refreshFromProcessor();
}

void StepModulatorPanel::paint (juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat();
    graphics.setColour (Theme::Colours::slotBackground);
    graphics.fillRoundedRectangle (area, 8.0f);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto titleArea = getLocalBounds().reduced (12, 6).removeFromTop (18);
    graphics.setColour (Theme::Colours::primaryText);
    graphics.setFont (Theme::font (14.0f, true));
    graphics.drawText ("Steps " + juce::String (stepModulatorIndex + 1), titleArea, juce::Justification::centredLeft);
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (11.5f));
    graphics.drawText (shownTitleValue, titleArea, juce::Justification::centredRight);
}

void StepModulatorPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 6);
    area.removeFromTop (22); // title
    auto controls = area.removeFromBottom (24);
    area.removeFromBottom (6);
    editor.setBounds (area);

    stepCountName.setBounds (controls.removeFromLeft (34));
    fewerStepsButton.setBounds (controls.removeFromLeft (20));
    stepCountLabel.setBounds (controls.removeFromLeft (24));
    moreStepsButton.setBounds (controls.removeFromLeft (20));
    controls.removeFromLeft (8);
    rateMenu.setBounds (controls.removeFromLeft (72));
    controls.removeFromLeft (8);
    glideName.setBounds (controls.removeFromLeft (32));
    auto restartArea = controls.removeFromRight (84);
    glideValue.setBounds (controls.removeFromRight (34));
    glideSlider.setBounds (controls.reduced (0, 2));
    restartSwitch.setBounds (restartArea.removeFromLeft (34));
    restartLabel.setBounds (restartArea);
}

void StepModulatorPanel::refreshFromProcessor()
{
    editor.refreshFromProcessor();

    const auto settings = processor.getStepModulatorSettings (stepModulatorIndex);
    const juce::String countText (settings.numSteps);
    if (stepCountLabel.getText() != countText)
        stepCountLabel.setText (countText, juce::dontSendNotification);
    fewerStepsButton.setEnabled (settings.numSteps > minimumSteps);
    moreStepsButton.setEnabled (settings.numSteps < maximumSteps);

    if (auto* glideParameter = processor.parameters.getParameter (ParameterIds::stepGlide (stepModulatorIndex)))
        if (glideValue.getText() != glideParameter->getCurrentValueAsText())
            glideValue.setText (glideParameter->getCurrentValueAsText(), juce::dontSendNotification);

    // Title line: "step 6 / 16 · -30" while routed, "not routed" otherwise.
    const bool isInUse = processor.isSourceInUse (firstStepModulatorSource + stepModulatorIndex);
    const int valuePercent = juce::roundToInt (processor.getStepModulatorValue (stepModulatorIndex) * 100.0f);
    const juce::String titleValue =
        isInUse ? "step " + juce::String (processor.getCurrentStep (stepModulatorIndex) + 1) + " / " +
                      juce::String (settings.numSteps) + juce::String::fromUTF8 (" \xc2\xb7 ") +
                      (valuePercent > 0 ? "+" : "") + juce::String (valuePercent)
                : juce::String ("not routed");
    if (titleValue != shownTitleValue)
    {
        shownTitleValue = titleValue;
        repaint (getLocalBounds().removeFromTop (30));
    }
}

} // namespace midiforge
