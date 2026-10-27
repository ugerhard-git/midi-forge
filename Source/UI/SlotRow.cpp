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
    // Long texts ("Last Note") are condensed a little instead of being cut off.
    nameLabel.setMinimumHorizontalScale (0.75f);
    valueLabel.setMinimumHorizontalScale (0.7f);
}

void ParameterKnob::resized()
{
    auto area = getLocalBounds();
    knob.setBounds (area.removeFromLeft (juce::jmin (area.getHeight(), 46))
                        .withSizeKeepingCentre (juce::jmin (area.getHeight(), 46), juce::jmin (area.getHeight(), 46)));
    area.removeFromLeft (4);
    nameLabel.setBounds (area.removeFromTop (area.getHeight() / 2).withTrimmedTop (2));
    valueLabel.setBounds (area.withTrimmedBottom (4));
}

void ParameterKnob::attachTo (juce::AudioProcessorValueTreeState& parameters, const juce::String& parameterId,
                              const ParameterDescription& description)
{
    parameterAttachment.reset();
    parameterAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, parameterId, knob);

    // The host parameter is 0..1; for stepped parameters the knob snaps to the steps.
    const bool hasSteps = description.isUsed() && description.kind != ParameterKind::Continuous;
    const int numSteps = hasSteps ? getNumberOfSteps (description) : 0;
    knob.setRange (0.0, 1.0, numSteps > 1 ? 1.0 / (numSteps - 1) : 0.0);
}

void ParameterKnob::setModulation (bool isModulated, float modulatedValue01)
{
    if (isModulated == showsModulation && std::abs (modulatedValue01 - modulatedValue) < 0.001f)
        return;
    showsModulation = isModulated;
    modulatedValue = modulatedValue01;
    repaint();
}

void ParameterKnob::paintOverChildren (juce::Graphics& graphics)
{
    if (! showsModulation || ! isVisible())
        return;

    // Same geometry as ForgeLookAndFeel::drawRotarySlider, one ring further out.
    const auto rotary = knob.getRotaryParameters();
    const auto knobArea = knob.getBounds().toFloat().reduced (3.0f);
    const auto centre = knobArea.getCentre();
    const float ringRadius = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) / 2.0f + 1.5f;
    const auto angleOf = [&] (float value01)
    { return rotary.startAngleRadians + value01 * (rotary.endAngleRadians - rotary.startAngleRadians); };

    const float baseAngle = angleOf ((float) knob.valueToProportionOfLength (knob.getValue()));
    const float modulatedAngle = angleOf (modulatedValue);

    juce::Path offset;
    offset.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, juce::jmin (baseAngle, modulatedAngle),
                          juce::jmax (baseAngle, modulatedAngle), true);
    graphics.setColour (Theme::Colours::modulation.withAlpha (0.55f));
    graphics.strokePath (offset, juce::PathStrokeType (1.5f));

    graphics.setColour (Theme::Colours::modulation);
    graphics.fillEllipse (
        juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre.getPointOnCircumference (ringRadius, modulatedAngle)));
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

    for (auto& parameterKnob : parameterKnobs)
        addChildComponent (parameterKnob);

    previousPageButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x80")); // ◀
    nextPageButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));     // ▶
    previousPageButton.setTooltip ("Knob page 1 (A-D)");
    nextPageButton.setTooltip ("Knob page 2 (E-H). A yellow dot means a knob on the other page is modulated.");
    previousPageButton.onClick = [this] { switchPage (-1); };
    nextPageButton.onClick = [this] { switchPage (+1); };
    for (auto* button : { &previousPageButton, &nextPageButton })
    {
        button->setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));
        addChildComponent (*button);
    }
    pageLabel.setFont (Theme::font (11.0f));
    pageLabel.setColour (juce::Label::textColourId, Theme::Colours::secondaryText);
    pageLabel.setJustificationType (juce::Justification::centred);
    pageLabel.setInterceptsMouseClicks (false, false);
    addChildComponent (pageLabel);

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
    // Modules are grouped by category; within a category they keep their ModuleType order, so modules
    // added in later versions (appended to ModuleType) still appear under their own category.
    moduleMenu.addItem (getModuleDescription (ModuleType::Empty).name, 1);
    for (auto category :
         { ModuleCategory::Filter, ModuleCategory::Transform, ModuleCategory::Generator, ModuleCategory::Randomizer })
    {
        moduleMenu.addSectionHeading (getCategoryName (category));
        for (int type = 1; type < numModuleTypes; ++type)
            if (getModuleDescription (type).category == category)
                moduleMenu.addItem (getModuleDescription (type).name, type + 1);
    }
}

