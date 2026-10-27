/**
 * @file RecentModuleList.h
 * @brief The modules the user picked most recently in the module browser, shared by all plug-in instances
 *        and saved in Documents/MIDI Forge/Settings.xml.
 */
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace midiforge
{

/**
 * Keeps the last few module types picked in the module browser, newest first, without duplicates and
 * without the Empty module. Every change is written to the settings file at once, so other plug-in
 * instances (and the next session) see it.
 *
 * The settings file may have been edited by hand, so reading it is defensive: size limit, our own format
 * only, unknown or repeated module types are skipped.
 *
 * Only used on the message thread.
 */
class RecentModuleList
{
public:
    /** How many modules the list keeps. */
    static constexpr int maximumNumberOfModules = 5;
    /** Largest settings file that is read. */
    static constexpr juce::int64 maximumFileSizeInBytes = 64 * 1024;

    /** @param settingsFile Where the list is stored; by default Documents/MIDI Forge/Settings.xml. */
    explicit RecentModuleList (juce::File settingsFile = getDefaultSettingsFile());

    /** @return Documents/MIDI Forge/Settings.xml on Windows and macOS. */
    static juce::File getDefaultSettingsFile();

    /** @return The module types, most recently used first. */
    const std::vector<int>& getModuleTypes() const noexcept { return moduleTypes; }

    /** Puts a module type at the front of the list and saves the list. The Empty module is ignored. */
    void moduleWasPicked (int moduleType);

    /** Reads the list from the settings file again (e.g. after another instance changed it). */
    void reload();

private:
    void save() const;

    juce::File file;
    std::vector<int> moduleTypes;
};

/**
 * The one list shared by all plug-in instances in a host process, for use with
 * juce::SharedResourcePointer<SharedRecentModuleList>.
 */
struct SharedRecentModuleList
{
    RecentModuleList list;
};

} // namespace midiforge
