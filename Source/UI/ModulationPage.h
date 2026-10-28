/**
 * @file ModulationPage.h
 * @brief The "Modulation" tab: sub-tabs for the LFO, Note Random and step modulator panels, and the eight
 *        modulation routes below them.
 */
#pragma once

#include "Plugin/PluginProcessor.h"
#include "StepModulatorPanel.h"

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
    /**
     * Shows the value a route has moved this setting to (in the modulation colour) instead of the knob's own
     * value; while the mouse is over the knob its own value is shown. An empty text shows the own value.
     */
    void showModulatedValue (const juce::String& modulatedText);

    juce::Slider knob;

private:
    juce::Label nameLabel, valueLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::String modulatedValueText; ///< Shown instead of the own value while the setting is modulated
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

/**
 * One modulation route: source (LFO, Random or Steps) -> target (a slot knob or switch, or a setting of the
 * modulation itself), with polarity and amount.
 */
class RouteRow final : public juce::Component
{
public:
    RouteRow (MidiForgeProcessor& processor, int routeIndex);
    void paint (juce::Graphics& graphics) override;
    void resized() override;
    /** Keeps the target menu in step with the modules in the slots. Call regularly from a timer. */
    void refreshFromProcessor();

private:
    /**
     * Fills the target menu: "None", then per slot a heading and the knobs its module uses, sorted by
     * slot and knob, then a "Modulation" heading with sub-menus for route amounts, LFO and step settings.
     * Item IDs are target numbers + 1 (the menu order differs from the numbering).
     */
    void rebuildTargetMenu();
    int currentTargetIndex() const;

    MidiForgeProcessor& processor;
    const int routeIndex;

    juce::ComboBox sourceMenu, targetMenu;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    std::unique_ptr<juce::ParameterAttachment> targetAttachment;
    std::array<int, numSlots> moduleTypesInTargetMenu {};
    int targetInMenu = -1;
    juce::TextButton polarityButton;
    juce::AudioProcessorValueTreeState::ButtonAttachment polarityAttachment;
    juce::Slider amountSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment amountAttachment;
    juce::Label amountLabel;
};

/**
 * The whole "Modulation" tab. The sources are on three sub-tabs (LFOs, Random, Steps), so each kind gets
 * the full width; the routes stay visible below, six at a time on two pages (1–6, 7–12). A yellow dot on a
 * sub-tab means one of its sources is routed somewhere; on a route page button, that one of its routes is on.
 */
class ModulationPage final : public juce::Component
{
public:
    explicit ModulationPage (MidiForgeProcessor& processor);
    void paint (juce::Graphics& graphics) override;
    void paintOverChildren (juce::Graphics& graphics) override;
    void resized() override;
    void refreshFromProcessor();

    /** The sub-tabs of the sources area. */
    enum class SourcePage
    {
        lfos,
        randoms,
        steps
    };
    /** Shows one sub-tab's panels and highlights its tab. */
    void showSourcePage (SourcePage page);
    SourcePage getShownSourcePage() const noexcept { return shownSourcePage; }
    /** Routes shown at a time; the two route pages show routes 1–6 and 7–12. */
    static constexpr int routesPerPage = 6;
    static constexpr int numRoutePages = numModulationRoutes / routesPerPage;
    /** Shows one page of routes (0: routes 1–6, 1: routes 7–12) and highlights its button. */
    void showRoutePage (int page);
    int getShownRoutePage() const noexcept { return shownRoutePage; }
    /** @return The panel of a step modulator (for tests). */
    StepModulatorPanel& getStepModulatorPanel (int index) { return *stepModulatorPanels[(size_t) index]; }

private:
    /** @return True if any source of a sub-tab is routed somewhere. */
    bool isAnySourceInUse (SourcePage page) const;
    /** @return True if a route on a route page has a target and an amount. */
    bool isAnyRouteInUse (int routePage) const;

    MidiForgeProcessor& processor;
    juce::TextButton lfoTab { "LFOs" }, randomTab { "Random" }, stepsTab { "Steps" };
    SourcePage shownSourcePage = SourcePage::lfos;
    std::array<bool, 3> shownInUseDots {};

    std::vector<std::unique_ptr<LfoPanel>> lfoPanels;
    std::vector<std::unique_ptr<NoteRandomPanel>> noteRandomPanels;
    std::vector<std::unique_ptr<StepModulatorPanel>> stepModulatorPanels;
    std::vector<std::unique_ptr<RouteRow>> routeRows;
    std::array<juce::TextButton, numRoutePages> routePageButtons;
    int shownRoutePage = 0;
    std::array<bool, numRoutePages> shownRoutePageDots {};
    juce::Rectangle<int> routesArea;
};

} // namespace midiforge
