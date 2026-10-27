/**
 * @file ModulationPage.cpp
 * @brief Implementation of the "Modulation" tab: CompactKnob, WaveformPreview, LfoPanel, NoteRandomPanel,
 *        RouteRow, ModulationPage.
 */
#include "ModulationPage.h"

#include "Plugin/ParameterIds.h"
#include "Theme.h"

namespace midiforge
{

namespace
{
/** Fills a menu with the choices of a host parameter, item ID = choice index + 1. */
void fillWithChoices (juce::ComboBox& menu, juce::RangedAudioParameter* parameter)
{
    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*> (parameter))
        for (int index = 0; index < choiceParameter->choices.size(); ++index)
            menu.addItem (choiceParameter->choices[index], index + 1);
}

void styleSmallLabel (juce::Label& label, juce::Colour colour, float height, bool isBold = false)
{
    label.setFont (Theme::font (height, isBold));
    label.setColour (juce::Label::textColourId, colour);
    label.setJustificationType (juce::Justification::centred);
    label.setInterceptsMouseClicks (false, false);
}
} // namespace

//==============================================================================
// CompactKnob

CompactKnob::CompactKnob (juce::AudioProcessorValueTreeState& parameters, const juce::String& parameterId,
                          const juce::String& name)
    : attachment (parameters, parameterId, knob)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setMouseDragSensitivity (180);
    knob.setColour (juce::Slider::rotarySliderFillColourId, Theme::Colours::modulation);
    if (auto* parameter = parameters.getParameter (parameterId))
        knob.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    addAndMakeVisible (knob);

    nameLabel.setText (name, juce::dontSendNotification);
    styleSmallLabel (nameLabel, Theme::Colours::secondaryText, 11.0f);
    styleSmallLabel (valueLabel, Theme::Colours::primaryText, 12.5f, true);
    addAndMakeVisible (nameLabel);
    addAndMakeVisible (valueLabel);
    refresh();
}

void CompactKnob::resized()
{
    auto area = getLocalBounds();
    valueLabel.setBounds (area.removeFromBottom (16));
    nameLabel.setBounds (area.removeFromBottom (14));
    knob.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth(), area.getHeight()), area.getHeight()));
}

void CompactKnob::refresh()
{
    const auto text = knob.getTextFromValue (knob.getValue());
    if (valueLabel.getText() != text)
        valueLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
// WaveformPreview

void WaveformPreview::show (const LfoSettings& settings, float currentPhase, float currentValue)
{
    const bool shapeChanged = settings.waveform != shownSettings.waveform ||
                              std::abs (settings.pulseWidth - shownSettings.pulseWidth) > 1.0e-6;
    if (! shapeChanged && std::abs (currentPhase - phase) < 1.0e-4f && std::abs (currentValue - value) < 1.0e-4f)
        return;
    shownSettings = settings;
    phase = currentPhase;
    value = currentValue;
    repaint();
}

void WaveformPreview::paint (juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat();
    graphics.setColour (Theme::Colours::windowBackground);
    graphics.fillRoundedRectangle (area, 5.0f);

    const auto plot = area.reduced (6.0f, 8.0f);
    const auto yFor = [&] (float lfoValue) { return plot.getCentreY() - lfoValue * plot.getHeight() / 2.0f; };

    graphics.setColour (Theme::Colours::controlTrack);
    graphics.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

    // One cycle of the wave. Random waves show an example shape (their real values are unpredictable).
    static const Lfo previewLfo (7);
    juce::Path wave;
    constexpr int numPoints = 160;
    for (int point = 0; point <= numPoints; ++point)
    {
        const double cyclePosition = (double) point / numPoints;
        const bool isRandomWave =
            shownSettings.waveform == LfoWaveform::SampleAndHold || shownSettings.waveform == LfoWaveform::SmoothRandom;
        // Random waves: show four of their steps so the character is visible
        const double shownPosition = isRandomWave ? cyclePosition * 4.0 : cyclePosition;
        const float lfoValue =
            previewLfo.waveformValue (shownSettings.waveform, shownPosition, shownSettings.pulseWidth);
        const float x = plot.getX() + (float) cyclePosition * plot.getWidth();
        if (point == 0)
            wave.startNewSubPath (x, yFor (lfoValue));
        else
            wave.lineTo (x, yFor (lfoValue));
    }
    graphics.setColour (Theme::Colours::modulation.withAlpha (0.8f));
    graphics.strokePath (wave, juce::PathStrokeType (1.6f));

    // Where the LFO is now
    const float dotX = plot.getX() + phase * plot.getWidth();
    graphics.setColour (Theme::Colours::modulation.withAlpha (0.25f));
    graphics.drawVerticalLine ((int) dotX, plot.getY(), plot.getBottom());
    graphics.setColour (juce::Colours::white);
    graphics.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ dotX, yFor (value) }));
}

