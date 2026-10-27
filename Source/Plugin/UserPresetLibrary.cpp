/**
 * @file UserPresetLibrary.cpp
 * @brief Implementation of UserPresetLibrary.
 *
 * File format (formatVersion 1):
 *   <MidiForgePreset formatVersion="1">
 *     <MidiForge> ...the plug-in's parameter state, as saved in DAW projects... </MidiForge>
 *   </MidiForgePreset>
 */
#include "UserPresetLibrary.h"

namespace midiforge
{

namespace
{
constexpr int currentFormatVersion = 1;
/** Root type of the plug-in's parameter state (see the AudioProcessorValueTreeState in the processor). */
constexpr const char* pluginStateTag = "MidiForge";
constexpr const char* charactersNotAllowedInNames = "/\\:*?\"<>|";

bool isReservedWindowsName (const juce::String& name)
{
    const auto upper = name.toUpperCase();
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL")
        return true;
    for (int number = 1; number <= 9; ++number)
        if (upper == "COM" + juce::String (number) || upper == "LPT" + juce::String (number))
            return true;
    return false;
}
} // namespace

UserPresetLibrary::UserPresetLibrary (juce::File presetFolder) : folder (std::move (presetFolder)) {}

juce::File UserPresetLibrary::getDefaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("MIDI Forge")
        .getChildFile ("Presets");
}

juce::String UserPresetLibrary::cleanName (const juce::String& userInput)
{
    juce::String cleaned;
    for (auto character : userInput)
    {
        const bool isControlCharacter = character < 0x20 || character == 0x7F;
        if (isControlCharacter || juce::String (charactersNotAllowedInNames).containsChar (character))
            cleaned << ' ';
        else
            cleaned << juce::String::charToString (character);
    }

    // Collapse runs of spaces, trim spaces and dots at both ends (dots would hide files or clash with
    // the extension), and limit the length.
    while (cleaned.contains ("  "))
        cleaned = cleaned.replace ("  ", " ");
    cleaned = cleaned.trimCharactersAtStart (" .").trimCharactersAtEnd (" .");
    cleaned = cleaned.substring (0, maximumNameLength).trimEnd();

    if (cleaned.isEmpty() || isReservedWindowsName (cleaned))
        return {};
    return cleaned;
}

juce::File UserPresetLibrary::fileFor (const juce::String& cleanedName) const
{
    return folder.getChildFile (cleanedName + fileExtension);
}

juce::StringArray UserPresetLibrary::getPresetNames() const
{
    juce::StringArray names;
    if (! folder.isDirectory())
        return names;

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        names.add (file.getFileNameWithoutExtension());

    names.sortNatural();
    return names;
}

bool UserPresetLibrary::contains (const juce::String& name) const
{
    const auto cleaned = cleanName (name);
    return cleaned.isNotEmpty() && fileFor (cleaned).existsAsFile();
}

juce::String UserPresetLibrary::save (const juce::String& name, const juce::ValueTree& pluginState) const
{
    const auto cleaned = cleanName (name);
    if (cleaned.isEmpty())
        return "Please enter a name with at least one letter or number.";

    if (! pluginState.hasType (pluginStateTag))
        return "Internal error: unexpected plug-in state.";

    if (! folder.createDirectory())
        return "Could not create the preset folder " + folder.getFullPathName();

    juce::XmlElement presetXml (presetFileTag);
    presetXml.setAttribute ("formatVersion", currentFormatVersion);
    if (auto stateXml = pluginState.createXml())
        presetXml.addChildElement (stateXml.release());

    // Write to a temporary file first, so a crash or full disk never leaves a half-written preset.
    const juce::TemporaryFile temporaryFile (fileFor (cleaned));
    if (! presetXml.writeTo (temporaryFile.getFile()) || ! temporaryFile.overwriteTargetFileWithTemporary())
        return "Could not write the preset file in " + folder.getFullPathName();

    return {};
}

std::optional<juce::ValueTree> UserPresetLibrary::readPresetFile (const juce::File& file)
{
    if (! file.existsAsFile() || file.getSize() > maximumFileSizeInBytes)
        return std::nullopt;

    const auto presetXml = juce::XmlDocument::parse (file);
    if (presetXml == nullptr || ! presetXml->hasTagName (presetFileTag))
        return std::nullopt;

    const auto* stateXml = presetXml->getChildByName (pluginStateTag);
    if (stateXml == nullptr)
        return std::nullopt;

    auto state = juce::ValueTree::fromXml (*stateXml);
    if (! state.isValid())
        return std::nullopt;
    return state;
}

std::optional<juce::ValueTree> UserPresetLibrary::load (const juce::String& name) const
{
    const auto cleaned = cleanName (name);
    if (cleaned.isEmpty())
        return std::nullopt;
    return readPresetFile (fileFor (cleaned));
}

juce::String UserPresetLibrary::rename (const juce::String& oldName, const juce::String& newName) const
{
    const auto cleanedOld = cleanName (oldName);
    const auto cleanedNew = cleanName (newName);
    if (cleanedNew.isEmpty())
        return "Please enter a name with at least one letter or number.";
    if (cleanedOld.isEmpty() || ! fileFor (cleanedOld).existsAsFile())
        return "The preset \"" + oldName + "\" no longer exists.";
    if (cleanedNew == cleanedOld)
        return {};

    // On case-insensitive file systems a pure change of case must not count as "name taken".
    const bool onlyCaseChanged = cleanedNew.equalsIgnoreCase (cleanedOld);
    if (! onlyCaseChanged && fileFor (cleanedNew).exists())
        return "A preset called \"" + cleanedNew + "\" already exists.";

    if (! fileFor (cleanedOld).moveFileTo (fileFor (cleanedNew)))
        return "Could not rename the preset file.";
    return {};
}

bool UserPresetLibrary::remove (const juce::String& name) const
{
    const auto cleaned = cleanName (name);
    if (cleaned.isEmpty())
        return false;

    const auto file = fileFor (cleaned);
    if (! file.existsAsFile())
        return false;
    return file.moveToTrash() || file.deleteFile();
}

juce::String UserPresetLibrary::importFile (const juce::File& sourceFile, juce::String& importedName) const
{
    if (! readPresetFile (sourceFile).has_value())
        return "\"" + sourceFile.getFileName() + "\" is not a MIDI Forge preset (or it is damaged).";

    auto baseName = cleanName (sourceFile.getFileNameWithoutExtension());
    if (baseName.isEmpty())
        baseName = "Imported preset";

    if (! folder.createDirectory())
        return "Could not create the preset folder " + folder.getFullPathName();

    // Never overwrite: find a free name.
    importedName = baseName;
    for (int number = 2; fileFor (importedName).exists(); ++number)
        importedName = baseName.substring (0, maximumNameLength - 6) + " (" + juce::String (number) + ")";

    if (! sourceFile.copyFileTo (fileFor (importedName)))
        return "Could not copy the preset into " + folder.getFullPathName();
    return {};
}

void UserPresetLibrary::revealFolder() const
{
    folder.createDirectory();
    folder.startAsProcess(); // opens the folder in Explorer / Finder
}

} // namespace midiforge
