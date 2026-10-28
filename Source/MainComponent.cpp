#include "MainComponent.h"
#include "ui/AppLookAndFeel.h"

namespace
{
    // Runs a job on its own thread; the job reports back through MessageManager::callAsync.
    class JobThread : public juce::Thread
    {
    public:
        explicit JobThread (std::function<void()> j) : juce::Thread ("Noise reduction"), job (std::move (j)) {}
        void run() override { job(); }

    private:
        std::function<void()> job;
    };
}

MainComponent::MainComponent()
{
    addAndMakeVisible (waveform);
    addAndMakeVisible (noiseProfileView);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (originalSpectrogram);
    addAndMakeVisible (processedSpectrogram);
    addAndMakeVisible (reductionPanel);
    addAndMakeVisible (actions);
    addAndMakeVisible (originalButton);
    addAndMakeVisible (processedButton);

    reductionPanel.alphaSlider.onValueChange = [this] { markStale(); };

    originalButton.setRadioGroupId (1);
    processedButton.setRadioGroupId (1);
    originalButton.setClickingTogglesState (true);
    processedButton.setClickingTogglesState (true);
    originalButton.setToggleState (true, juce::dontSendNotification);
    originalButton.onClick = [this] { selectVersion (false); };
    processedButton.onClick = [this] { selectVersion (true); };
    actions.processButton.onClick = [this]
    {
        if (isProcessing)
            cancelRequested = true;
        else
            startProcessing();
    };

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6e6e73));
    statusLabel.setText (playback.getDeviceError().isEmpty() ? "No file loaded"
                                                             : "Audio device unavailable: " + playback.getDeviceError(),
                         juce::dontSendNotification);

    loadButton.onClick = [this] { chooseAndLoadFile(); };
    actions.saveButton.onClick = [this] { chooseAndSaveFile(); };
    actions.playButton.onClick = [this]
    {
        if (playback.isPlaying())
            playback.stop();
        else
            playback.play();

        updateButtons();
    };

    waveform.onSeek = [this] (double seconds) { playback.setPositionSeconds (seconds); };
    waveform.onRegionSelected = [this] (double start, double end) { useNoiseRegion (start, end); };
    noiseProfileView.onLoadNoiseFile = [this] { chooseAndLoadNoiseFile(); };

    updateButtons();
    setSize (1440, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    cancelRequested = true;
    if (worker != nullptr)
        worker->stopThread (5000);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppLookAndFeel::window);
    g.setColour (AppLookAndFeel::textPrimary);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Spectral Noise Cancellation", 24, 24, 320, 44, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto bar = area.removeFromTop (44);

    bar.removeFromLeft (332);
    loadButton.setBounds (bar.removeFromLeft (130).withSizeKeepingCentre (130, 36));
    processedButton.setBounds (bar.removeFromRight (100).withSizeKeepingCentre (100, 36));
    originalButton.setBounds (bar.removeFromRight (100).withSizeKeepingCentre (100, 36));
    bar.removeFromLeft (16);
    bar.removeFromRight (16);
    statusLabel.setBounds (bar);

    const int gap = 14;
    const int usable = area.getHeight() - 3 * gap;
    area.removeFromTop (gap);
    waveform.setBounds (area.removeFromTop (usable * 20 / 100));
    area.removeFromTop (gap);

    auto spectrograms = area.removeFromTop (usable * 38 / 100);
    originalSpectrogram.setBounds (spectrograms.removeFromLeft ((spectrograms.getWidth() - gap) / 2));
    spectrograms.removeFromLeft (gap);
    processedSpectrogram.setBounds (spectrograms);

    area.removeFromTop (gap);
    actions.setBounds (area.removeFromRight (240));
    area.removeFromRight (gap);
    reductionPanel.setBounds (area.removeFromRight (280));
    area.removeFromRight (gap);
    noiseProfileView.setBounds (area);
}

