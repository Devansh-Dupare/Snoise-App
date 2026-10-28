#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include "audio/PlaybackController.h"
#include "io/AudioFileManager.h"
#include "dsp/NoiseReductionEngine.h"
#include "ui/NoiseProfileView.h"
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
    void chooseAndLoadNoiseFile();
    void useNoiseRegion (double startSeconds, double endSeconds);
    void clearNoiseProfile();
    void showProfile (const juce::String& description);
    void startProcessing();
    void finishProcessing (juce::Result result, juce::AudioBuffer<float> processedAudio);
    void markStale();
    void selectVersion (bool processed);
    void saveCurrent (const juce::File& file, juce::AudioBuffer<float> audio);
    void showError (const juce::String& message);
    void updateButtons();

    AudioFileManager::LoadedAudio input;
    juce::File inputFile;

    NoiseReductionEngine engine;
    std::vector<NoiseProfile> noiseProfiles;

    PlaybackController playback;
    WaveformView waveform;
    NoiseProfileView noiseProfileView;
    // Processing (plan §7.1): the job works on copies and reports back on the message thread.
    juce::AudioBuffer<float> processed;
    bool hasProcessed = false, processedSelected = false, isProcessing = false, stale = true;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelRequested { false };
    std::unique_ptr<juce::Thread> worker;

    juce::TextButton loadButton { "Load audio" }, playButton { "Play" }, saveButton { "Save WAV" },
                     processButton { "Process" }, originalButton { "Original" }, processedButton { "Processed" };
    juce::Slider alphaSlider;
    juce::Label alphaLabel;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
