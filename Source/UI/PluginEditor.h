/**
 * @file PluginEditor.h
 * @brief The plug-in window: header with page tabs, presets and panic button; the "Chain" page with the
 *        eight slot rows or the "Modulation" page with LFOs and routes; and a footer.
 */
#pragma once

#include "ForgeLookAndFeel.h"
#include "ModulationPage.h"
#include "PresetBar.h"
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
    /** The Modulation tab (for switching its sub-tabs, e.g. from the tests). */
    ModulationPage& getModulationPage() noexcept { return modulationPage; }

    /** Brings the visible page and the preset menu up to date with the processor. Called by the timer. */
    void refreshFromProcessor();

    /** @return True while the "Removed … · Undo" strip is shown in the footer. */
    bool isUndoStripVisible() const noexcept { return undoButton.isVisible(); }
    /** How long the undo strip stays after a module was removed. */
    static constexpr juce::uint32 undoStripMilliseconds = 8000;

private:
    /** Keeps rows and the preset menu in sync with the processor (automation, presets, undo). */
    void timerCallback() override;
    void paintFooter (juce::Graphics& graphics);
    /** Shows the undo strip after a removal and hides it when it expires or can no longer undo. */
    void updateUndoStrip();
    void showUndoStrip (bool shouldBeShown);

    static constexpr int refreshRateHz = 30;

    MidiForgeProcessor& processor;
    ForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    PresetBar presetBar;
    juce::TextButton panicButton { "Panic" };
    juce::TextButton chainTab { "Chain" }, modulationTab { "Modulation" };
    juce::Component chainPage; ///< Holds the slot rows
    std::vector<std::unique_ptr<SlotRow>> slotRows;
    ModulationPage modulationPage;
    Page shownPage = Page::chain;

    juce::Label undoMessage;
    juce::TextButton undoButton { "Undo" };
    int seenModuleRemovalCount = 0;
    juce::uint32 undoStripShownSince = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiForgeEditor)
};

} // namespace midiforge
