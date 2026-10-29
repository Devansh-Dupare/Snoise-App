#include "MainMenuModel.h"
#include "Commands.h"

MainMenuModel::MainMenuModel (juce::ApplicationCommandManager& cm,
                              juce::RecentlyOpenedFilesList& recent,
                              std::function<void (const juce::File&)> onOpenRecent)
    : commandManager (cm), recentFiles (recent), openRecent (std::move (onOpenRecent))
{
    setApplicationCommandManagerToWatch (&commandManager);   // refresh on commandStatusChanged()
}

juce::StringArray MainMenuModel::getMenuBarNames()
{
    return { "File", "Edit", "View", "Process", "Playback", "Help" };
}

juce::PopupMenu MainMenuModel::getMenuForIndex (int index, const juce::String&)
{
    using namespace CommandIDs;
    juce::PopupMenu m;
    auto add = [&] (juce::CommandID id) { m.addCommandItem (&commandManager, id); };

    switch (index)
    {
        case 0: // File
        {
            add (openAudio);
            add (openNoiseFile);

            juce::PopupMenu recentMenu;
            recentFiles.createPopupMenuItems (recentMenu, MenuItemRanges::recentFilesBase, false, true);
            m.addSubMenu ("Open Recent", recentMenu, recentMenu.getNumItems() > 0);

            m.addSeparator();
            add (exportProcessed);
            m.addSeparator();
            add (closeFile);
           #if ! JUCE_MAC
            m.addSeparator();
            add (juce::StandardApplicationCommandIDs::quit);
           #endif
            break;
        }
        case 1: // Edit
            add (clearSelection);
            add (clearNoiseProfile);
            m.addSeparator();
            add (resetAllParameters);
            break;

        case 2: // View
            add (zoomIn); add (zoomOut); add (zoomToFit); add (zoomToSelection);
            m.addSeparator();
            add (followPlayhead);
            add (toggleSpectrograms);
            break;

        case 3: // Process
            add (applyNoiseReduction);
            m.addSeparator();
            add (alphaUp); add (alphaDown); add (alphaUpFine); add (alphaDownFine); add (alphaReset);
            break;

        case 4: // Playback
            add (playPause); add (stop); add (returnToStart);
            m.addSeparator();
            add (listenOriginal); add (listenProcessed); add (toggleListenSource);
            break;

        case 5: // Help
            add (showKeyboardShortcuts);
            add (showAbout);
            break;

        default: break;
    }

    return m;
}

void MainMenuModel::menuItemSelected (int itemID, int)
{
    using namespace MenuItemRanges;

    if (itemID >= recentFilesBase && itemID < recentFilesBase + recentFilesMax && openRecent)
        openRecent (recentFiles.getFile (itemID - recentFilesBase));
}