//==============================================================================
// LfoPanel

LfoPanel::LfoPanel (MidiForgeProcessor& processorToControl, int lfoIndexToShow)
    : processor (processorToControl), lfoIndex (lfoIndexToShow),
      rateKnob (processor.parameters, ParameterIds::lfoRate (lfoIndex), "Rate"),
      divisionKnob (processor.parameters, ParameterIds::lfoDivision (lfoIndex), "Rate"),
      phaseKnob (processor.parameters, ParameterIds::lfoPhase (lfoIndex), "Phase"),
      pulseWidthKnob (processor.parameters, ParameterIds::lfoPulseWidth (lfoIndex), "Width"),
      restartAttachment (processor.parameters, ParameterIds::lfoRestartOnKey (lfoIndex), restartSwitch)
{
    addAndMakeVisible (preview);

    fillWithChoices (waveformMenu, processor.parameters.getParameter (ParameterIds::lfoWaveform (lfoIndex)));
    fillWithChoices (syncModeMenu, processor.parameters.getParameter (ParameterIds::lfoSyncMode (lfoIndex)));
    waveformAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::lfoWaveform (lfoIndex), waveformMenu);
    syncModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::lfoSyncMode (lfoIndex), syncModeMenu);
    waveformMenu.setTooltip ("Wave shape. S&H jumps to a new random value every cycle; Smooth Random glides.");
    syncModeMenu.setTooltip ("Tempo: cycle length in bars and notes, locked to the bar grid while the DAW plays. "
                             "Free: rate in Hz.");
    addAndMakeVisible (waveformMenu);
    addAndMakeVisible (syncModeMenu);

    for (auto* compactKnob : { &rateKnob, &divisionKnob, &phaseKnob, &pulseWidthKnob })
        addAndMakeVisible (*compactKnob);
    pulseWidthKnob.knob.setTooltip ("Share of the cycle the Square wave is high (Square only)");
    phaseKnob.knob.setTooltip ("Shifts the wave within its cycle, e.g. to offset two LFOs against each other");

    restartSwitch.setColour (juce::ToggleButton::tickColourId, Theme::Colours::modulation);
    restartSwitch.setTooltip ("Restart the LFO when you press the first key after all keys were released");
    addAndMakeVisible (restartSwitch);
    restartLabel.setText ("Restart on key", juce::dontSendNotification);
    styleSmallLabel (restartLabel, Theme::Colours::secondaryText, 11.0f);
    restartLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (restartLabel);

    refreshFromProcessor();
}

void LfoPanel::paint (juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat();
    graphics.setColour (Theme::Colours::slotBackground);
    graphics.fillRoundedRectangle (area, 8.0f);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto titleArea = getLocalBounds().reduced (10, 8).removeFromTop (20);
    graphics.setColour (Theme::Colours::primaryText);
    graphics.setFont (Theme::font (15.0f, true));
    graphics.drawText ("LFO " + juce::String (lfoIndex + 1), titleArea, juce::Justification::centredLeft);

    // Current output as a number, -100..+100
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (11.5f));
    const int outputPercent = juce::roundToInt (processor.getLfoValue (lfoIndex) * 100.0f);
    graphics.drawText ((outputPercent > 0 ? "+" : "") + juce::String (outputPercent), titleArea,
                       juce::Justification::centredRight);
}

void LfoPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 8);
    area.removeFromTop (22); // title
    preview.setBounds (area.removeFromTop (42));
    area.removeFromTop (6);
    waveformMenu.setBounds (area.removeFromTop (24));
    area.removeFromTop (5);

    auto syncLine = area.removeFromTop (24);
    syncModeMenu.setBounds (syncLine.removeFromLeft (78));
    syncLine.removeFromLeft (6);
    restartSwitch.setBounds (syncLine.removeFromLeft (34));
    restartLabel.setBounds (syncLine);
    area.removeFromTop (4);

    const int knobWidth = area.getWidth() / 3;
    auto knobLine = area;
    const auto rateArea = knobLine.removeFromLeft (knobWidth);
    rateKnob.setBounds (rateArea);
    divisionKnob.setBounds (rateArea);
    phaseKnob.setBounds (knobLine.removeFromLeft (knobWidth));
    pulseWidthKnob.setBounds (knobLine);
}

void LfoPanel::refreshFromProcessor()
{
    const auto settings = processor.getLfoSettings (lfoIndex);
    preview.show (settings, processor.getLfoPhase (lfoIndex), processor.getLfoValue (lfoIndex));

    rateKnob.setVisible (! settings.isTempoSynced);
    divisionKnob.setVisible (settings.isTempoSynced);
    const bool usesPulseWidth = settings.waveform == LfoWaveform::Square;
    pulseWidthKnob.setEnabled (usesPulseWidth);
    pulseWidthKnob.setAlpha (usesPulseWidth ? 1.0f : 0.35f);

    for (auto* compactKnob : { &rateKnob, &divisionKnob, &phaseKnob, &pulseWidthKnob })
        compactKnob->refresh();
    repaint (getLocalBounds().removeFromTop (30)); // the output number in the title line
}

//==============================================================================
// NoteRandomPanel

NoteRandomPanel::NoteRandomPanel (MidiForgeProcessor& processorToControl, int randomIndexToShow)
    : processor (processorToControl), randomIndex (randomIndexToShow),
      windowKnob (processor.parameters, ParameterIds::noteRandomWindow (randomIndex), "Chord window")
{
    windowKnob.knob.setTooltip ("Notes played within this time after the first one count as one chord "
                                "and share its random value");
    addAndMakeVisible (windowKnob);

    fillWithChoices (triggerMenu, processor.parameters.getParameter (ParameterIds::noteRandomTrigger (randomIndex)));
    fillWithChoices (stepsMenu, processor.parameters.getParameter (ParameterIds::noteRandomSteps (randomIndex)));
    triggerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::noteRandomTrigger (randomIndex), triggerMenu);
    stepsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::noteRandomSteps (randomIndex), stepsMenu);
    triggerMenu.setTooltip ("New value on every played note, or only on the first note of a phrase "
                            "(a key pressed while no other key is held)");
    stepsMenu.setTooltip ("Smooth: any value. 2-12: only that many evenly spaced values "
                          "(good for knobs with choices, like Scale or Chord type)");
    addAndMakeVisible (triggerMenu);
    addAndMakeVisible (stepsMenu);
}

void NoteRandomPanel::paint (juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat();
    graphics.setColour (Theme::Colours::slotBackground);
    graphics.fillRoundedRectangle (area, 8.0f);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto titleArea = getLocalBounds().reduced (10, 6).removeFromTop (18);
    graphics.setColour (Theme::Colours::primaryText);
    graphics.setFont (Theme::font (14.0f, true));
    graphics.drawText ("Random " + juce::String (randomIndex + 1), titleArea, juce::Justification::centredLeft);

    // Current value as a number and as a bar growing from the centre
    const int valuePercent = juce::roundToInt (displayedValue * 100.0f);
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (11.5f));
    graphics.drawText ((valuePercent > 0 ? "+" : "") + juce::String (valuePercent), titleArea,
                       juce::Justification::centredRight);

    const auto meter = titleArea.withTrimmedLeft (78).withTrimmedRight (36).toFloat().withSizeKeepingCentre (
        (float) titleArea.withTrimmedLeft (78).withTrimmedRight (36).getWidth(), 4.0f);
    graphics.setColour (Theme::Colours::controlTrack);
    graphics.fillRoundedRectangle (meter, 2.0f);
    const float centreX = meter.getCentreX();
    const float valueX = centreX + displayedValue * meter.getWidth() / 2.0f;
    graphics.setColour (Theme::Colours::modulation);
    graphics.fillRoundedRectangle (
        juce::Rectangle<float>::leftTopRightBottom (juce::jmin (centreX, valueX), meter.getY(),
                                                    juce::jmax (centreX, valueX), meter.getBottom()),
        2.0f);
}

