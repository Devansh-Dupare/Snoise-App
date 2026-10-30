#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// One ID per user action. Never renumber: saved key mappings refer to these values.
namespace CommandIDs
{
    enum : juce::CommandID
    {
        // File
        openAudio = 0x2000,
        openNoiseFile,
        exportProcessed,
        closeFile,

        // Edit
        clearSelection = 0x2100,
        clearNoiseProfile,
        resetAllParameters,

        // View
        zoomIn = 0x2200,
        zoomOut,
        zoomToFit,
        zoomToSelection,
        followPlayhead,
        toggleSpectrograms,

        // Process
        applyNoiseReduction = 0x2300,
        alphaUp,
        alphaDown,
        alphaUpFine,
        alphaDownFine,
        alphaReset,

        // Playback
        playPause = 0x2400,
        stop,
        returnToStart,
        listenOriginal,
        listenProcessed,
        toggleListenSource,

        // Help
        showKeyboardShortcuts = 0x2500,
        showAbout,

        // Options
        showAudioSettings = 0x2600
    };
}

namespace CommandCategories
{
    static constexpr const char* file     = "File";
    static constexpr const char* edit     = "Edit";
    static constexpr const char* view     = "View";
    static constexpr const char* process  = "Process";
    static constexpr const char* playback = "Playback";
    static constexpr const char* options  = "Options";
    static constexpr const char* help     = "Help";
}

namespace MenuItemRanges
{
    // Non-command menu items live in their own ID range so they never collide with a CommandID.
    static constexpr int recentFilesBase = 0x3000;
    static constexpr int recentFilesMax  = 10;
}
