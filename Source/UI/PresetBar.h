/**
 * @file PresetBar.h
 * @brief The preset controls in the header: ◀ preset menu ▶, Save, and a "⋯" menu with
 *        Save As, Rename, Delete, Import and Show folder.
 */
#pragma once

#include "Plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace midiforge
{

class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (MidiForgeProcessor& processor);
    ~PresetBar() override;

    void resized() override;

    /**
     * Shows the current preset name (with "*" when modified) and picks up presets added or removed
     * outside the plug-in. Call regularly from a timer.
     */
    void refreshFromProcessor();

    /** Rebuilds the menu from the factory presets and the files in the preset folder. */
    void rebuildMenu();

private:
    /** Menu item IDs: factory presets from 1, user presets from userPresetIdOffset + 1. */
    static constexpr int userPresetIdOffset = 1000;

    void loadChosenPreset();
    void saveClicked();
    void showMoreMenu();
    void askForName (const juce::String& title, const juce::String& message, const juce::String& initialName,
                     const juce::String& confirmButtonText, std::function<void (const juce::String&)> onConfirm);
    void askToConfirm (const juce::String& title, const juce::String& message, const juce::String& confirmButtonText,
                       std::function<void()> onConfirm);
    void showError (const juce::String& message);
    void saveAs();
    void saveWithName (const juce::String& name);
    void renameCurrent();
    void deleteCurrent();
    void importPreset();
    void presetChangedByUser();
    /** Remembers an open dialog, so it can be closed if the editor goes away first. */
    void trackDialog (juce::AlertWindow* dialog);

    MidiForgeProcessor& processor;
    juce::TextButton previousButton, nextButton, saveButton, moreButton;
    juce::ComboBox presetMenu;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<juce::Component::SafePointer<juce::AlertWindow>> openDialogs;

    juce::StringArray userPresetNamesInMenu;
    juce::String displayedText;
    int refreshesUntilFolderScan = 0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PresetBar)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

} // namespace midiforge