void NoteRandomPanel::resized()
{
    auto area = getLocalBounds().reduced (10, 6);
    area.removeFromTop (22); // title and value
    windowKnob.setBounds (area.removeFromLeft (76));
    area.removeFromLeft (6);
    area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 56));
    const int menuHeight = (area.getHeight() - 6) / 2;
    triggerMenu.setBounds (area.removeFromTop (menuHeight));
    area.removeFromTop (6);
    stepsMenu.setBounds (area.removeFromTop (menuHeight));
}

void NoteRandomPanel::refreshFromProcessor()
{
    windowKnob.refresh();
    const float value = processor.getNoteRandomValue (randomIndex);
    if (std::abs (value - displayedValue) > 1.0e-4f)
    {
        displayedValue = value;
        repaint (getLocalBounds().removeFromTop (30));
    }
}

//==============================================================================
// RouteRow

RouteRow::RouteRow (MidiForgeProcessor& processorToControl, int routeIndexToShow)
    : processor (processorToControl), routeIndex (routeIndexToShow),
      polarityAttachment (processor.parameters, ParameterIds::routePolarity (routeIndex), polarityButton),
      amountAttachment (processor.parameters, ParameterIds::routeAmount (routeIndex), amountSlider)
{
    fillWithChoices (sourceMenu, processor.parameters.getParameter (ParameterIds::routeSource (routeIndex)));
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::routeSource (routeIndex), sourceMenu);

    // The target menu is ordered by slot, not by target number, so it gets its own attachment.
    targetAttachment = std::make_unique<juce::ParameterAttachment> (
        *processor.parameters.getParameter (ParameterIds::routeTarget (routeIndex)),
        [this] (float targetIndex)
        {
            if (targetMenu.indexOfItemId (juce::roundToInt (targetIndex) + 1) < 0)
                rebuildTargetMenu();
            targetMenu.setSelectedId (juce::roundToInt (targetIndex) + 1, juce::dontSendNotification);
        });
    targetMenu.onChange = [this]
    {
        if (const int chosenId = targetMenu.getSelectedId(); chosenId > 0)
            targetAttachment->setValueAsCompleteGesture ((float) (chosenId - 1));
    };
    rebuildTargetMenu();
    targetAttachment->sendInitialUpdate();
    sourceMenu.setTooltip ("Which LFO or Random modulator drives this route");
    targetMenu.setTooltip ("Which slot parameter the source moves");

    polarityButton.setClickingTogglesState (true);
    polarityButton.setTooltip (juce::String::fromUTF8 (
        "Polarity. \xc2\xb1 Bipolar: the value swings below and above the knob position. "
        "+ Unipolar: it is only pushed up from the knob position (or down with a negative amount); "
        "sine and triangle LFOs then start their cycle at the lowest point."));
    polarityButton.setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));
    polarityButton.setColour (juce::TextButton::buttonOnColourId, Theme::Colours::controlTrack.darker (0.2f));
    polarityButton.setColour (juce::TextButton::textColourOnId, Theme::Colours::modulation);
    addAndMakeVisible (polarityButton);
    addAndMakeVisible (sourceMenu);
    addAndMakeVisible (targetMenu);

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amountSlider.setDoubleClickReturnValue (true, 50.0);
    amountSlider.setTooltip ("How far the source moves the parameter. 100% sweeps the whole range; "
                             "negative values invert it. Double-click: 50%.");
    amountSlider.getProperties().set (Theme::bipolarSliderProperty, true);
    addAndMakeVisible (amountSlider);

    styleSmallLabel (amountLabel, Theme::Colours::primaryText, 12.5f, true);
    amountLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (amountLabel);

    refreshFromProcessor();
}

