#include "AppSettings.h"

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "SpectralNoiseCancellation";
    options.filenameSuffix = ".settings";
    options.folderName = "SpectralNoiseCancellation";
    options.osxLibrarySubFolder = "Application Support";
    props.setStorageParameters (options);
}

void AppSettings::saveKeyMappings (const juce::KeyPressMappingSet& keyMappings)
{
    if (auto xml = keyMappings.createXml (true))   // true = only changes from the defaults
        user().setValue ("keyMappings", xml.get());
    else
        user().removeValue ("keyMappings");

    user().saveIfNeeded();
}

void AppSettings::restoreKeyMappings (juce::KeyPressMappingSet& keyMappings)
{
    if (auto xml = user().getXmlValue ("keyMappings"))
        keyMappings.restoreFromXml (*xml);
}

void AppSettings::saveRecentFiles (const juce::RecentlyOpenedFilesList& recent)
{
    user().setValue ("recentFiles", recent.toString());
    user().saveIfNeeded();
}

void AppSettings::restoreRecentFiles (juce::RecentlyOpenedFilesList& recent)
{
    recent.restoreFromString (user().getValue ("recentFiles"));
}

void AppSettings::saveAudioDeviceState (const juce::XmlElement& state)
{
    user().setValue ("audioDevice", &state);
    user().saveIfNeeded();
}

std::unique_ptr<juce::XmlElement> AppSettings::restoreAudioDeviceState()
{
    return user().getXmlValue ("audioDevice");
}

void AppSettings::saveAlpha (double alpha)
{
    user().setValue ("alpha", alpha);
    user().saveIfNeeded();
}

double AppSettings::restoreAlpha (double fallback)
{
    return user().getDoubleValue ("alpha", fallback);
}
