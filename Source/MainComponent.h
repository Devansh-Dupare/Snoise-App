#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include "BinaryData.h"
#include "Commands.h"
#include "audio/PlaybackController.h"
#include "io/AppSettings.h"
#include "io/AudioFileManager.h"
#include "dsp/NoiseReductionEngine.h"
#include "ui/ActionsPanel.h"
#include "ui/MainMenuModel.h"
#include "ui/ViewState.h"
#include "ui/NoiseProfileView.h"
#include "ui/NoiseReductionPanel.h"
#include "ui/SpectrogramView.h"
#include "ui/WaveformView.h"

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::FileDragAndDropTarget,
                      private juce::ScrollBar::Listener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    juce::MenuBarModel& getMenuModel() { return menuModel; }
    void loadInputFile (const juce::File&);
    // Runs `proceed` once any unexported processed audio has been dealt with (exported, discarded, or never if cancelled).
    void confirmDiscardIfNeeded (std::function<void()> proceed);

private:
    enum class DropZone { none, input, noise };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBar();
    DropZone dropZoneAt (juce::Point<int>) const;
    juce::Rectangle<int> inputDropArea() const;
    void updateDropZone (juce::Point<int>);
    bool isSupportedAudioFile (const juce::File&) const;
    void loadNoiseFile (const juce::File&);
    void closeCurrentFile();
    void nudgeAlpha (int direction, bool fine);
    void setStatus (const juce::String& text);
    void showShortcutEditor();
    void showAbout();
    void showAudioSettings();
    bool hasInputAudio() const { return input.buffer.getNumSamples() > 0; }
    double zoomAnchor() const;
    void toggleListenSource();
    void togglePlayback();

    void timerCallback() override;
    struct LoadOutcome
    {
        juce::Result result = juce::Result::ok();
        AudioFileManager::LoadedAudio audio;
        std::vector<NoiseProfile> profiles;   // noise files only
    };

    void chooseAndLoadFile();
    void chooseAndSaveFile (std::function<void()> afterSave = {});
    void chooseAndLoadNoiseFile();
    void useNoiseRegion (double startSeconds, double endSeconds);
    void clearNoiseProfile();
    void showProfile (const juce::String& description);
    void startProcessing();
    void finishProcessing (juce::Result result, juce::AudioBuffer<float> processedAudio, unsigned settingsAtStart);
    void startLoad (const juce::File& file, bool isNoiseFile);
    void finishLoad (const juce::File& file, bool isNoiseFile, LoadOutcome& outcome);
    void refreshStatus();
    void markStale();
    void selectVersion (bool processed);
    void saveCurrent (const juce::File& file, juce::AudioBuffer<float> audio, std::function<void()> afterSave);
    void showError (const juce::String& message);
    void updateButtons();

    // Declared first: the buttons, menu and key mappings all refer to the command manager and view state.
    juce::ApplicationCommandManager commandManager;
    ViewState viewState;
    AppSettings settings;
    juce::RecentlyOpenedFilesList recentFiles;
    MainMenuModel menuModel { commandManager, recentFiles, [this] (const juce::File& f) { loadInputFile (f); } };

    AudioFileManager::LoadedAudio input;
    juce::File inputFile;

    NoiseReductionEngine engine;
    std::vector<NoiseProfile> noiseProfiles;

    std::unique_ptr<juce::XmlElement> savedDeviceState { settings.restoreAudioDeviceState() };
    PlaybackController playback { savedDeviceState.get() };
    WaveformView waveform { viewState };
    juce::ScrollBar timeScrollBar { false };
    NoiseProfileView noiseProfileView;
    // Processing (plan §7.1): the job works on copies and reports back on the message thread.
    juce::AudioBuffer<float> processed;
    bool processedExported = true, followPlayheadEnabled = false, spectrogramsVisible = true;
    DropZone currentDropZone = DropZone::none;
    int statusHoldTicks = 0;
    bool hasProcessed = false, processedSelected = false, isProcessing = false, isLoading = false, stale = true;
    unsigned settingsVersion = 0;   // bumped whenever alpha or the noise profile changes
    std::unique_ptr<juce::Thread> loader;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelRequested { false };
    std::unique_ptr<juce::Thread> worker;

    juce::TextButton originalButton { "Original" }, processedButton { "Processed" };
    SpectrogramView originalSpectrogram { viewState, "Original", "Load an audio file" };
    SpectrogramView processedSpectrogram { viewState, "Processed", "Mark some noise, then press Process" };
    NoiseReductionPanel reductionPanel;
    ActionsPanel actions;
    juce::Image logo = juce::ImageFileFormat::loadFrom (BinaryData::AppIcon_png, BinaryData::AppIcon_pngSize);
    juce::PopupMenu appMenuExtras;
    juce::TooltipWindow tooltipWindow { this };
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
