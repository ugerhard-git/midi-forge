/**
 * @file SlotRow.h
 * @brief One row of the editor: power switch, module menu with ⓘ button, four knobs (with page arrows for
 *        modules that have more than four parameters) and move buttons for one slot.
 */
#pragma once

#include "Plugin/PluginProcessor.h"
#include "Plugin/RecentModuleList.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

/**
 * A rotary knob with the parameter name and its current value next to it.
 * When an LFO modulates the parameter, a dot on an outer ring shows the value the chain actually uses.
 */
class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob();
    void resized() override;
    void paintOverChildren (juce::Graphics& graphics) override;

    /**
     * Shows or hides the modulation ring.
     * @param isModulated          True if an active route targets this parameter.
     * @param modulatedValue01     The value (0..1) after modulation.
     */
    void setModulation (bool isModulated, float modulatedValue01);

    /**
     * Connects the knob to a host parameter. Knobs for parameters with fixed steps (choices, whole numbers)
     * jump from step to step while dragging; the mouse wheel moves one step per notch.
     */
    void attachTo (juce::AudioProcessorValueTreeState& parameters, const juce::String& parameterId,
                   const ParameterDescription& description);

    juce::Slider knob;
    juce::Label nameLabel;
    juce::Label valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> parameterAttachment;

private:
    bool showsModulation = false;
    float modulatedValue = 0.0f;
};

/**
 * Looks like a drop-down menu, but opens the searchable ModuleBrowser instead of a plain menu
 * (by mouse click or by pressing Return / Space while it has the focus).
 */
class ModuleMenu final : public juce::ComboBox
{
public:
    std::function<void()> onOpenBrowser;
    void showPopup() override
    {
        if (onOpenBrowser != nullptr)
            onOpenBrowser();
    }
};

/**
 * Shows and edits one slot. The row polls the processor (refreshFromProcessor) instead of listening,
 * so changes from host automation, presets and other rows all show up the same way.
 *
 * Four knobs are visible at a time. Modules with more than four parameters show ◀ ▶ arrows to switch
 * between page 1 (A-D) and page 2 (E-H); a yellow dot on an arrow means an LFO or Random modulator
 * moves a knob on the page it leads to.
 *
 * The module menu opens the ModuleBrowser; the ⓘ button next to it shows the module's full description
 * and what every knob does.
 */
class SlotRow final : public juce::Component
{
public:
    SlotRow (MidiForgeProcessor& processor, int slotIndex);

    void paint (juce::Graphics& graphics) override;
    void paintOverChildren (juce::Graphics& graphics) override;
    void resized() override;

    /** Updates the row from the processor's current parameter values. Call regularly from a timer. */
    void refreshFromProcessor();

    /** Opens the module browser below the module menu. */
    void openModuleBrowser();
    /** Opens the info pop-up of the loaded module next to the ⓘ button. */
    void openModuleInfo();
    /** Loads a module picked in the module browser and remembers it as recently used. */
    void moduleChosenInBrowser (int moduleType);

private:
    void fillModuleMenu();
    /** @return The editor this row is in (call-out boxes are shown inside it), or nullptr. */
    juce::Component* findCallOutParent() const;
    void showModule (int moduleType, int page);
    void updateValueTexts();
    void updateModulationRings();
    void switchPage (int direction);
    /** @return The slot parameter index (0..7) shown by knob 0..3 on the current page. */
    int parameterIndexOfKnob (int knobIndex) const noexcept { return displayedPage * numParametersPerPage + knobIndex; }
    /** @return True if a used parameter on the given page is moved by a modulation route. */
    bool isPageModulated (int page) const;

    MidiForgeProcessor& processor;
    const int slotIndex;
    int displayedModuleType = -1;
    int displayedPage = 0;
    bool displayedAsEnabled = true;
    bool showsHiddenPageDot = false;

    juce::ToggleButton powerSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerSwitchAttachment;
    ModuleMenu moduleMenu;
    juce::TextButton moduleInfoButton;
    juce::Label moduleSummary;
    juce::SharedResourcePointer<SharedRecentModuleList> recentModules;
    ParameterKnob parameterKnobs[numParametersPerPage];
    juce::TextButton previousPageButton, nextPageButton;
    juce::Label pageLabel;
    juce::TextButton moveUpButton, moveDownButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotRow)
};

} // namespace midiforge
