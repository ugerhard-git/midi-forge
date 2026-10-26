/**
 * @file SlotRow.cpp
 * @brief Implementation of ParameterKnob and SlotRow.
 */
#include "SlotRow.h"

#include "Plugin/ParameterIds.h"
#include "Theme.h"

namespace midiforge
{

//==============================================================================
ParameterKnob::ParameterKnob()
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setMouseDragSensitivity (180);
    addAndMakeVisible (knob);

    for (auto* label : { &nameLabel, &valueLabel })
    {
        label->setJustificationType (juce::Justification::centredLeft);
        label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*label);
    }
    nameLabel.setFont (Theme::font (11.0f));
    nameLabel.setColour (juce::Label::textColourId, Theme::Colours::secondaryText);
    valueLabel.setFont (Theme::font (13.5f, true));
}

void ParameterKnob::resized()
{
    auto area = getLocalBounds();
    knob.setBounds (area.removeFromLeft (area.getHeight()));
    area.removeFromLeft (4);
    nameLabel.setBounds (area.removeFromTop (area.getHeight() / 2).withTrimmedTop (2));
    valueLabel.setBounds (area.withTrimmedBottom (4));
}

//==============================================================================
SlotRow::SlotRow (MidiForgeProcessor& processorToControl, int slotIndexToShow)
    : processor (processorToControl), slotIndex (slotIndexToShow)
{
    powerSwitch.setTooltip ("Switch this slot on or off. A switched-off slot passes everything through.");
    powerSwitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.parameters, ParameterIds::slotEnabled (slotIndex), powerSwitch);
    addAndMakeVisible (powerSwitch);

    fillModuleMenu();
    moduleMenu.onChange = [this]
    {
        const int chosenType = moduleMenu.getSelectedId() - 1; // menu IDs are module type + 1
        if (chosenType >= 0 && chosenType != processor.getModuleType (slotIndex))
            processor.loadModule (slotIndex, chosenType);
        refreshFromProcessor();
    };
    addAndMakeVisible (moduleMenu);

    moduleSummary.setFont (Theme::font (11.0f));
    moduleSummary.setColour (juce::Label::textColourId, Theme::Colours::secondaryText);
    moduleSummary.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (moduleSummary);

    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
    {
        auto& parameterKnob = parameterKnobs[parameterIndex];
        parameterKnob.parameterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.parameters, ParameterIds::moduleParameter (slotIndex, parameterIndex), parameterKnob.knob);
        addChildComponent (parameterKnob);
    }

    moveUpButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb2"));   // ▲
    moveDownButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbc")); // ▼
    moveUpButton.setTooltip ("Move this module one slot up (earlier in the chain)");
    moveDownButton.setTooltip ("Move this module one slot down (later in the chain)");
    moveUpButton.onClick = [this] { processor.swapSlots (slotIndex, slotIndex - 1); };
    moveDownButton.onClick = [this] { processor.swapSlots (slotIndex, slotIndex + 1); };
    moveUpButton.setEnabled (slotIndex > 0);
    moveDownButton.setEnabled (slotIndex < numSlots - 1);
    for (auto* button : { &moveUpButton, &moveDownButton })
    {
        button->setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));
        addAndMakeVisible (*button);
    }

    refreshFromProcessor();
}

void SlotRow::fillModuleMenu()
{
    // Menu item IDs are module type + 1, because JUCE reserves ID 0 for "nothing selected".
    moduleMenu.addItem (getModuleDescription (ModuleType::Empty).name, 1);
    auto previousCategory = ModuleCategory::None;
    for (int type = 1; type < numModuleTypes; ++type)
    {
        const auto& description = getModuleDescription (type);
        if (description.category != previousCategory)
        {
            moduleMenu.addSectionHeading (getCategoryName (description.category));
            previousCategory = description.category;
        }
        moduleMenu.addItem (description.name, type + 1);
    }
}

