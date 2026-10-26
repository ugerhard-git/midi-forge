/**
 * @file PluginEditor.cpp
 * @brief Implementation of MidiForgeEditor.
 */
#include "PluginEditor.h"

#include "Theme.h"

namespace midiforge
{

MidiForgeEditor::MidiForgeEditor (MidiForgeProcessor& processorToEdit)
    : AudioProcessorEditor (processorToEdit), processor (processorToEdit), modulationPage (processorToEdit)
{
    setLookAndFeel (&lookAndFeel);

    // Preset menu: item ID = preset index + 1 (ID 0 means "nothing selected" in JUCE)
    for (int presetIndex = 0; presetIndex < processor.getNumPrograms(); ++presetIndex)
        presetMenu.addItem (processor.getProgramName (presetIndex), presetIndex + 1);
    presetMenu.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetMenu.setTooltip ("Factory presets. Loading one replaces all eight slots.");
    presetMenu.onChange = [this]
    {
        const int presetIndex = presetMenu.getSelectedId() - 1;
        if (presetIndex >= 0)
        {
            processor.setCurrentProgram (presetIndex);
            processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
        }
    };
    addAndMakeVisible (presetMenu);

    panicButton.setTooltip ("Release all sounding notes and send All Notes Off on every channel");
    panicButton.setColour (juce::TextButton::buttonColourId, Theme::Colours::panicButton);
    panicButton.onClick = [this] { processor.panic(); };
    addAndMakeVisible (panicButton);

    for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex)
    {
        slotRows.push_back (std::make_unique<SlotRow> (processor, slotIndex));
        chainPage.addAndMakeVisible (*slotRows.back());
    }
    addAndMakeVisible (chainPage);
    addChildComponent (modulationPage);

    // Page tabs: two toggle buttons in one radio group
    constexpr int pageTabsRadioGroup = 1001;
    for (auto* tab : { &chainTab, &modulationTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (pageTabsRadioGroup);
        tab->setColour (juce::TextButton::buttonColourId, Theme::Colours::windowBackground);
        tab->setColour (juce::TextButton::buttonOnColourId, Theme::Colours::selectedTab);
        tab->setColour (juce::TextButton::textColourOffId, Theme::Colours::secondaryText);
        tab->setColour (juce::TextButton::textColourOnId, Theme::Colours::primaryText);
        addAndMakeVisible (*tab);
    }
    chainTab.setTooltip ("The eight slots that MIDI flows through");
    modulationTab.setTooltip ("LFOs and the routes that let them move slot parameters");
    chainTab.onClick = [this] { showPage (Page::chain); };
    modulationTab.onClick = [this] { showPage (Page::modulation); };
    showPage (Page::chain);

    using namespace Theme::Layout;
    setSize (editorWidth, headerHeight + numSlots * (slotHeight + gapBetweenSlots) + footerHeight);
    startTimerHz (refreshRateHz);
}

MidiForgeEditor::~MidiForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MidiForgeEditor::paint (juce::Graphics& graphics)
{
    using namespace Theme::Layout;
    graphics.fillAll (Theme::Colours::windowBackground);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (outerMargin, 0);
    graphics.setColour (Theme::Colours::primaryText);
    graphics.setFont (Theme::font (24.0f, true));
    graphics.drawText ("MIDI FORGE", header.removeFromLeft (170), juce::Justification::centredLeft);

    paintFooter (graphics);
}

void MidiForgeEditor::paintFooter (juce::Graphics& graphics)
{
    using namespace Theme::Layout;
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (outerMargin, 0);

    graphics.setFont (Theme::font (11.5f));
    graphics.setColour (Theme::Colours::secondaryText);
    graphics.drawText ("Events flow from slot 1 to slot 8. Double-click a knob to reset it.", footer,
                       juce::Justification::centredLeft);
    graphics.drawText ("v" JucePlugin_VersionString, footer, juce::Justification::centredRight);

    // Colour legend of the module categories
    auto legend = footer.withTrimmedRight (60).removeFromRight (360);
    for (auto category :
         { ModuleCategory::Filter, ModuleCategory::Transform, ModuleCategory::Generator, ModuleCategory::Randomizer })
    {
        auto legendItem = legend.removeFromLeft (90);
        graphics.setColour (Theme::accentColourFor (category));
        graphics.fillEllipse (legendItem.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
        legendItem.removeFromLeft (5);
        graphics.setColour (Theme::Colours::secondaryText);
        graphics.drawText (getCategoryName (category), legendItem, juce::Justification::centredLeft);
    }
}

void MidiForgeEditor::resized()
{
    using namespace Theme::Layout;
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (outerMargin, 16);
    header.removeFromLeft (180); // title
    chainTab.setBounds (header.removeFromLeft (96));
    header.removeFromLeft (6);
    modulationTab.setBounds (header.removeFromLeft (110));
    panicButton.setBounds (header.removeFromRight (76));
    header.removeFromRight (10);
    presetMenu.setBounds (header.removeFromRight (220));

    area.removeFromBottom (footerHeight);
    area = area.reduced (outerMargin, 0);
    chainPage.setBounds (area);
    modulationPage.setBounds (area.withTrimmedBottom (gapBetweenSlots));

    auto rowsArea = chainPage.getLocalBounds();
    for (auto& row : slotRows)
    {
        row->setBounds (rowsArea.removeFromTop (slotHeight));
        rowsArea.removeFromTop (gapBetweenSlots);
    }
}

void MidiForgeEditor::showPage (Page page)
{
    shownPage = page;
    chainPage.setVisible (page == Page::chain);
    modulationPage.setVisible (page == Page::modulation);
    chainTab.setToggleState (page == Page::chain, juce::dontSendNotification);
    modulationTab.setToggleState (page == Page::modulation, juce::dontSendNotification);
}

void MidiForgeEditor::timerCallback()
{
    refreshFromProcessor();
}

void MidiForgeEditor::refreshFromProcessor()
{
    // Only the visible page needs updating.
    if (shownPage == Page::chain)
        for (auto& row : slotRows)
            row->refreshFromProcessor();
    else
        modulationPage.refreshFromProcessor();

    const int currentPresetId = processor.getCurrentProgram() + 1;
    if (presetMenu.getSelectedId() != currentPresetId)
        presetMenu.setSelectedId (currentPresetId, juce::dontSendNotification);
}

} // namespace midiforge