void RouteRow::paint (juce::Graphics& graphics)
{
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (13.0f, true));
    graphics.drawText (juce::String (routeIndex + 1), getLocalBounds().removeFromLeft (20),
                       juce::Justification::centred);

    // Arrow from source to target
    const auto arrowArea = juce::Rectangle<float> ((float) sourceMenu.getRight(), 0.0f, 18.0f, (float) getHeight());
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.drawArrow (
        { arrowArea.getX() + 3.0f, arrowArea.getCentreY(), arrowArea.getRight() - 3.0f, arrowArea.getCentreY() }, 1.2f,
        6.0f, 6.0f);
}

void RouteRow::resized()
{
    auto area = getLocalBounds().reduced (0, 3);
    area.removeFromLeft (22); // route number
    sourceMenu.setBounds (area.removeFromLeft (96));
    area.removeFromLeft (18); // arrow
    targetMenu.setBounds (area.removeFromLeft (240));
    area.removeFromLeft (10);
    polarityButton.setBounds (area.removeFromLeft (30));
    area.removeFromLeft (12);
    amountLabel.setBounds (area.removeFromRight (52));
    area.removeFromRight (8);
    amountSlider.setBounds (area);
}

int RouteRow::currentTargetIndex() const
{
    return juce::roundToInt (
        processor.parameters.getParameter (ParameterIds::routeTarget (routeIndex))
            ->convertFrom0to1 (processor.parameters.getParameter (ParameterIds::routeTarget (routeIndex))->getValue()));
}

void RouteRow::rebuildTargetMenu()
{
    const int selectedTarget = currentTargetIndex();
    targetMenu.clear (juce::dontSendNotification);
    targetMenu.addItem ("None", 1);

    bool selectedTargetListed = selectedTarget == 0;
    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        moduleTypesInTargetMenu[(size_t) slotIndex] = processor.getModuleType (slotIndex);
        const auto& description = getModuleDescription (moduleTypesInTargetMenu[(size_t) slotIndex]);
        if (getNumberOfUsedParameters (description) == 0)
            continue;

        targetMenu.addSectionHeading ("Slot " + juce::String (slotIndex + 1) + juce::String::fromUTF8 (" \xc2\xb7 ") +
                                      description.name);
        for (int parameterIndex = 0; parameterIndex < numParametersPerSlot; ++parameterIndex)
        {
            if (! description.parameters[parameterIndex].isUsed())
                continue;
            const int targetIndex = modulationTargetIndex (slotIndex, parameterIndex);
            targetMenu.addItem (processor.getModulationTargetName (targetIndex), targetIndex + 1);
            selectedTargetListed = selectedTargetListed || targetIndex == selectedTarget;
        }
        // The slot's on/off switch comes after its knobs.
        const int switchTarget = modulationTargetIndex (slotIndex, slotSwitchParameterIndex);
        targetMenu.addItem (processor.getModulationTargetName (switchTarget), switchTarget + 1);
        selectedTargetListed = selectedTargetListed || switchTarget == selectedTarget;
    }

    // A route can point at a knob the current module does not use (e.g. after changing the module): keep it visible.
    if (! selectedTargetListed)
    {
        targetMenu.addSectionHeading ("Unused");
        targetMenu.addItem (processor.getModulationTargetName (selectedTarget), selectedTarget + 1);
    }

    targetInMenu = selectedTarget;
    targetMenu.setSelectedId (selectedTarget + 1, juce::dontSendNotification);
}

void RouteRow::refreshFromProcessor()
{
    // The menu follows the modules loaded in the slots ("Slot 2 · Gate").
    bool modulesChanged = currentTargetIndex() != targetInMenu;
    for (int slotIndex = 0; slotIndex < numSlots && ! modulesChanged; ++slotIndex)
        modulesChanged = processor.getModuleType (slotIndex) != moduleTypesInTargetMenu[(size_t) slotIndex];
    if (modulesChanged && ! targetMenu.isPopupActive())
        rebuildTargetMenu();

    const auto polarityText = juce::String::fromUTF8 (polarityButton.getToggleState() ? "+" : "\xc2\xb1");
    if (polarityButton.getButtonText() != polarityText)
        polarityButton.setButtonText (polarityText);

    const auto amountText = amountSlider.getTextFromValue (amountSlider.getValue());
    if (amountLabel.getText() != amountText)
        amountLabel.setText (amountText, juce::dontSendNotification);

    const bool isActive = targetMenu.getSelectedItemIndex() > 0;
    for (auto* component :
         std::initializer_list<juce::Component*> { &amountSlider, &amountLabel, &sourceMenu, &polarityButton })
        component->setAlpha (isActive ? 1.0f : 0.45f);
}

