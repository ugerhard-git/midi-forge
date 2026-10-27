/**
 * @file ModulationPage.h
 * @brief The "Modulation" tab: four LFO panels, four Note Random panels and the eight modulation routes.
 */
#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

/** A small rotary knob with its name and value underneath, attached to one host parameter. */
class CompactKnob final : public juce::Component
{
public:
    CompactKnob (juce::AudioProcessorValueTreeState& parameters, const juce::String& parameterId,
                 const juce::String& name);
    void resized() override;
    /** Updates the value text from the parameter. */
    void refresh();

    juce::Slider knob;

private:
    juce::Label nameLabel, valueLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

/** Draws an LFO's wave shape and a dot at its current position. */
class WaveformPreview final : public juce::Component
{
public:
    void paint (juce::Graphics& graphics) override;
    /** Updates what is drawn; repaints only when something changed. */
    void show (const LfoSettings& settings, float currentPhase, float currentValue);

private:
    LfoSettings shownSettings;
    float phase = 0.0f;
    float value = 0.0f;
};

/** All controls of one LFO. */
class LfoPanel final : public juce::Component
{
public:
    LfoPanel (MidiForgeProcessor& processor, int lfoIndex);
    void paint (juce::Graphics& graphics) override;
    void resized() override;
    /** Updates the preview, value texts and which controls apply. Call regularly from a timer. */
    void refreshFromProcessor();

private:
    MidiForgeProcessor& processor;
    const int lfoIndex;

    WaveformPreview preview;
    juce::ComboBox waveformMenu, syncModeMenu;
    // Created after the menus are filled, so the attachments can select the right item.
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment, syncModeAttachment;
    CompactKnob rateKnob, divisionKnob, phaseKnob, pulseWidthKnob;
    juce::ToggleButton restartSwitch;
    juce::AudioProcessorValueTreeState::ButtonAttachment restartAttachment;
    juce::Label restartLabel;
};

/** All controls of one Note Random modulator, and its current value. */
class NoteRandomPanel final : public juce::Component
{
public:
    NoteRandomPanel (MidiForgeProcessor& processor, int randomIndex);
    void paint (juce::Graphics& graphics) override;
    void resized() override;
    /** Updates the value display. Call regularly from a timer. */
    void refreshFromProcessor();

private:
    MidiForgeProcessor& processor;
    const int randomIndex;
    float displayedValue = 0.0f;

    CompactKnob windowKnob;
    juce::ComboBox triggerMenu, stepsMenu;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> triggerAttachment, stepsAttachment;
};

/** One modulation route: source (LFO or Random) -> target parameter, with polarity and amount. */
class RouteRow final : public juce::Component
{
public:
    RouteRow (MidiForgeProcessor& processor, int routeIndex);
    void paint (juce::Graphics& graphics) override;
    void resized() override;
    /** Keeps the target names in step with the modules in the slots. Call regularly from a timer. */
    void refreshFromProcessor();

private:
    MidiForgeProcessor& processor;
    const int routeIndex;

    juce::ComboBox sourceMenu, targetMenu;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, targetAttachment;
    juce::TextButton polarityButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment polarityAttachment;
    juce::Slider amountSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment amountAttachment;
    juce::Label amountLabel;
};

/** The whole "Modulation" tab. */
class ModulationPage final : public juce::Component
{
public:
    explicit ModulationPage (MidiForgeProcessor& processor);
    void paint (juce::Graphics& graphics) override;
    void resized() override;
    void refreshFromProcessor();

private:
    std::vector<std::unique_ptr<LfoPanel>> lfoPanels;
    std::vector<std::unique_ptr<NoteRandomPanel>> noteRandomPanels;
    std::vector<std::unique_ptr<RouteRow>> routeRows;
    juce::Rectangle<int> routesArea;
};

} // namespace midiforge