void MainComponent::timerCallback()
{
    const double position = playback.getPositionSeconds();
    waveform.setPlayheadSeconds (position);
    originalSpectrogram.setPlayheadSeconds (position);
    processedSpectrogram.setPlayheadSeconds (position);

    if (isProcessing)
        actions.processButton.setButtonText ("Cancel  " + juce::String (juce::roundToInt (progress.load() * 100.0f)) + "%");

    const bool playing = playback.isPlaying();
    if ((actions.playButton.getButtonText() == "Stop") != playing)
        updateButtons();
}

void MainComponent::updateButtons()
{
    const bool hasAudio = input.buffer.getNumSamples() > 0;
    actions.playButton.setEnabled (hasAudio);
    actions.playButton.setButtonText (playback.isPlaying() ? "Stop" : "Play");
    noiseProfileView.setLoadEnabled (hasAudio && ! isProcessing);

    const bool canProcess = hasAudio && ! noiseProfiles.empty();
    actions.processButton.setEnabled (isProcessing || canProcess);
    actions.processButton.setTooltip (! hasAudio ? "Load an audio file first"
                              : noiseProfiles.empty() ? "Drag on the waveform to mark noise, or load a noise file"
                                                      : "");
    actions.processButton.setColour (juce::TextButton::buttonColourId,
                             stale && canProcess && ! isProcessing ? AppLookAndFeel::accent : AppLookAndFeel::card);
    actions.processButton.setColour (juce::TextButton::textColourOffId,
                                     stale && canProcess && ! isProcessing ? juce::Colours::white : AppLookAndFeel::textPrimary);
    if (! isProcessing)
        actions.processButton.setButtonText ("Process");

    processedButton.setEnabled (hasProcessed && ! isProcessing);
    originalButton.setEnabled (hasAudio && ! isProcessing);
    reductionPanel.alphaSlider.setEnabled (! isProcessing);
    loadButton.setEnabled (! isProcessing);
    actions.saveButton.setEnabled (hasAudio && ! isProcessing);
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
        originalSpectrogram.setAudio (input.buffer, input.sampleRate);
        processedSpectrogram.clear();
        clearNoiseProfile();
        hasProcessed = processedSelected = false;
        stale = true;
        originalButton.setToggleState (true, juce::dontSendNotification);

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

        saveCurrent (file.withFileExtension ("wav"), processedSelected ? processed : input.buffer);
    });
}

void MainComponent::clearNoiseProfile()
{
    noiseProfiles.clear();
    noiseProfileView.clearProfile();
    waveform.clearRegion();
    updateButtons();
}

void MainComponent::showProfile (const juce::String& description)
{
    noiseProfileView.setProfile (noiseProfiles[0].getAverageMagnitude(), input.sampleRate,
                                 description + " (" + juce::String (noiseProfiles[0].getFrameCount()) + " frames)");
    markStale();
}

void MainComponent::useNoiseRegion (double startSeconds, double endSeconds)
{
    const auto startSample = static_cast<int> (std::llround (startSeconds * input.sampleRate));
    const auto endSample = static_cast<int> (std::llround (endSeconds * input.sampleRate));

    const auto result = engine.estimateProfileFromRegion (input.buffer, startSample, endSample - startSample, noiseProfiles);

    if (result.failed())
    {
        clearNoiseProfile();
        showError (result.getErrorMessage());
        return;
    }

    showProfile ("Region " + juce::String (startSeconds, 2) + " - " + juce::String (endSeconds, 2) + " s");
}

void MainComponent::chooseAndLoadNoiseFile()
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    chooser = std::make_unique<juce::FileChooser> ("Choose a noise-only recording", juce::File(), formats.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;

        AudioFileManager::LoadedAudio noise;
        auto result = AudioFileManager::load (file, noise);

        if (result.wasOk())
            result = NoiseReductionEngine::checkSampleRates (input.sampleRate, noise.sampleRate);

        if (result.wasOk())
            result = engine.estimateProfile (noise.buffer, noiseProfiles);

        if (result.failed())
        {
            showError (result.getErrorMessage());
            return;
        }

        waveform.clearRegion();
        showProfile ("File " + file.getFileName());

        if (noise.numNonFiniteReplaced > 0)
            showError (juce::String (noise.numNonFiniteReplaced) + " invalid (NaN/Inf) samples in the noise file were replaced with silence.");
    });
}