//==============================================================================
// ModulationPage

ModulationPage::ModulationPage (MidiForgeProcessor& processorToControl) : processor (processorToControl)
{
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        lfoPanels.push_back (std::make_unique<LfoPanel> (processor, lfoIndex));
        addChildComponent (*lfoPanels.back());
    }
    for (int randomIndex = 0; randomIndex < numNoteRandoms; ++randomIndex)
    {
        noteRandomPanels.push_back (std::make_unique<NoteRandomPanel> (processor, randomIndex));
        addChildComponent (*noteRandomPanels.back());
    }
    for (int stepIndex = 0; stepIndex < numStepModulators; ++stepIndex)
    {
        stepModulatorPanels.push_back (std::make_unique<StepModulatorPanel> (processor, stepIndex));
        addChildComponent (*stepModulatorPanels.back());
    }
    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        routeRows.push_back (std::make_unique<RouteRow> (processor, routeIndex));
        addAndMakeVisible (*routeRows.back());
    }

    // The sub-tabs: three toggle buttons in one radio group.
    constexpr int sourceTabsRadioGroup = 2001;
    const std::pair<juce::TextButton*, SourcePage> tabs[] = { { &lfoTab, SourcePage::lfos },
                                                              { &randomTab, SourcePage::randoms },
                                                              { &stepsTab, SourcePage::steps } };
    for (const auto& [tab, page] : tabs)
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (sourceTabsRadioGroup);
        tab->setColour (juce::TextButton::buttonColourId, Theme::Colours::windowBackground);
        tab->setColour (juce::TextButton::buttonOnColourId, Theme::Colours::selectedTab);
        tab->setColour (juce::TextButton::textColourOffId, Theme::Colours::secondaryText);
        tab->setColour (juce::TextButton::textColourOnId, Theme::Colours::primaryText);
        tab->onClick = [this, sourcePage = page] { showSourcePage (sourcePage); };
        addAndMakeVisible (*tab);
    }
    lfoTab.setTooltip ("The four LFOs");
    randomTab.setTooltip ("The four Random modulators: a new value per played note or phrase");
    stepsTab.setTooltip ("The four step modulators: sequences of 4 to 16 values in time with the song");
    showSourcePage (SourcePage::lfos);
}

void ModulationPage::showSourcePage (SourcePage page)
{
    shownSourcePage = page;
    for (auto& panel : lfoPanels)
        panel->setVisible (page == SourcePage::lfos);
    for (auto& panel : noteRandomPanels)
        panel->setVisible (page == SourcePage::randoms);
    for (auto& panel : stepModulatorPanels)
        panel->setVisible (page == SourcePage::steps);
    lfoTab.setToggleState (page == SourcePage::lfos, juce::dontSendNotification);
    randomTab.setToggleState (page == SourcePage::randoms, juce::dontSendNotification);
    stepsTab.setToggleState (page == SourcePage::steps, juce::dontSendNotification);
}

bool ModulationPage::isAnySourceInUse (SourcePage page) const
{
    const auto [first, count] = page == SourcePage::lfos ? std::pair<int, int> { 0, numLfos }
                                : page == SourcePage::randoms
                                    ? std::pair<int, int> { firstNoteRandomSource, numNoteRandoms }
                                    : std::pair<int, int> { firstStepModulatorSource, numStepModulators };
    for (int sourceIndex = first; sourceIndex < first + count; ++sourceIndex)
        if (processor.isSourceInUse (sourceIndex))
            return true;
    return false;
}

