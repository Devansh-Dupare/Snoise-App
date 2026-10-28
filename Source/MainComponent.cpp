#include "MainComponent.h"

MainComponent::MainComponent()
{
    addAndMakeVisible (waveform);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (playButton);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (statusLabel);

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6e6e73));
    statusLabel.setText (playback.getDeviceError().isEmpty() ? "No file loaded"
                                                             : "Audio device unavailable: " + playback.getDeviceError(),
                         juce::dontSendNotification);

    loadButton.onClick = [this] { chooseAndLoadFile(); };
    saveButton.onClick = [this] { chooseAndSaveFile(); };
    playButton.onClick = [this]
    {
        if (playback.isPlaying())
            playback.stop();
        else
            playback.play();

        updateButtons();
    };

    waveform.onSeek = [this] (double seconds) { playback.setPositionSeconds (seconds); };

    updateButtons();
    setSize (1440, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff5f5f7));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto bar = area.removeFromTop (40);
    loadButton.setBounds (bar.removeFromLeft (130));
    bar.removeFromLeft (12);
    playButton.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (12);
    saveButton.setBounds (bar.removeFromLeft (130));
    bar.removeFromLeft (16);
    statusLabel.setBounds (bar);

    area.removeFromTop (16);
    waveform.setBounds (area.removeFromTop (220));
}

void MainComponent::timerCallback()
{
    waveform.setPlayheadSeconds (playback.getPositionSeconds());

    const bool playing = playback.isPlaying();
    if ((playButton.getButtonText() == "Stop") != playing)
        updateButtons();
}

void MainComponent::updateButtons()
{
    const bool hasAudio = input.buffer.getNumSamples() > 0;
    playButton.setEnabled (hasAudio);
    saveButton.setEnabled (hasAudio);
    playButton.setButtonText (playback.isPlaying() ? "Stop" : "Play");
}

void MainComponent::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Spectral Noise Cancellation", message);
}

void MainComponent::chooseAndLoadFile()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    chooser = std::make_unique<juce::FileChooser> ("Choose an audio file", juce::File(), formats.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        AudioFileManager::LoadedAudio loaded;
        const auto result = AudioFileManager::load (file, loaded);

        if (result.failed())
        {
            showError (result.getErrorMessage());
            return;
        }

        playback.stop();
        input = std::move (loaded);
        inputFile = file;
        playback.setAudio (input.buffer, input.sampleRate);
        waveform.setAudio (input.buffer, input.sampleRate);

        statusLabel.setText (file.getFileName() + "  -  " + juce::String (input.sampleRate, 0) + " Hz, "
                                 + juce::String (input.buffer.getNumChannels()) + " ch, "
                                 + juce::String (input.buffer.getNumSamples() / input.sampleRate, 2) + " s",
                             juce::dontSendNotification);
        updateButtons();

        if (input.numNonFiniteReplaced > 0)
            showError (juce::String (input.numNonFiniteReplaced) + " invalid (NaN/Inf) samples were replaced with silence.");
    });
}

void MainComponent::chooseAndSaveFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Save as WAV", inputFile.withFileExtension ("wav"), "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        const auto result = AudioFileManager::saveWav (file.withFileExtension ("wav"), input.buffer, input.sampleRate);

        if (result.failed())
            showError (result.getErrorMessage());
        else
            statusLabel.setText ("Saved " + file.withFileExtension ("wav").getFileName(), juce::dontSendNotification);
    });
}