void SlotRow::showModule (int moduleType, int page)
{
    displayedModuleType = moduleType;
    displayedPage = page;
    const auto& description = getModuleDescription (moduleType);
    const auto accentColour = Theme::accentColourFor (description.category);

    moduleMenu.setSelectedId (moduleType + 1, juce::dontSendNotification);
    moduleMenu.setTooltip (description.summary);
    moduleSummary.setText (moduleType == 0 ? juce::String ("Choose a module")
                                           : juce::String (getCategoryName (description.category)) +
                                                 juce::String::fromUTF8 ("  \xc2\xb7  ") + description.summary,
                           juce::dontSendNotification);
    powerSwitch.setColour (juce::ToggleButton::tickColourId, accentColour);

    const int numPages = getNumberOfParameterPages (description);
    for (auto* pageControl :
         std::initializer_list<juce::Component*> { &previousPageButton, &nextPageButton, &pageLabel })
        pageControl->setVisible (numPages > 1);
    previousPageButton.setEnabled (page > 0);
    nextPageButton.setEnabled (page < numPages - 1);
    pageLabel.setText (juce::String (page + 1) + "/" + juce::String (numPages), juce::dontSendNotification);

    for (int knobIndex = 0; knobIndex < numParametersPerPage; ++knobIndex)
    {
        auto& parameterKnob = parameterKnobs[knobIndex];
        const int parameterIndex = parameterIndexOfKnob (knobIndex);
        const auto& parameter = description.parameters[parameterIndex];
        parameterKnob.attachTo (processor.parameters, ParameterIds::moduleParameter (slotIndex, parameterIndex),
                                parameter);
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
    for (int knobIndex = 0; knobIndex < numParametersPerPage; ++knobIndex)
    {
        const int parameterIndex = parameterIndexOfKnob (knobIndex);
        const auto& parameter = description.parameters[parameterIndex];
        if (! parameter.isUsed())
            continue;
        const juce::String text (
            formatParameterValue (parameter, processor.getParameterValue (slotIndex, parameterIndex)));
        if (parameterKnobs[knobIndex].valueLabel.getText() != text)
            parameterKnobs[knobIndex].valueLabel.setText (text, juce::dontSendNotification);
    }
}

void SlotRow::refreshFromProcessor()
{
    const int moduleType = processor.getModuleType (slotIndex);
    const int page = processor.getShownPage (slotIndex);
    if (moduleType != displayedModuleType || page != displayedPage)
        showModule (moduleType, page);

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
    updateModulationRings();
}

void SlotRow::updateModulationRings()
{
    const auto& description = getModuleDescription (displayedModuleType);
    for (int knobIndex = 0; knobIndex < numParametersPerPage; ++knobIndex)
    {
        const int parameterIndex = parameterIndexOfKnob (knobIndex);
        const bool isModulated = displayedAsEnabled && description.parameters[parameterIndex].isUsed() &&
                                 processor.isParameterModulated (slotIndex, parameterIndex);
        parameterKnobs[knobIndex].setModulation (isModulated,
                                                 processor.getModulatedParameterValue (slotIndex, parameterIndex));
    }

    // The dot on the arrow towards the hidden page
    const bool hiddenPageModulated =
        displayedAsEnabled && getNumberOfParameterPages (description) > 1 && isPageModulated (1 - displayedPage);
    if (hiddenPageModulated != showsHiddenPageDot)
    {
        showsHiddenPageDot = hiddenPageModulated;
        repaint();
    }
}

bool SlotRow::isPageModulated (int page) const
{
    const auto& description = getModuleDescription (displayedModuleType);
    for (int knobIndex = 0; knobIndex < numParametersPerPage; ++knobIndex)
    {
        const int parameterIndex = page * numParametersPerPage + knobIndex;
        if (description.parameters[parameterIndex].isUsed() &&
            processor.isParameterModulated (slotIndex, parameterIndex))
            return true;
    }
    return false;
}

void SlotRow::switchPage (int direction)
{
    processor.setShownPage (slotIndex, displayedPage + direction);
    refreshFromProcessor();
}

void SlotRow::paintOverChildren (juce::Graphics& graphics)
{
    if (! showsHiddenPageDot)
        return;
    const auto& arrow = displayedPage == 0 ? nextPageButton : previousPageButton;
    graphics.setColour (Theme::Colours::modulation);
    graphics.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f)
                              .withCentre ({ (float) arrow.getRight() - 3.0f, (float) arrow.getY() + 3.0f }));
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

    auto moduleArea = area.removeFromLeft (228);
    auto topLine = moduleArea.removeFromTop (28);
    powerSwitch.setBounds (topLine.removeFromLeft (40));
    topLine.removeFromLeft (4);
    moduleMenu.setBounds (topLine);
    moduleSummary.setBounds (moduleArea.withTrimmedLeft (40).withTrimmedTop (2));

    // Page arrows (only shown for modules with two pages), left of the move buttons
    auto pageArea = area.removeFromRight (48);
    pageLabel.setBounds (pageArea.removeFromTop (18));
    auto arrows = pageArea.withSizeKeepingCentre (pageArea.getWidth(), 24);
    previousPageButton.setBounds (arrows.removeFromLeft (arrows.getWidth() / 2).reduced (1, 0));
    nextPageButton.setBounds (arrows.reduced (1, 0));
    area.removeFromRight (4);

    area.removeFromLeft (10);
    const int knobWidth = area.getWidth() / numParametersPerPage;
    for (auto& parameterKnob : parameterKnobs)
        parameterKnob.setBounds (area.removeFromLeft (knobWidth).reduced (4, 0));
}

} // namespace midiforge