void ModulationPage::paint (juce::Graphics& graphics)
{
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (11.0f));
    graphics.drawText (juce::String::fromUTF8 ("Sources move slot knobs and switches through the routes below "
                                               "\xc2\xb7 a dot: a source on that tab is in use"),
                       getLocalBounds().removeFromTop (28).withTrimmedLeft (stepsTab.getRight() + 12),
                       juce::Justification::centredRight);

    const auto area = routesArea.toFloat();
    graphics.setColour (Theme::Colours::slotBackground);
    graphics.fillRoundedRectangle (area, 8.0f);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto titleArea = routesArea.reduced (12, 6).removeFromTop (18);
    graphics.setColour (Theme::Colours::primaryText);
    graphics.setFont (Theme::font (14.0f, true));
    graphics.drawText ("Modulation routes", titleArea, juce::Justification::centredLeft);
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (11.5f));
    graphics.drawText (
        juce::String::fromUTF8 ("A source moves a slot knob by the amount:  \xc2\xb1 both ways,  + only one way"),
        titleArea, juce::Justification::centredRight);
}

void ModulationPage::paintOverChildren (juce::Graphics& graphics)
{
    // The "in use" dots on the sub-tabs
    const juce::TextButton* tabs[] = { &lfoTab, &randomTab, &stepsTab };
    graphics.setColour (Theme::Colours::modulation);
    for (size_t index = 0; index < 3; ++index)
        if (shownInUseDots[index])
            graphics.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f)
                                      .withCentre ({ (float) tabs[index]->getRight() - 9.0f,
                                                     (float) tabs[index]->getBounds().getCentreY() }));
}

void ModulationPage::resized()
{
    auto area = getLocalBounds();
    constexpr int gap = 8;

    auto tabLine = area.removeFromTop (28);
    for (auto* tab : { &lfoTab, &randomTab, &stepsTab })
    {
        tab->setBounds (tabLine.removeFromLeft (86).reduced (0, 1));
        tabLine.removeFromLeft (6);
    }
    area.removeFromTop (6);

    // Each sub-tab uses the whole sources area.
    auto sourcesArea = area.removeFromTop (300);
    area.removeFromTop (gap);

    const auto layOutRow = [gap] (juce::Rectangle<int> rowArea, auto begin, auto end)
    {
        const int numPanels = (int) std::distance (begin, end);
        const int panelWidth = (rowArea.getWidth() - gap * (numPanels - 1)) / numPanels;
        for (auto panel = begin; panel != end; ++panel)
        {
            (*panel)->setBounds (rowArea.removeFromLeft (panelWidth));
            rowArea.removeFromLeft (gap);
        }
    };
    const auto layOutGrid = [&layOutRow, gap, sourcesArea] (auto& panels)
    {
        auto gridArea = sourcesArea;
        const int rowHeight = (gridArea.getHeight() - gap) / 2;
        layOutRow (gridArea.removeFromTop (rowHeight), panels.begin(), panels.begin() + 2);
        gridArea.removeFromTop (gap);
        layOutRow (gridArea, panels.begin() + 2, panels.end());
    };
    layOutRow (sourcesArea, lfoPanels.begin(), lfoPanels.end());
    layOutGrid (noteRandomPanels);
    layOutGrid (stepModulatorPanels);

    routesArea = area;
    auto rowsArea = area.reduced (12, 4);
    rowsArea.removeFromTop (22); // title
    const int rowHeight = juce::jmin (34, rowsArea.getHeight() / numModulationRoutes);
    for (auto& row : routeRows)
        row->setBounds (rowsArea.removeFromTop (rowHeight));
}

void ModulationPage::refreshFromProcessor()
{
    // Only the visible panels need updating.
    if (shownSourcePage == SourcePage::lfos)
        for (auto& panel : lfoPanels)
            panel->refreshFromProcessor();
    if (shownSourcePage == SourcePage::randoms)
        for (auto& panel : noteRandomPanels)
            panel->refreshFromProcessor();
    if (shownSourcePage == SourcePage::steps)
        for (auto& panel : stepModulatorPanels)
            panel->refreshFromProcessor();
    for (auto& row : routeRows)
        row->refreshFromProcessor();

    const std::array<bool, 3> inUse { isAnySourceInUse (SourcePage::lfos), isAnySourceInUse (SourcePage::randoms),
                                      isAnySourceInUse (SourcePage::steps) };
    if (inUse != shownInUseDots)
    {
        shownInUseDots = inUse;
        repaint();
    }
}

} // namespace midiforge