void SlotRow::showModule (int moduleType)
{
    displayedModuleType = moduleType;
    const auto& description = getModuleDescription (moduleType);
    const auto accentColour = Theme::accentColourFor (description.category);

    moduleMenu.setSelectedId (moduleType + 1, juce::dontSendNotification);
    moduleMenu.setTooltip (description.summary);
    moduleSummary.setText (moduleType == 0 ? juce::String ("Choose a module")
                                           : juce::String (getCategoryName (description.category)) +
                                                 juce::String::fromUTF8 ("  \xc2\xb7  ") + description.summary,
                           juce::dontSendNotification);
    powerSwitch.setColour (juce::ToggleButton::tickColourId, accentColour);

    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
    {
        auto& parameterKnob = parameterKnobs[parameterIndex];
        const auto& parameter = description.parameters[parameterIndex];
        parameterKnob.setVisible (parameter.isUsed());
        parameterKnob.nameLabel.setText (parameter.name, juce::dontSendNotification);
        parameterKnob.knob.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
        parameterKnob.knob.setDoubleClickReturnValue (parameter.isUsed(), getNormalisedDefault (parameter));
        parameterKnob.valueLabel.setText ({}, juce::dontSendNotification);
    }
    updateValueTexts();
    repaint();
}

void SlotRow::updateValueTexts()
{
    const auto& description = getModuleDescription (displayedModuleType);
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
    {
        const auto& parameter = description.parameters[parameterIndex];
        if (! parameter.isUsed())
            continue;
        const juce::String text (
            formatParameterValue (parameter, processor.getParameterValue (slotIndex, parameterIndex)));
        if (parameterKnobs[parameterIndex].valueLabel.getText() != text)
            parameterKnobs[parameterIndex].valueLabel.setText (text, juce::dontSendNotification);
    }
}

void SlotRow::refreshFromProcessor()
{
    const int moduleType = processor.getModuleType (slotIndex);
    if (moduleType != displayedModuleType)
        showModule (moduleType);

    const bool isEnabled = processor.isSlotEnabled (slotIndex);
    if (isEnabled != displayedAsEnabled)
    {
        displayedAsEnabled = isEnabled;
        for (auto& parameterKnob : parameterKnobs)
            parameterKnob.setAlpha (isEnabled ? 1.0f : 0.4f);
        moduleMenu.setAlpha (isEnabled ? 1.0f : 0.55f);
        repaint();
    }

    updateValueTexts();
}

void SlotRow::paint (juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat();
    const auto& description = getModuleDescription (std::max (0, displayedModuleType));
    const auto accentColour = Theme::accentColourFor (description.category);
    const bool isActive = displayedAsEnabled && displayedModuleType > 0;

    graphics.setColour (Theme::Colours::slotBackground);
    graphics.fillRoundedRectangle (area, 8.0f);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    // Category strip on the left edge
    graphics.setColour (isActive ? accentColour : accentColour.withAlpha (0.35f));
    graphics.fillRoundedRectangle (area.withWidth (5.0f).reduced (0.0f, 10.0f), 2.5f);

    // Slot number
    graphics.setColour (isActive ? Theme::Colours::primaryText : Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (20.0f, true));
    graphics.drawText (juce::String (slotIndex + 1), juce::Rectangle<float> (14.0f, 0.0f, 24.0f, area.getHeight()),
                       juce::Justification::centred);
}

void SlotRow::resized()
{
    auto area = getLocalBounds().reduced (8, 8);
    area.removeFromLeft (34); // slot number

    auto moveButtons = area.removeFromRight (26);
    moveUpButton.setBounds (moveButtons.removeFromTop (moveButtons.getHeight() / 2).reduced (0, 1));
    moveDownButton.setBounds (moveButtons.reduced (0, 1));
    area.removeFromRight (8);

    auto moduleArea = area.removeFromLeft (250);
    auto topLine = moduleArea.removeFromTop (28);
    powerSwitch.setBounds (topLine.removeFromLeft (40));
    topLine.removeFromLeft (4);
    moduleMenu.setBounds (topLine);
    moduleSummary.setBounds (moduleArea.withTrimmedLeft (40).withTrimmedTop (2));

    area.removeFromLeft (14);
    const int knobWidth = area.getWidth() / numParametersPerSlot;
    for (auto& parameterKnob : parameterKnobs)
        parameterKnob.setBounds (area.removeFromLeft (knobWidth).reduced (4, 0));
}

} // namespace midiforge
