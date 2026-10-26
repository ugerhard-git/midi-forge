/**
 * @file PluginEditor.h
 * @brief The plug-in window: header with presets and panic button, eight slot rows, and a footer.
 */
#pragma once

#include "ForgeLookAndFeel.h"
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

private:
    /** Keeps rows and the preset menu in sync with the processor (automation, presets, undo). */
    void timerCallback() override;
    void paintFooter (juce::Graphics& graphics);

    static constexpr int refreshRateHz = 20;

    MidiForgeProcessor& processor;
    ForgeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::ComboBox presetMenu;
    juce::TextButton panicButton { "Panic" };
    std::vector<std::unique_ptr<SlotRow>> slotRows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiForgeEditor)
};

} // namespace midiforge
