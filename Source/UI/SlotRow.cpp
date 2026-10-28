/**
 * @file SlotRow.cpp
 * @brief Implementation of ParameterKnob and SlotRow.
 */
#include "SlotRow.h"

#include "ModuleBrowser.h"
#include "Plugin/ParameterIds.h"
#include "Theme.h"

#include <cmath>

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

    triggerButton.setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack);
    triggerButton.onStateChange = [this] { triggerButtonStateChanged(); };
    addChildComponent (triggerButton);
}

void ParameterKnob::triggerButtonStateChanged()
{
    const bool isDown = triggerButton.isDown();
    if (! isTrigger || attachedParameter == nullptr || isDown == isTriggerDown)
        return;
    isTriggerDown = isDown;
    if (isDown)
        attachedParameter->beginChangeGesture();
    attachedParameter->setValueNotifyingHost (isDown ? 1.0f : 0.0f);
    if (! isDown)
        attachedParameter->endChangeGesture();
}

void ParameterKnob::resized()
{
    auto area = getLocalBounds();
    knob.setBounds (area.removeFromLeft (juce::jmin (area.getHeight(), 46))
                        .withSizeKeepingCentre (juce::jmin (area.getHeight(), 46), juce::jmin (area.getHeight(), 46)));
    area.removeFromLeft (4);
    nameLabel.setBounds (area.removeFromTop (area.getHeight() / 2).withTrimmedTop (2));
    valueLabel.setBounds (area.withTrimmedBottom (4));
    triggerButton.setBounds (getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth(), 90), 26));
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

    // Trigger parameters are a button: on while held down.
    attachedParameter = parameters.getParameter (parameterId);
    isTrigger = description.kind == ParameterKind::Trigger;
    isTriggerDown = false;
    triggerButton.setButtonText (description.name);
    triggerButton.setTooltip (description.helpText);
    triggerButton.setVisible (isTrigger);
    for (auto* component : std::initializer_list<juce::Component*> { &knob, &nameLabel, &valueLabel })
        component->setVisible (! isTrigger);
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
    if (isTrigger)
    {
        // A modulated trigger: a yellow outline around the button.
        graphics.setColour (Theme::Colours::modulation.withAlpha (0.8f));
        graphics.drawRoundedRectangle (triggerButton.getBounds().toFloat().expanded (1.5f), 5.0f, 1.2f);
        return;
    }

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

    // The menu items only provide the text shown in the closed menu; picking happens in the module browser.
    fillModuleMenu();
    moduleMenu.onOpenBrowser = [this] { openModuleBrowser(); };
    addAndMakeVisible (moduleMenu);

    moduleInfoButton.setButtonText ("i");
    moduleInfoButton.setTooltip ("What this module does and what each knob means");
    moduleInfoButton.setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));
    moduleInfoButton.onClick = [this] { openModuleInfo(); };
    addChildComponent (moduleInfoButton);

    removeModuleButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97")); // ×
    removeModuleButton.setTooltip ("Remove this module. The modules below move up; you can undo it.");
    removeModuleButton.setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));
    removeModuleButton.onClick = [this] { processor.removeModule (slotIndex); };
    addChildComponent (removeModuleButton);

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

juce::Component* SlotRow::findCallOutParent() const
{
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        return editor;
    return getTopLevelComponent();
}

void SlotRow::openModuleBrowser()
{
    auto browser =
        std::make_unique<ModuleBrowser> (processor.getModuleType (slotIndex), recentModules->list.getModuleTypes(),
                                         [safeThis = juce::Component::SafePointer<SlotRow> (this)] (int type)
                                         {
                                             if (safeThis != nullptr)
                                                 safeThis->moduleChosenInBrowser (type);
                                         });
    auto* parent = findCallOutParent();
    const auto pointAt =
        parent != nullptr ? parent->getLocalArea (this, moduleMenu.getBounds()) : moduleMenu.getScreenBounds();
    juce::CallOutBox::launchAsynchronously (std::move (browser), pointAt, parent);
}

void SlotRow::openModuleInfo()
{
    auto info = std::make_unique<ModuleInfoView>();
    info->showModule (processor.getModuleType (slotIndex));
    constexpr int infoWidth = 380;
    info->setSize (infoWidth, info->getHeightForWidth (infoWidth));
    auto* parent = findCallOutParent();
    const auto pointAt = parent != nullptr ? parent->getLocalArea (this, moduleInfoButton.getBounds())
                                           : moduleInfoButton.getScreenBounds();
    juce::CallOutBox::launchAsynchronously (std::move (info), pointAt, parent);
}

void SlotRow::moduleChosenInBrowser (int moduleType)
{
    if (moduleType < 0 || moduleType >= numModuleTypes)
        return;
    if (moduleType != processor.getModuleType (slotIndex))
        processor.loadModule (slotIndex, moduleType);
    recentModules->list.moduleWasPicked (moduleType);
    refreshFromProcessor();
}

