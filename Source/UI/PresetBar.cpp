/**
 * @file PresetBar.cpp
 * @brief Implementation of PresetBar. Dialogs are asynchronous, as plug-ins must not block the host.
 */
#include "PresetBar.h"

#include "Theme.h"

namespace midiforge
{

namespace
{
/** How often the preset folder is re-read while the editor is open (in timer ticks of ~33 ms). */
constexpr int refreshesBetweenFolderScans = 60;

enum MoreMenuItem
{
    saveAsItem = 1,
    renameItem,
    deleteItem,
    importItem,
    showFolderItem,
    aboutItem
};

/** Result of the About dialog's "Source code" button (namespace scope, so lambdas can use it uncaptured). */
constexpr int openSourcePageResult = 2;
} // namespace

juce::String getAboutText()
{
    return juce::String ("MIDI Forge ") + JucePlugin_VersionString +
           "\nCopyright (C) 2026 Udo Gerhards\n\n"
           "This program is free software: you can redistribute it and/or modify it under the terms of the GNU "
           "Affero General Public License, version 3. It comes with ABSOLUTELY NO WARRANTY.\n\n"
           "Source code: " +
           sourceCodeUrl + "\n\nBuilt with JUCE, used under the AGPLv3.";
}

PresetBar::PresetBar (MidiForgeProcessor& processorToControl) : processor (processorToControl)
{
    previousButton.setButtonText (juce::String::fromUTF8 ("\xe2\x97\x80")); // ◀
    nextButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));     // ▶
    saveButton.setButtonText ("Save");
    moreButton.setButtonText (juce::String::fromUTF8 ("\xe2\x8b\xaf")); // ⋯

    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset");
    saveButton.setTooltip (
        "Save your own preset. Overwrites the current one if it is yours, otherwise asks for a name.");
    moreButton.setTooltip ("Save As, Rename, Delete, Import, Show folder");
    presetMenu.setTooltip ("Factory presets and your own presets. Loading one replaces all slots, LFOs and routes. "
                           "A * means the settings differ from the saved preset.");

    previousButton.onClick = [this]
    {
        processor.stepPreset (-1);
        presetChangedByUser();
    };
    nextButton.onClick = [this]
    {
        processor.stepPreset (+1);
        presetChangedByUser();
    };
    saveButton.onClick = [this] { saveClicked(); };
    moreButton.onClick = [this] { showMoreMenu(); };
    presetMenu.onChange = [this] { loadChosenPreset(); };

    for (auto* button : { &previousButton, &nextButton, &saveButton, &moreButton })
    {
        button->setColour (juce::TextButton::buttonColourId, Theme::Colours::controlTrack.darker (0.2f));
        addAndMakeVisible (*button);
    }
    addAndMakeVisible (presetMenu);

    rebuildMenu();
}

PresetBar::~PresetBar()
{
    // Close dialogs that are still open (e.g. the host closes the plug-in window); their callbacks
    // hold weak references to this bar and do nothing once it is gone.
    for (auto& dialog : openDialogs)
        if (dialog != nullptr)
            dialog->exitModalState (0);
}

void PresetBar::trackDialog (juce::AlertWindow* dialog)
{
    openDialogs.erase (std::remove_if (openDialogs.begin(), openDialogs.end(),
                                       [] (const auto& existing) { return existing == nullptr; }),
                       openDialogs.end());
    openDialogs.emplace_back (dialog);
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    moreButton.setBounds (area.removeFromRight (30));
    area.removeFromRight (4);
    saveButton.setBounds (area.removeFromRight (54));
    area.removeFromRight (8);
    previousButton.setBounds (area.removeFromLeft (26));
    area.removeFromLeft (4);
    nextButton.setBounds (area.removeFromRight (26));
    area.removeFromRight (4);
    presetMenu.setBounds (area);
}

//==============================================================================
// Menu and display

void PresetBar::rebuildMenu()
{
    userPresetNamesInMenu = processor.getUserPresetLibrary().getPresetNames();

    presetMenu.clear (juce::dontSendNotification);
    presetMenu.addSectionHeading ("Factory");
    for (int presetIndex = 0; presetIndex < processor.getNumPrograms(); ++presetIndex)
        presetMenu.addItem (processor.getProgramName (presetIndex), presetIndex + 1);

    presetMenu.addSectionHeading ("User");
    if (userPresetNamesInMenu.isEmpty())
    {
        presetMenu.addItem ("(none yet: click Save)", userPresetIdOffset);
        presetMenu.setItemEnabled (userPresetIdOffset, false);
    }
    for (int index = 0; index < userPresetNamesInMenu.size(); ++index)
        presetMenu.addItem (userPresetNamesInMenu[index], userPresetIdOffset + 1 + index);

    displayedText.clear(); // force the display to update
    refreshFromProcessor();
}

