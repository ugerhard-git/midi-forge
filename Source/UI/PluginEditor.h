/**
 * @file PluginEditor.h
 * @brief The plug-in window: header with page tabs, presets and panic button; the "Chain" page with the
 *        eight slot rows or the "Modulation" page with LFOs and routes; and a footer.
 */
#pragma once

#include "ForgeLookAndFeel.h"
#include "ModulationPage.h"
#include "SlotRow.h"

namespace midiforge
{

class MidiForgeEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MidiForgeEditor (MidiForgeProcessor& processor);
    ~MidiForgeEditor() override;

    void paint (juce::Graphics& graphics) override;
    void resized() override;

    /** The two pages of the window, switched with the tabs in the header. */
    enum class Page
    {
        chain,
        modulation
    };

    /** Shows one page and highlights its tab. */
    void showPage (Page page);
    Page getShownPage() const noexcept { return shownPage; }

    /** Brings the visible page and the preset menu up to date with the processor. Called by the timer. */
    void refreshFromProcessor();

private:
    /** Keeps rows and the preset menu in sync with the processor (automation, presets, undo). */
    void timerCallback() override;
    void paintFooter (juce::Graphics& graphics);

    static constexpr int refreshRateHz = 30;

    MidiForgeProcessor& processor;
    ForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ComboBox presetMenu;
    juce::TextButton panicButton { "Panic" };
    juce::TextButton chainTab { "Chain" }, modulationTab { "Modulation" };
    juce::Component chainPage; ///< Holds the slot rows
    std::vector<std::unique_ptr<SlotRow>> slotRows;
    ModulationPage modulationPage;
    Page shownPage = Page::chain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiForgeEditor)
};

} // namespace midiforge
