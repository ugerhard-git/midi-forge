/**
 * @file RecentModuleList.cpp
 * @brief Implementation of RecentModuleList.
 */
#include "RecentModuleList.h"

#include "Engine/ModuleDescriptions.h"

#include <algorithm>

namespace midiforge
{

namespace
{
constexpr const char* settingsTag = "MidiForgeSettings";
constexpr const char* recentModulesTag = "RecentModules";
constexpr const char* moduleTag = "Module";
constexpr const char* typeAttribute = "type";
} // namespace

RecentModuleList::RecentModuleList (juce::File settingsFile) : file (std::move (settingsFile))
{
    reload();
}

juce::File RecentModuleList::getDefaultSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("MIDI Forge")
        .getChildFile ("Settings.xml");
}

void RecentModuleList::reload()
{
    moduleTypes.clear();
    if (! file.existsAsFile() || file.getSize() > maximumFileSizeInBytes)
        return;

    const auto settings = juce::parseXMLIfTagMatches (file, settingsTag);
    if (settings == nullptr)
        return;
    const auto* recentModules = settings->getChildByName (recentModulesTag);
    if (recentModules == nullptr)
        return;

    for (const auto* module : recentModules->getChildWithTagNameIterator (moduleTag))
    {
        const int type = module->getIntAttribute (typeAttribute, 0);
        const bool isKnownModule = type > 0 && type < numModuleTypes;
        if (isKnownModule && std::find (moduleTypes.begin(), moduleTypes.end(), type) == moduleTypes.end())
            moduleTypes.push_back (type);
        if ((int) moduleTypes.size() == maximumNumberOfModules)
            break;
    }
}

void RecentModuleList::moduleWasPicked (int moduleType)
{
    if (moduleType <= 0 || moduleType >= numModuleTypes)
        return;
    reload(); // another instance may have changed the list in the meantime
    moduleTypes.erase (std::remove (moduleTypes.begin(), moduleTypes.end(), moduleType), moduleTypes.end());
    moduleTypes.insert (moduleTypes.begin(), moduleType);
    if ((int) moduleTypes.size() > maximumNumberOfModules)
        moduleTypes.resize ((size_t) maximumNumberOfModules);
    save();
}

void RecentModuleList::save() const
{
    // Keep other settings that a later version may have written to the same file.
    std::unique_ptr<juce::XmlElement> settings;
    if (file.existsAsFile() && file.getSize() <= maximumFileSizeInBytes)
        settings = juce::parseXMLIfTagMatches (file, settingsTag);
    if (settings == nullptr)
        settings = std::make_unique<juce::XmlElement> (settingsTag);

    settings->deleteAllChildElementsWithTagName (recentModulesTag);
    auto* recentModules = settings->createNewChildElement (recentModulesTag);
    for (int type : moduleTypes)
        recentModules->createNewChildElement (moduleTag)->setAttribute (typeAttribute, type);

    // Failing to save is not worth bothering the user about: the list simply isn't remembered.
    if (file.getParentDirectory().createDirectory())
        settings->writeTo (file);
}

} // namespace midiforge