void PresetBar::refreshFromProcessor()
{
    if (--refreshesUntilFolderScan <= 0)
    {
        refreshesUntilFolderScan = refreshesBetweenFolderScans;
        if (processor.getUserPresetLibrary().getPresetNames() != userPresetNamesInMenu)
        {
            rebuildMenu();
            return;
        }
    }

    const bool isModified = processor.isCurrentPresetModified();
    const auto text = processor.getCurrentPresetName() + (isModified ? " *" : "");
    if (text == displayedText)
        return;
    displayedText = text;

    // Select the matching item (so the menu ticks it), then show the name with the modified marker.
    int selectedId = 0;
    if (processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::factory)
        selectedId = processor.getCurrentProgram() + 1;
    else if (processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user)
        if (const int index = userPresetNamesInMenu.indexOf (processor.getCurrentPresetName()); index >= 0)
            selectedId = userPresetIdOffset + 1 + index;

    presetMenu.setSelectedId (selectedId, juce::dontSendNotification);
    if (isModified || selectedId == 0)
        presetMenu.setText (text, juce::dontSendNotification);

    const bool isUserPreset = processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user;
    saveButton.setTooltip (isUserPreset ? "Save the changes to \"" + processor.getCurrentPresetName() + "\""
                                        : "Save the current settings as your own preset");
}

void PresetBar::presetChangedByUser()
{
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
    displayedText.clear();
    refreshFromProcessor();
}

void PresetBar::loadChosenPreset()
{
    const int chosenId = presetMenu.getSelectedId();
    if (chosenId <= 0)
        return;

    if (chosenId > userPresetIdOffset)
    {
        const auto name = userPresetNamesInMenu[chosenId - userPresetIdOffset - 1];
        if (! processor.loadUserPreset (name))
            showError ("\"" + name + "\" could not be loaded. The file is missing or not a valid MIDI Forge preset.");
    }
    else if (chosenId < userPresetIdOffset)
    {
        processor.setCurrentProgram (chosenId - 1);
    }
    presetChangedByUser();
}

//==============================================================================
// Actions

void PresetBar::saveClicked()
{
    if (processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user)
        saveWithName (processor.getCurrentPresetName()); // overwrite your own preset
    else
        saveAs();
}

void PresetBar::saveAs()
{
    const bool isUserPreset = processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user;
    const auto suggestion =
        isUserPreset ? processor.getCurrentPresetName() + " 2" : processor.getCurrentPresetName() + " (my version)";
    askForName ("Save preset", "Name of the new preset:", suggestion, "Save",
                [weakThis = juce::WeakReference<PresetBar> (this)] (const juce::String& name)
                {
                    if (weakThis == nullptr)
                        return;
                    auto& library = weakThis->processor.getUserPresetLibrary();
                    if (library.contains (name))
                    {
                        weakThis->askToConfirm ("Replace preset?",
                                                "A preset called \"" + UserPresetLibrary::cleanName (name) +
                                                    "\" already exists. Replace it?",
                                                "Replace",
                                                [weakThis, name]
                                                {
                                                    if (weakThis != nullptr)
                                                        weakThis->saveWithName (name);
                                                });
                    }
                    else
                    {
                        weakThis->saveWithName (name);
                    }
                });
}

void PresetBar::saveWithName (const juce::String& name)
{
    const auto error = processor.saveUserPreset (name);
    if (error.isNotEmpty())
        showError (error);
    rebuildMenu();
}

void PresetBar::renameCurrent()
{
    askForName ("Rename preset", "New name:", processor.getCurrentPresetName(), "Rename",
                [weakThis = juce::WeakReference<PresetBar> (this)] (const juce::String& name)
                {
                    if (weakThis == nullptr)
                        return;
                    const auto error = weakThis->processor.renameCurrentUserPreset (name);
                    if (error.isNotEmpty())
                        weakThis->showError (error);
                    weakThis->rebuildMenu();
                });
}

