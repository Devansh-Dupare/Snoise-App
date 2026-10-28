#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/PlaybackController.h"
#include "io/AudioFileManager.h"
#include "ui/WaveformView.h"

class MainComponent : public juce::Component, private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseAndLoadFile();
    void chooseAndSaveFile();
    void showError (const juce::String& message);
    void updateButtons();

    AudioFileManager::LoadedAudio input;
    juce::File inputFile;

    PlaybackController playback;
    WaveformView waveform;
    juce::TextButton loadButton { "Load audio" }, playButton { "Play" }, saveButton { "Save WAV" };
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