void MainComponent::markStale()
{
    stale = true;
    updateButtons();
}

void MainComponent::selectVersion (bool useProcessed)
{
    processedSelected = useProcessed && hasProcessed;
    const auto& audio = processedSelected ? processed : input.buffer;
    playback.replaceAudio (audio, input.sampleRate);
}

void MainComponent::startProcessing()
{
    if (isProcessing || input.buffer.getNumSamples() == 0 || noiseProfiles.empty())
        return;

    isProcessing = true;
    cancelRequested = false;
    progress = 0.0f;
    playback.stop();
    statusLabel.setText ("Processing...", juce::dontSendNotification);
    updateButtons();

    struct Outcome { juce::Result result = juce::Result::ok(); juce::AudioBuffer<float> audio; };
    auto outcome = std::make_shared<Outcome>();

    // The job only touches copies, so the UI stays free to change while it runs.
    worker = std::make_unique<JobThread> ([this, outcome,
                                           safeThis = juce::Component::SafePointer<MainComponent> (this),
                                           audioIn = input.buffer,
                                           profiles = noiseProfiles,
                                           alpha = static_cast<float> (reductionPanel.alphaSlider.getValue())]
    {
        const NoiseReductionEngine jobEngine;
        outcome->result = jobEngine.process (audioIn, profiles, alpha, outcome->audio,
                                             [this] (float p) { progress = p; }, cancelRequested);

        juce::MessageManager::callAsync ([safeThis, outcome]
        {
            if (safeThis != nullptr)
                safeThis->finishProcessing (outcome->result, std::move (outcome->audio));
        });
    });
    worker->startThread();
}

void MainComponent::finishProcessing (juce::Result result, juce::AudioBuffer<float> processedAudio)
{
    if (worker != nullptr)
        worker->stopThread (5000);   // the job has finished; this just joins it
    worker.reset();
    isProcessing = false;

    if (result.wasOk())
    {
        processed = std::move (processedAudio);
        hasProcessed = true;
        stale = false;
        statusLabel.setText ("Processed with alpha = " + juce::String (reductionPanel.alphaSlider.getValue(), 2)
                                 + ". Use Original / Processed to compare.",
                             juce::dontSendNotification);
        processedButton.setToggleState (true, juce::dontSendNotification);
        processedSelected = true;
        playback.replaceAudio (processed, input.sampleRate);
        processedSpectrogram.setAudio (processed, input.sampleRate);
    }
    else
    {
        statusLabel.setText (cancelRequested ? "Processing cancelled" : "Processing failed", juce::dontSendNotification);

        if (! cancelRequested)
            showError (result.getErrorMessage());
    }

    updateButtons();
}

void MainComponent::saveCurrent (const juce::File& file, juce::AudioBuffer<float> audio)
{
    auto write = [this, file] (const juce::AudioBuffer<float>& toWrite)
    {
        const auto result = AudioFileManager::saveWav (file, toWrite, input.sampleRate);

        if (result.failed())
            showError (result.getErrorMessage());
        else
            statusLabel.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
    };

    const float peak = AudioFileManager::getPeak (audio);

    if (peak <= 1.0f)
    {
        write (audio);
        return;
    }

    // Overlap-add of modified frames can overshoot full scale slightly (plan §7.3).
    const auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (
        juce::MessageBoxIconType::WarningIcon, "Spectral Noise Cancellation",
        "The audio peaks at " + juce::String (20.0f * std::log10 (peak), 1) + " dBFS, above full scale, and will clip.\n\n"
        "Normalise it to -0.1 dBFS before saving?",
        "Normalise", "Save as is", "Cancel");

    juce::AlertWindow::showAsync (options, [write, toSave = std::move (audio), peak] (int choice) mutable
    {
        if (choice == 1)
        {
            toSave.applyGain (0.9886f / peak);   // -0.1 dBFS
            write (toSave);
        }
        else if (choice == 2)
        {
            write (toSave);
        }
    });
}