void SlotRow::showModule (int moduleType, int page)
{
    displayedModuleType = moduleType;
    displayedPage = page;
    const auto& description = getModuleDescription (moduleType);
    const auto accentColour = Theme::accentColourFor (description.category);

    moduleMenu.setSelectedId (moduleType + 1, juce::dontSendNotification);
    moduleMenu.setTooltip ("Click to pick a module (you can search by typing)");
    moduleInfoButton.setVisible (moduleType != 0);
    removeModuleButton.setVisible (moduleType != 0);
    moduleSummary.setTooltip (description.details);
    // The category is already shown by the colour; the full text is in the ⓘ pop-up and the tooltip.
    moduleSummary.setText (moduleType == 0 ? juce::String ("Choose a module") : juce::String (description.summary),
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
        parameterKnob.knob.setTooltip (parameter.helpText);
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
    float ownValues[numParametersPerSlot] {};
    for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        ownValues[parameterIndex] = processor.getParameterValue (slotIndex, parameterIndex);
    for (int knobIndex = 0; knobIndex < numParametersPerPage; ++knobIndex)
    {
        const int parameterIndex = parameterIndexOfKnob (knobIndex);
        const auto& parameter = description.parameters[parameterIndex];
        if (! parameter.isUsed())
            continue;
        // Knobs that do nothing with the current settings of the module are dimmed.
        const float knobAlpha = isParameterInactive (displayedModuleType, parameterIndex, ownValues) ? 0.35f : 1.0f;
        if (std::abs (parameterKnobs[knobIndex].getAlpha() - knobAlpha) > 0.01f)
            parameterKnobs[knobIndex].setAlpha (knobAlpha);
        // A modulated knob shows the value the module uses right now (in yellow), except while the user
        // points at or turns it: then it shows the user's own setting.
        auto& parameterKnob = parameterKnobs[knobIndex];
        const bool showsLiveValue = displayedAsEnabled && processor.isEngineRunning() &&
                                    processor.isParameterModulated (slotIndex, parameterIndex) &&
                                    ! parameterKnob.isMouseOverOrDragging (true);
        const float shownValue = showsLiveValue ? processor.getModulatedParameterValue (slotIndex, parameterIndex)
                                                : processor.getParameterValue (slotIndex, parameterIndex);
        const juce::String text (formatParameterValue (parameter, shownValue));
        if (parameterKnob.valueLabel.getText() != text)
            parameterKnob.valueLabel.setText (text, juce::dontSendNotification);
        const auto colour = showsLiveValue ? Theme::Colours::modulation : Theme::Colours::primaryText;
        if (parameterKnob.valueLabel.findColour (juce::Label::textColourId) != colour)
            parameterKnob.valueLabel.setColour (juce::Label::textColourId, colour);
    }
}

void SlotRow::refreshFromProcessor()
{
    const int moduleType = processor.getModuleType (slotIndex);
    const int page = processor.getShownPage (slotIndex);
    if (moduleType != displayedModuleType || page != displayedPage)
        showModule (moduleType, page);

    // With a modulated switch, the row shows the state the chain is using right now.
    const bool isSwitchModulated = processor.isSlotSwitchModulated (slotIndex);
    const bool isEnabled =
        isSwitchModulated ? processor.isSlotEnabledAfterModulation (slotIndex) : processor.isSlotEnabled (slotIndex);
    if (isSwitchModulated != showsSwitchModulation)
    {
        showsSwitchModulation = isSwitchModulated;
        repaint();
    }
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
    if (showsSwitchModulation)
    {
        // A yellow outline around the power switch, with a dot when modulation has the slot on right now.
        const auto switchArea = powerSwitch.getBounds().toFloat().reduced (1.0f, 5.0f);
        graphics.setColour (Theme::Colours::modulation.withAlpha (0.7f));
        graphics.drawRoundedRectangle (switchArea, switchArea.getHeight() / 2.0f, 1.2f);
        const auto dotArea = juce::Rectangle<float> (6.0f, 6.0f).withCentre (switchArea.getTopRight());
        graphics.setColour (Theme::Colours::modulation);
        if (displayedAsEnabled)
            graphics.fillEllipse (dotArea);
        else
            graphics.drawEllipse (dotArea, 1.2f);
    }

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

    auto moduleArea = area.removeFromLeft (248);
    auto topLine = moduleArea.removeFromTop (28);
    powerSwitch.setBounds (topLine.removeFromLeft (40));
    topLine.removeFromLeft (4);
    removeModuleButton.setBounds (topLine.removeFromRight (24).reduced (0, 2));
    topLine.removeFromRight (3);
    moduleInfoButton.setBounds (topLine.removeFromRight (24).reduced (0, 2));
    topLine.removeFromRight (4);
    moduleMenu.setBounds (topLine);
    moduleSummary.setBounds (moduleArea.withTrimmedLeft (2).withTrimmedTop (2));

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
