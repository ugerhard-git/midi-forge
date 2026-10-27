/**
 * @file UserPresetLibrary.h
 * @brief The user's own presets: one XML file per preset in Documents/MIDI Forge/Presets.
 *
 * Preset files can come from anywhere (shared by other people, edited by hand), so everything read
 * from disk is validated: size limit, our own format only. Values are clamped when they are applied
 * (see MidiForgeProcessor::applyStateValues).
 */
#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace midiforge
{

class UserPresetLibrary
{
public:
    /** File extension of preset files, including the dot. */
    static constexpr const char* fileExtension = ".mfpreset";
    /** Largest preset file that is read. Real presets are a few kilobytes. */
    static constexpr juce::int64 maximumFileSizeInBytes = 256 * 1024;
    /** Longest allowed preset name. */
    static constexpr int maximumNameLength = 64;

    /** @param presetFolder Where the presets live; by default Documents/MIDI Forge/Presets. */
    explicit UserPresetLibrary (juce::File presetFolder = getDefaultFolder());

    /** @return Documents/MIDI Forge/Presets on Windows and macOS. */
    static juce::File getDefaultFolder();

    const juce::File& getFolder() const noexcept { return folder; }

    /** @return The names of all presets in the folder, sorted alphabetically (ignoring case). */
    juce::StringArray getPresetNames() const;

    /** @return True if a preset with this (already cleaned) name exists. */
    bool contains (const juce::String& name) const;

    /**
     * Turns user input into a name that is safe as a file name on Windows and macOS: removes characters
     * like / \ : * ? " < > |, control characters, leading/trailing dots and spaces, and limits the length.
     * @return The cleaned name, or an empty string if nothing usable is left (or it is a reserved name).
     */
    static juce::String cleanName (const juce::String& userInput);

    /**
     * Writes a preset. An existing preset with the same name is replaced.
     * @param name        Preset name (will be cleaned).
     * @param pluginState The plug-in's parameter state (juce::AudioProcessorValueTreeState::copyState()).
     * @return An empty string on success, otherwise a message for the user.
     */
    juce::String save (const juce::String& name, const juce::ValueTree& pluginState) const;

    /**
     * Reads a preset.
     * @return The plug-in state stored in it, or nothing if the file is missing, too big or not a
     *         MIDI Forge preset.
     */
    std::optional<juce::ValueTree> load (const juce::String& name) const;

    /** Reads and validates any preset file. @see load */
    static std::optional<juce::ValueTree> readPresetFile (const juce::File& file);

    /** @return An empty string on success, otherwise a message for the user. */
    juce::String rename (const juce::String& oldName, const juce::String& newName) const;

    /** Moves the preset file to the trash (or deletes it where there is no trash). @return Success. */
    bool remove (const juce::String& name) const;

    /**
     * Copies a preset file into the library after checking it. If the name is taken, " (2)", " (3)" ...
     * is appended.
     * @param[out] importedName  The name the preset got in the library.
     * @return An empty string on success, otherwise a message for the user.
     */
    juce::String importFile (const juce::File& sourceFile, juce::String& importedName) const;

    /** Opens the preset folder in Explorer / Finder (creating it if needed). */
    void revealFolder() const;

    /** The XML tag of preset files and the attribute holding the preset's name. */
    static constexpr const char* presetFileTag = "MidiForgePreset";

private:
    juce::File fileFor (const juce::String& cleanedName) const;

    juce::File folder;
};

} // namespace midiforge
