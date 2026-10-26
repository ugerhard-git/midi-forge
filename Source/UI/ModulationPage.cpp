/**
 * @file ModulationPage.cpp
 * @brief Implementation of the "Modulation" tab: CompactKnob, WaveformPreview, LfoPanel, RouteRow, ModulationPage.
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
    area.removeFromTop (26); // title
    preview.setBounds (area.removeFromTop (70));
    area.removeFromTop (8);
    waveformMenu.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    auto syncLine = area.removeFromTop (26);
    syncModeMenu.setBounds (syncLine.removeFromLeft (78));
    syncLine.removeFromLeft (6);
    restartSwitch.setBounds (syncLine.removeFromLeft (34));
    restartLabel.setBounds (syncLine);
    area.removeFromTop (6);

    const int knobWidth = area.getWidth() / 3;
    auto knobLine = area.removeFromTop (84);
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
// RouteRow

RouteRow::RouteRow (MidiForgeProcessor& processorToControl, int routeIndexToShow)
    : processor (processorToControl), routeIndex (routeIndexToShow),
      amountAttachment (processor.parameters, ParameterIds::routeAmount (routeIndex), amountSlider)
{
    fillWithChoices (sourceMenu, processor.parameters.getParameter (ParameterIds::routeSource (routeIndex)));
    for (int targetIndex = 0; targetIndex < numModulationTargets; ++targetIndex)
        targetMenu.addItem (processor.getModulationTargetName (targetIndex), targetIndex + 1);
    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::routeSource (routeIndex), sourceMenu);
    targetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.parameters, ParameterIds::routeTarget (routeIndex), targetMenu);
    sourceMenu.setTooltip ("Which LFO drives this route");
    targetMenu.setTooltip ("Which slot parameter the LFO moves");
    addAndMakeVisible (sourceMenu);
    addAndMakeVisible (targetMenu);

    amountSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    amountSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amountSlider.setDoubleClickReturnValue (true, 50.0);
    amountSlider.setTooltip ("How far the LFO moves the parameter. 100% sweeps the whole range; "
                             "negative values invert the LFO. Double-click: 50%.");
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
    area.removeFromLeft (20);
    amountLabel.setBounds (area.removeFromRight (52));
    area.removeFromRight (8);
    amountSlider.setBounds (area);
}

void RouteRow::refreshFromProcessor()
{
    // Target names follow the modules loaded in the slots ("Slot 2 · Gate").
    for (int targetIndex = 0; targetIndex < numModulationTargets; ++targetIndex)
    {
        const auto name = processor.getModulationTargetName (targetIndex);
        if (targetMenu.getItemText (targetIndex) != name)
            targetMenu.changeItemText (targetIndex + 1, name);
    }
    if (targetMenu.getText() != targetMenu.getItemText (targetMenu.getSelectedItemIndex()))
        targetMenu.setText (targetMenu.getItemText (targetMenu.getSelectedItemIndex()), juce::dontSendNotification);

    const auto amountText = amountSlider.getTextFromValue (amountSlider.getValue());
    if (amountLabel.getText() != amountText)
        amountLabel.setText (amountText, juce::dontSendNotification);

    const bool isActive = targetMenu.getSelectedItemIndex() > 0;
    for (auto* component : std::initializer_list<juce::Component*> { &amountSlider, &amountLabel, &sourceMenu })
        component->setAlpha (isActive ? 1.0f : 0.45f);
}

//==============================================================================
// ModulationPage

ModulationPage::ModulationPage (MidiForgeProcessor& processor)
{
    for (int lfoIndex = 0; lfoIndex < numLfos; ++lfoIndex)
    {
        lfoPanels.push_back (std::make_unique<LfoPanel> (processor, lfoIndex));
        addAndMakeVisible (*lfoPanels.back());
    }
    for (int routeIndex = 0; routeIndex < numModulationRoutes; ++routeIndex)
    {
        routeRows.push_back (std::make_unique<RouteRow> (processor, routeIndex));
        addAndMakeVisible (*routeRows.back());
    }
}

void ModulationPage::paint (juce::Graphics& graphics)
{
    const auto area = routesArea.toFloat();
    graphics.setColour (Theme::Colours::slotBackground);
    graphics.fillRoundedRectangle (area, 8.0f);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

    auto titleArea = routesArea.reduced (12, 8).removeFromTop (20);
    graphics.setColour (Theme::Colours::primaryText);
    graphics.setFont (Theme::font (15.0f, true));
    graphics.drawText ("Modulation routes", titleArea, juce::Justification::centredLeft);
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.setFont (Theme::font (11.5f));
    graphics.drawText ("LFO moves a slot parameter around its knob value by the amount", titleArea,
                       juce::Justification::centredRight);
}

void ModulationPage::resized()
{
    auto area = getLocalBounds();
    constexpr int gap = 10;

    auto lfoArea = area.removeFromTop (266);
    const int panelWidth = (lfoArea.getWidth() - gap * (numLfos - 1)) / numLfos;
    for (auto& panel : lfoPanels)
    {
        panel->setBounds (lfoArea.removeFromLeft (panelWidth));
        lfoArea.removeFromLeft (gap);
    }

    area.removeFromTop (gap);
    routesArea = area;
    auto rowsArea = area.reduced (12, 8);
    rowsArea.removeFromTop (28); // title
    const int rowHeight = juce::jmin (34, rowsArea.getHeight() / numModulationRoutes);
    for (auto& row : routeRows)
        row->setBounds (rowsArea.removeFromTop (rowHeight));
}

void ModulationPage::refreshFromProcessor()
{
    for (auto& panel : lfoPanels)
        panel->refreshFromProcessor();
    for (auto& row : routeRows)
        row->refreshFromProcessor();
}

} // namespace midiforge
