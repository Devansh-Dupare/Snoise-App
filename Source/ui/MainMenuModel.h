#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

// Builds the File/Edit/View/Process/Playback/Help menus from command IDs.
class MainMenuModel : public juce::MenuBarModel
{
public:
    MainMenuModel (juce::ApplicationCommandManager& cm,
                   juce::RecentlyOpenedFilesList& recent,
                   std::function<void (const juce::File&)> onOpenRecent);

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    juce::ApplicationCommandManager& commandManager;
    juce::RecentlyOpenedFilesList& recentFiles;
    std::function<void (const juce::File&)> openRecent;
};
