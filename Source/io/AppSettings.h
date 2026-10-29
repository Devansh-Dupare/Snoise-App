#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// Persistent user settings: key mappings, recent files, last alpha.
class AppSettings
{
public:
    AppSettings();

    void saveKeyMappings (const juce::KeyPressMappingSet& keyMappings);
    void restoreKeyMappings (juce::KeyPressMappingSet& keyMappings);

    void saveRecentFiles (const juce::RecentlyOpenedFilesList& recent);
    void restoreRecentFiles (juce::RecentlyOpenedFilesList& recent);

    void saveAlpha (double alpha);
    double restoreAlpha (double fallback);

private:
    juce::PropertiesFile& user() { return *props.getUserSettings(); }

    juce::ApplicationProperties props;
};