void PresetBar::deleteCurrent()
{
    askToConfirm ("Delete preset?",
                  "Move \"" + processor.getCurrentPresetName() +
                      "\" to the trash? Your current settings stay as they are.",
                  "Delete",
                  [weakThis = juce::WeakReference<PresetBar> (this)]
                  {
                      if (weakThis == nullptr)
                          return;
                      if (! weakThis->processor.deleteCurrentUserPreset())
                          weakThis->showError ("The preset file could not be deleted.");
                      weakThis->rebuildMenu();
                  });
}

void PresetBar::importPreset()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Import a MIDI Forge preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        juce::String ("*") + UserPresetLibrary::fileExtension);

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [weakThis = juce::WeakReference<PresetBar> (this)] (const juce::FileChooser& chooser)
                              {
                                  if (weakThis == nullptr || chooser.getResult() == juce::File())
                                      return;
                                  const auto error = weakThis->processor.importUserPreset (chooser.getResult());
                                  if (error.isNotEmpty())
                                      weakThis->showError (error);
                                  weakThis->rebuildMenu();
                                  weakThis->presetChangedByUser();
                              });
}

void PresetBar::showMoreMenu()
{
    const bool isUserPreset = processor.getCurrentPresetKind() == MidiForgeProcessor::PresetKind::user;

    juce::PopupMenu menu;
    menu.addItem (saveAsItem, "Save As...");
    menu.addItem (renameItem, "Rename...", isUserPreset);
    menu.addItem (deleteItem, "Delete", isUserPreset);
    menu.addSeparator();
    menu.addItem (importItem, "Import...");
    menu.addItem (showFolderItem, "Show preset folder");
    menu.addSeparator();
    menu.addItem (aboutItem, "About MIDI Forge...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (moreButton),
                        [weakThis = juce::WeakReference<PresetBar> (this)] (int chosenItem)
                        {
                            if (weakThis == nullptr)
                                return;
                            switch (chosenItem)
                            {
                                case saveAsItem: weakThis->saveAs(); break;
                                case renameItem: weakThis->renameCurrent(); break;
                                case deleteItem: weakThis->deleteCurrent(); break;
                                case importItem: weakThis->importPreset(); break;
                                case showFolderItem: weakThis->processor.getUserPresetLibrary().revealFolder(); break;
                                case aboutItem: weakThis->showAbout(); break;
                                default: break;
                            }
                        });
}

//==============================================================================
// Dialogs

void PresetBar::askForName (const juce::String& title, const juce::String& message, const juce::String& initialName,
                            const juce::String& confirmButtonText, std::function<void (const juce::String&)> onConfirm)
{
    auto* dialog = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, this);
    trackDialog (dialog);
    dialog->addTextEditor ("name", initialName);
    if (auto* nameEditor = dialog->getTextEditor ("name"))
    {
        nameEditor->setInputRestrictions (UserPresetLibrary::maximumNameLength);
        nameEditor->selectAll();
    }
    dialog->addButton (confirmButtonText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [dialog, onConfirm] (int result)
                                 {
                                     if (result == 1)
                                         onConfirm (dialog->getTextEditorContents ("name"));
                                 }),
                             true); // the dialog deletes itself when closed
}

void PresetBar::askToConfirm (const juce::String& title, const juce::String& message,
                              const juce::String& confirmButtonText, std::function<void()> onConfirm)
{
    auto* dialog = new juce::AlertWindow (title, message, juce::MessageBoxIconType::QuestionIcon, this);
    trackDialog (dialog);
    dialog->addButton (confirmButtonText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [onConfirm] (int result)
                                 {
                                     if (result == 1)
                                         onConfirm();
                                 }),
                             true);
}

void PresetBar::showAbout()
{
    auto* dialog = new juce::AlertWindow ("About MIDI Forge", getAboutText(), juce::MessageBoxIconType::NoIcon, this);
    trackDialog (dialog);
    dialog->addButton ("Source code", openSourcePageResult);
    dialog->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey), juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true,
                             juce::ModalCallbackFunction::create (
                                 [] (int result)
                                 {
                                     if (result == openSourcePageResult)
                                         juce::URL (sourceCodeUrl).launchInDefaultBrowser();
                                 }),
                             true);
}

void PresetBar::showError (const juce::String& message)
{
    auto* dialog = new juce::AlertWindow ("MIDI Forge", message, juce::MessageBoxIconType::WarningIcon, this);
    trackDialog (dialog);
    dialog->addButton ("OK", 0, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->enterModalState (true, nullptr, true);
}

} // namespace midiforge
