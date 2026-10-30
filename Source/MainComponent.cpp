#include "MainComponent.h"
#include "ui/AppLookAndFeel.h"
#include "BinaryData.h"

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
    recentFiles.setMaxNumberOfItems (MenuItemRanges::recentFilesMax);
    settings.restoreRecentFiles (recentFiles);

    commandManager.registerAllCommandsForTarget (this);
    commandManager.registerAllCommandsForTarget (juce::JUCEApplication::getInstance());   // quit
    commandManager.setFirstCommandTarget (this);
    settings.restoreKeyMappings (*commandManager.getKeyMappings());   // after registration, so defaults exist
    commandManager.getKeyMappings()->addChangeListener (this);
    viewState.addChangeListener (this);
    playback.getDeviceManager().addChangeListener (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    addAndMakeVisible (waveform);
    addAndMakeVisible (timeScrollBar);
    addAndMakeVisible (noiseProfileView);
    addAndMakeVisible (statusLabel);
    addAndMakeVisible (originalSpectrogram);
    addAndMakeVisible (processedSpectrogram);
    addAndMakeVisible (reductionPanel);
    addAndMakeVisible (actions);
    addAndMakeVisible (originalButton);
    addAndMakeVisible (processedButton);

    timeScrollBar.setAutoHide (true);
    timeScrollBar.addListener (this);

    // Buttons never take keyboard focus, otherwise a focused button would swallow Space / Return.
    for (auto* b : { &originalButton, &processedButton, &actions.processButton, &actions.playButton })
        b->setWantsKeyboardFocus (false);

    reductionPanel.alphaSlider.setValue (settings.restoreAlpha (1.0), juce::dontSendNotification);
    reductionPanel.alphaSlider.onValueChange = [this]
    {
        settings.saveAlpha (reductionPanel.alphaSlider.getValue());
        markStale();
    };

    originalButton.setRadioGroupId (1);
    processedButton.setRadioGroupId (1);
    originalButton.setClickingTogglesState (true);
    processedButton.setClickingTogglesState (true);
    originalButton.setToggleState (true, juce::dontSendNotification);
    originalButton.onClick = [this] { commandManager.invokeDirectly (CommandIDs::listenOriginal, true); };
    processedButton.onClick = [this] { commandManager.invokeDirectly (CommandIDs::listenProcessed, true); };
    actions.processButton.onClick = [this] { commandManager.invokeDirectly (CommandIDs::applyNoiseReduction, true); };
    actions.playButton.onClick = [this] { commandManager.invokeDirectly (CommandIDs::playPause, true); };

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff6e6e73));
    statusLabel.setJustificationType (juce::Justification::centred);
    refreshStatus();

    if (playback.getDeviceError().isNotEmpty())   // plan §8: show it; processing and saving still work
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
        {
            if (safeThis != nullptr)
                safeThis->showError ("The audio output device could not be opened, so playback is unavailable "
                                     "(processing and saving still work).\n\n" + safeThis->playback.getDeviceError());
        });

    waveform.onSeek = [this] (double seconds) { playback.setPositionSeconds (seconds); };
    waveform.onRegionSelected = [this] (double start, double end) { useNoiseRegion (start, end); };
    noiseProfileView.onLoadNoiseFile = [this] { commandManager.invokeDirectly (CommandIDs::openNoiseFile, true); };

   #if JUCE_MAC
    appMenuExtras.addCommandItem (&commandManager, CommandIDs::showAbout);
    appMenuExtras.addSeparator();
    appMenuExtras.addCommandItem (&commandManager, CommandIDs::showKeyboardShortcuts);
    juce::MenuBarModel::setMacMainMenu (&menuModel, &appMenuExtras);
   #endif

    updateButtons();
    setSize (1440, 900);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);   // before menuModel is destroyed
   #endif

    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
        top->removeKeyListener (commandManager.getKeyMappings());

    commandManager.getKeyMappings()->removeChangeListener (this);
    viewState.removeChangeListener (this);
    playback.getDeviceManager().removeChangeListener (this);
    stopTimer();
    cancelRequested = true;
    if (worker != nullptr)
        worker->stopThread (5000);
    if (loader != nullptr)
        loader->stopThread (10000);
}

void MainComponent::parentHierarchyChanged()
{
    // Also listen on the window so shortcuts work even if some child holds keyboard focus.
    if (auto* top = getTopLevelComponent(); top != nullptr && top != this)
    {
        top->removeKeyListener (commandManager.getKeyMappings());
        top->addKeyListener (commandManager.getKeyMappings());
    }
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppLookAndFeel::window);
    g.setColour (AppLookAndFeel::textPrimary);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawImage (logo, juce::Rectangle<float> (24.0f, 20.0f, 52.0f, 52.0f), juce::RectanglePlacement::centred);
    g.drawText ("Snoise Editor", 84, 24, 260, 44, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (24);
    auto bar = area.removeFromTop (44);

    bar.removeFromLeft (332);
    processedButton.setBounds (bar.removeFromRight (100).withSizeKeepingCentre (100, 36));
    originalButton.setBounds (bar.removeFromRight (100).withSizeKeepingCentre (100, 36));
    bar.removeFromLeft (16);
    bar.removeFromRight (16);
    statusLabel.setBounds (bar);

    const int gap = 14;
    const int usable = area.getHeight() - 3 * gap;
    area.removeFromTop (gap);
    waveform.setBounds (area.removeFromTop (usable * (spectrogramsVisible ? 20 : 58) / 100));
    timeScrollBar.setBounds (area.removeFromTop (12));
    area.removeFromTop (gap - 4);

    auto spectrograms = area.removeFromTop (usable * (spectrogramsVisible ? 38 : 0) / 100);
    originalSpectrogram.setVisible (spectrogramsVisible);
    processedSpectrogram.setVisible (spectrogramsVisible);
    originalSpectrogram.setBounds (spectrograms.removeFromLeft ((spectrograms.getWidth() - gap) / 2));
    spectrograms.removeFromLeft (gap);
    processedSpectrogram.setBounds (spectrograms);

    if (spectrogramsVisible)
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

    if (followPlayheadEnabled && playback.isPlaying())
    {
        const auto visible = viewState.getVisibleRange();
        if (position > visible.getStart() + 0.9 * visible.getLength() || position < visible.getStart())
            viewState.setVisibleRange (visible.movedToStartAt (position - 0.1 * visible.getLength()));
    }

    if (statusHoldTicks > 0 && --statusHoldTicks == 0 && ! isProcessing && ! isLoading)
        refreshStatus();

    if (isProcessing)
        actions.processButton.setButtonText ("Cancel  " + juce::String (juce::roundToInt (progress.load() * 100.0f)) + "%");

    const bool playing = playback.isPlaying();
    if ((actions.playButton.getButtonText() == "Stop") != playing)
        updateButtons();
}

void MainComponent::updateButtons()
{
    const bool hasAudio = input.buffer.getNumSamples() > 0;
    const bool busy = isProcessing || isLoading;
    waveform.setInterceptsMouseClicks (! busy, ! busy);
    actions.playButton.setEnabled (hasAudio);
    commandManager.commandStatusChanged();   // refresh menu items, ticks and shortcuts-dependent state
    actions.playButton.setButtonText (playback.isPlaying() ? "Stop" : "Play");
    noiseProfileView.setLoadEnabled (hasAudio && ! busy);

    const bool canProcess = hasAudio && ! noiseProfiles.empty();
    actions.processButton.setEnabled (isProcessing || (canProcess && ! isLoading));
    actions.processButton.setTooltip (! hasAudio ? "Load an audio file first"
                              : noiseProfiles.empty() ? "Drag on the waveform to mark noise, or load a noise file"
                                                      : "");
    actions.processButton.setColour (juce::TextButton::buttonColourId,
                             stale && canProcess && ! isProcessing ? AppLookAndFeel::accent : AppLookAndFeel::card);
    actions.processButton.setColour (juce::TextButton::textColourOffId,
                                     stale && canProcess && ! isProcessing ? juce::Colours::white : AppLookAndFeel::textPrimary);
    if (! isProcessing)
        actions.processButton.setButtonText ("Process");

    processedButton.setEnabled (hasProcessed && ! busy);
    originalButton.setEnabled (hasAudio && ! busy);
    reductionPanel.alphaSlider.setEnabled (! busy);
    originalButton.setToggleState (! processedSelected, juce::dontSendNotification);
    processedButton.setToggleState (processedSelected, juce::dontSendNotification);
}

void MainComponent::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Snoise Editor", message);
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

        loadInputFile (file);
    });
}

void MainComponent::chooseAndSaveFile (std::function<void()> afterSave)
{
    chooser = std::make_unique<juce::FileChooser> ("Save as WAV", inputFile.withFileExtension ("wav"), "*.wav");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, afterSave] (const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file == juce::File())
            return;

        saveCurrent (file.withFileExtension ("wav"), processed, afterSave);
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

        loadNoiseFile (file);
    });
}

void MainComponent::markStale()
{
    stale = true;
    ++settingsVersion;
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
    const unsigned versionAtStart = settingsVersion;

    // The job only touches copies, so the UI stays free to change while it runs.
    worker = std::make_unique<JobThread> ([this, outcome, versionAtStart,
                                           safeThis = juce::Component::SafePointer<MainComponent> (this),
                                           audioIn = input.buffer,
                                           profiles = noiseProfiles,
                                           alpha = static_cast<float> (reductionPanel.alphaSlider.getValue())]
    {
        const NoiseReductionEngine jobEngine;
        outcome->result = jobEngine.process (audioIn, profiles, alpha, outcome->audio,
                                             [this] (float p) { progress = p; }, cancelRequested);

        juce::MessageManager::callAsync ([safeThis, outcome, versionAtStart]
        {
            if (safeThis != nullptr)
                safeThis->finishProcessing (outcome->result, std::move (outcome->audio), versionAtStart);
        });
    });
    worker->startThread();
}

void MainComponent::finishProcessing (juce::Result result, juce::AudioBuffer<float> processedAudio, unsigned settingsAtStart)
{
    if (worker != nullptr)
        worker->stopThread (5000);   // the job has finished; this just joins it
    worker.reset();
    isProcessing = false;

    if (result.wasOk())
    {
        processed = std::move (processedAudio);
        hasProcessed = true;
        processedExported = false;
        stale = settingsVersion != settingsAtStart;   // settings changed while it ran
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

void MainComponent::saveCurrent (const juce::File& file, juce::AudioBuffer<float> audio, std::function<void()> afterSave)
{
    auto write = [safeThis = juce::Component::SafePointer<MainComponent> (this), file, afterSave] (const juce::AudioBuffer<float>& toWrite)
    {
        if (safeThis == nullptr)
            return;

        const auto result = AudioFileManager::saveWav (file, toWrite, safeThis->input.sampleRate);

        if (result.failed())
            safeThis->showError (result.getErrorMessage());
        else
        {
            safeThis->processedExported = true;
            safeThis->setStatus ("Exported " + file.getFileName());

            if (afterSave)
                afterSave();
        }
    };

    const float peak = AudioFileManager::getPeak (audio);

    if (peak <= 1.0f)
    {
        write (audio);
        return;
    }

    // Overlap-add of modified frames can overshoot full scale slightly (plan §7.3).
    const auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel (
        juce::MessageBoxIconType::WarningIcon, "Snoise Editor",
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

void MainComponent::refreshStatus()
{
    if (! hasInputAudio())
    {
       #if JUCE_MAC
        statusLabel.setText ("No file loaded - press Cmd+O or drop a file", juce::dontSendNotification);
       #else
        statusLabel.setText ("No file loaded - press Ctrl+O or drop a file", juce::dontSendNotification);
       #endif
        return;
    }

    const int totalSeconds = juce::roundToInt (input.buffer.getNumSamples() / input.sampleRate);
    const auto channels = input.buffer.getNumChannels() == 1 ? juce::String ("mono")
                        : input.buffer.getNumChannels() == 2 ? juce::String ("stereo")
                                                             : juce::String (input.buffer.getNumChannels()) + " ch";

    statusLabel.setText (inputFile.getFileName() + "  -  " + juce::String (input.sampleRate / 1000.0, 1) + " kHz  -  "
                             + juce::String (totalSeconds / 60) + ":" + juce::String (totalSeconds % 60).paddedLeft ('0', 2)
                             + "  -  " + channels,
                         juce::dontSendNotification);
}

void MainComponent::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusHoldTicks = 120;   // ~4 s at 30 Hz, then the file info comes back
}

void MainComponent::startLoad (const juce::File& file, bool isNoiseFile)
{
    if (isLoading || isProcessing)
        return;

    isLoading = true;
    statusLabel.setText ("Loading " + file.getFileName() + "...", juce::dontSendNotification);
    updateButtons();

    auto outcome = std::make_shared<LoadOutcome>();

    // Decoding (and, for a noise file, the profile estimate) happens off the message thread (plan §7.1).
    loader = std::make_unique<JobThread> ([outcome, file, isNoiseFile, inputRate = input.sampleRate,
                                           safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        outcome->result = AudioFileManager::load (file, outcome->audio);

        if (isNoiseFile && outcome->result.wasOk())
            outcome->result = NoiseReductionEngine::checkSampleRates (inputRate, outcome->audio.sampleRate);

        if (isNoiseFile && outcome->result.wasOk())
            outcome->result = NoiseReductionEngine().estimateProfile (outcome->audio.buffer, outcome->profiles);

        juce::MessageManager::callAsync ([safeThis, outcome, file, isNoiseFile]
        {
            if (safeThis != nullptr)
                safeThis->finishLoad (file, isNoiseFile, *outcome);
        });
    });
    loader->startThread();
}

void MainComponent::finishLoad (const juce::File& file, bool isNoiseFile, LoadOutcome& outcome)
{
    if (loader != nullptr)
        loader->stopThread (5000);   // the job has finished; this just joins it
    loader.reset();
    isLoading = false;

    if (outcome.result.failed())
    {
        refreshStatus();
        updateButtons();
        showError (outcome.result.getErrorMessage());
        return;
    }

    if (isNoiseFile)
    {
        noiseProfiles = std::move (outcome.profiles);
        waveform.clearRegion();
        showProfile ("File " + file.getFileName());
        refreshStatus();

        if (outcome.audio.numNonFiniteReplaced > 0)
            showError (juce::String (outcome.audio.numNonFiniteReplaced)
                       + " invalid (NaN/Inf) samples in the noise file were replaced with silence.");
        return;
    }

    playback.stop();
    input = std::move (outcome.audio);
    inputFile = file;
    viewState.setTotalLength (input.buffer.getNumSamples() / input.sampleRate);
    recentFiles.addFile (file);
    settings.saveRecentFiles (recentFiles);
    processedExported = true;
    playback.setAudio (input.buffer, input.sampleRate);
    waveform.setAudio (input.buffer, input.sampleRate);
    originalSpectrogram.setAudio (input.buffer, input.sampleRate);
    processedSpectrogram.clear();
    clearNoiseProfile();
    hasProcessed = processedSelected = false;
    stale = true;
    ++settingsVersion;
    originalButton.setToggleState (true, juce::dontSendNotification);
    refreshStatus();
    updateButtons();

    grabKeyboardFocus();

    if (input.numNonFiniteReplaced > 0)
        showError (juce::String (input.numNonFiniteReplaced) + " invalid (NaN/Inf) samples were replaced with silence.");
}

// ---- Commands ------------------------------------------------------------------------------------------------

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    using namespace CommandIDs;
    c.addArray ({ openAudio, openNoiseFile, exportProcessed, closeFile,
                  clearSelection, CommandIDs::clearNoiseProfile, resetAllParameters,
                  zoomIn, zoomOut, zoomToFit, zoomToSelection, followPlayhead, toggleSpectrograms,
                  applyNoiseReduction, alphaUp, alphaDown, alphaUpFine, alphaDownFine, alphaReset,
                  playPause, stop, returnToStart, listenOriginal, listenProcessed, CommandIDs::toggleListenSource,
                  showKeyboardShortcuts, CommandIDs::showAbout, CommandIDs::showAudioSettings });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using namespace CommandIDs;
    namespace Cat = CommandCategories;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const bool busy = isProcessing || isLoading;
    const bool canProcess = hasInputAudio() && ! noiseProfiles.empty();
    const double alpha = reductionPanel.alphaSlider.getValue();

    switch (id)
    {
        case openAudio:
            info.setInfo ("Open Audio...", "Load the noisy recording to clean", Cat::file, 0);
            info.addDefaultKeypress ('o', cmd);
            info.setActive (! busy);
            break;

        case openNoiseFile:
            info.setInfo ("Open Noise File...", "Load a noise-only recording for the profile", Cat::file, 0);
            info.addDefaultKeypress ('o', cmd | shift);
            info.setActive (hasInputAudio() && ! busy);
            break;

        case exportProcessed:
            info.setInfo ("Export Processed Audio...", "Save the cleaned audio as WAV", Cat::file, 0);
            info.addDefaultKeypress ('e', cmd);
            info.setActive (hasProcessed && ! stale && ! busy);
            break;

        case closeFile:
            info.setInfo ("Close File", "Close the current recording", Cat::file, 0);
            info.addDefaultKeypress ('w', cmd);
            info.setActive (hasInputAudio() && ! busy);
            break;

        case clearSelection:
            info.setInfo ("Clear Selection", "Remove the marked noise region", Cat::edit, 0);
            info.addDefaultKeypress (juce::KeyPress::escapeKey, 0);
            info.setActive (waveform.hasSelection());
            break;

        case CommandIDs::clearNoiseProfile:
            info.setInfo ("Clear Noise Profile", "Forget the learned noise profile", Cat::edit, 0);
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, cmd);
            info.setActive (! noiseProfiles.empty() && ! busy);
            break;

        case resetAllParameters:
            info.setInfo ("Reset All Parameters", "Restore the default settings", Cat::edit, 0);
            info.setActive (! busy);
            break;

        case zoomIn:
            info.setInfo ("Zoom In", "Show less time, more detail", Cat::view, 0);
            info.addDefaultKeypress ('=', cmd);
            info.addDefaultKeypress ('+', cmd);
            info.setActive (hasInputAudio() && viewState.canZoomIn());
            break;

        case zoomOut:
            info.setInfo ("Zoom Out", "Show more time", Cat::view, 0);
            info.addDefaultKeypress ('-', cmd);
            info.setActive (hasInputAudio() && viewState.canZoomOut());
            break;

        case zoomToFit:
            info.setInfo ("Zoom to Fit", "Show the whole recording", Cat::view, 0);
            info.addDefaultKeypress ('0', cmd);
            info.setActive (hasInputAudio());
            break;

        case zoomToSelection:
            info.setInfo ("Zoom to Selection", "Zoom to the marked noise region", Cat::view, 0);
            info.addDefaultKeypress ('z', 0);
            info.setActive (waveform.hasSelection());
            break;

        case followPlayhead:
            info.setInfo ("Follow Playhead", "Scroll the view along with playback", Cat::view, 0);
            info.addDefaultKeypress ('f', 0);
            info.setActive (hasInputAudio());
            info.setTicked (followPlayheadEnabled);
            break;

        case toggleSpectrograms:
            info.setInfo ("Show Spectrograms", "Show or hide the spectrogram panels", Cat::view, 0);
            info.addDefaultKeypress ('s', cmd | shift);
            info.setTicked (spectrogramsVisible);
            break;

        case applyNoiseReduction:
            info.setInfo (isProcessing ? "Cancel Processing" : "Apply Noise Reduction",
                          "Run spectral subtraction with the current settings", Cat::process, 0);
            info.addDefaultKeypress (juce::KeyPress::returnKey, cmd);
            info.setActive (isProcessing || (canProcess && ! isLoading));
            break;

        case alphaUp:
            info.setInfo ("Increase Alpha", "Stronger noise reduction (+0.1)", Cat::process, 0);
            info.addDefaultKeypress (']', 0);
            info.setActive (! busy);
            break;

        case alphaDown:
            info.setInfo ("Decrease Alpha", "Gentler noise reduction (-0.1)", Cat::process, 0);
            info.addDefaultKeypress ('[', 0);
            info.setActive (! busy);
            break;

        case alphaUpFine:
            info.setInfo ("Increase Alpha (Fine)", "+0.01", Cat::process, 0);
            info.addDefaultKeypress (']', shift);
            info.addDefaultKeypress ('}', shift);
            info.setActive (! busy);
            break;

        case alphaDownFine:
            info.setInfo ("Decrease Alpha (Fine)", "-0.01", Cat::process, 0);
            info.addDefaultKeypress ('[', shift);
            info.addDefaultKeypress ('{', shift);
            info.setActive (! busy);
            break;

        case alphaReset:
            info.setInfo ("Reset Alpha", "Back to 1.0", Cat::process, 0);
            info.addDefaultKeypress ('\\', 0);
            info.setActive (! busy && ! juce::approximatelyEqual (alpha, 1.0));
            break;

        case playPause:
            info.setInfo (playback.isPlaying() ? "Pause" : "Play", "Start or pause playback", Cat::playback, 0);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            info.setActive (hasInputAudio());
            break;

        case stop:
            info.setInfo ("Stop", "Stop playback", Cat::playback, 0);
            info.addDefaultKeypress ('.', 0);
            info.setActive (playback.isPlaying());
            break;

        case returnToStart:
            info.setInfo ("Return to Start", "Move the playhead to the start", Cat::playback, 0);
            info.addDefaultKeypress (juce::KeyPress::homeKey, 0);
            info.addDefaultKeypress (juce::KeyPress::returnKey, 0);
            info.setActive (hasInputAudio());
            break;

        case listenOriginal:
            info.setInfo ("Listen to Original", "Play the unprocessed input", Cat::playback, 0);
            info.addDefaultKeypress ('1', 0);
            info.setActive (hasInputAudio() && ! busy);
            info.setTicked (! processedSelected);
            break;

        case listenProcessed:
            info.setInfo ("Listen to Processed", "Play the noise-reduced audio", Cat::playback, 0);
            info.addDefaultKeypress ('2', 0);
            info.setActive (hasProcessed && ! busy);
            info.setTicked (processedSelected);
            break;

        case CommandIDs::toggleListenSource:
            info.setInfo ("Toggle Original / Processed", "Switch between the two versions", Cat::playback, 0);
            info.addDefaultKeypress (juce::KeyPress::tabKey, 0);
            info.setActive (hasProcessed && ! busy);
            break;

        case showKeyboardShortcuts:
            info.setInfo ("Keyboard Shortcuts...", "View and change keyboard shortcuts", Cat::help, 0);
            info.addDefaultKeypress (',', cmd);
            break;

        case CommandIDs::showAbout:
            info.setInfo ("About Snoise Editor", "About this application", Cat::help, 0);
            break;

        case CommandIDs::showAudioSettings:
            info.setInfo ("Audio Settings...", "Choose the audio output device and its settings", Cat::options, 0);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    using namespace CommandIDs;
    auto& alphaSlider = reductionPanel.alphaSlider;

    switch (info.commandID)
    {
        case openAudio:               chooseAndLoadFile();                                    break;
        case openNoiseFile:           chooseAndLoadNoiseFile();                               break;
        case exportProcessed:         chooseAndSaveFile();                                    break;
        case closeFile:               closeCurrentFile();                                     break;

        case clearSelection:          waveform.clearRegion();                                 break;
        case CommandIDs::clearNoiseProfile:     clearNoiseProfile(); markStale();                       break;
        case resetAllParameters:      alphaSlider.setValue (1.0);                             break;

        case zoomIn:                  viewState.zoomAround (zoomAnchor(), 2.0);               break;
        case zoomOut:                 viewState.zoomAround (zoomAnchor(), 0.5);               break;
        case zoomToFit:               viewState.zoomToFit();                                  break;
        case zoomToSelection:
        {
            const auto sel = waveform.getSelection();
            const auto pad = sel.getLength() * 0.05;
            viewState.setVisibleRange ({ sel.getStart() - pad, sel.getEnd() + pad });
            break;
        }
        case followPlayhead:          followPlayheadEnabled = ! followPlayheadEnabled;        break;
        case toggleSpectrograms:      spectrogramsVisible = ! spectrogramsVisible; resized(); repaint(); break;

        case applyNoiseReduction:
            if (isProcessing)
                cancelRequested = true;
            else
                startProcessing();
            break;

        case alphaUp:                 nudgeAlpha (+1, false);                                 break;
        case alphaDown:               nudgeAlpha (-1, false);                                 break;
        case alphaUpFine:             nudgeAlpha (+1, true);                                  break;
        case alphaDownFine:           nudgeAlpha (-1, true);                                  break;
        case alphaReset:              alphaSlider.setValue (1.0);
                                      setStatus ("alpha " + juce::String (alphaSlider.getValue(), 2));   break;

        case playPause:               togglePlayback();                                       break;
        case stop:                    playback.stop(); updateButtons();                       break;
        case returnToStart:           playback.setPositionSeconds (0.0);                      break;
        case listenOriginal:          selectVersion (false); updateButtons();                 break;
        case listenProcessed:         selectVersion (true);  updateButtons();                 break;
        case CommandIDs::toggleListenSource:      toggleListenSource();                                   break;

        case showKeyboardShortcuts:   showShortcutEditor();                                   break;
        case CommandIDs::showAbout:               showAbout();                                            break;
        case CommandIDs::showAudioSettings:       showAudioSettings();                                    break;

        default: return false;
    }

    commandManager.commandStatusChanged();
    return true;
}

void MainComponent::togglePlayback()
{
    if (playback.isPlaying())
        playback.stop();
    else
        playback.play();

    updateButtons();
}

void MainComponent::toggleListenSource()
{
    selectVersion (! processedSelected);
    updateButtons();
}

void MainComponent::nudgeAlpha (int direction, bool fine)
{
    auto& slider = reductionPanel.alphaSlider;
    const double step = fine ? 0.01 : 0.1;
    // Round to 0.01 so repeated steps never drift (1.0000000002 and the like).
    const double value = std::round ((slider.getValue() + direction * step) * 100.0) / 100.0;
    slider.setValue (juce::jlimit (slider.getMinimum(), slider.getMaximum(), value));
    setStatus ("alpha " + juce::String (slider.getValue(), 2));
}

double MainComponent::zoomAnchor() const
{
    const auto visible = viewState.getVisibleRange();
    const double playhead = playback.getPositionSeconds();
    return visible.contains (playhead) ? playhead : visible.getStart() + 0.5 * visible.getLength();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == commandManager.getKeyMappings())
        settings.saveKeyMappings (*commandManager.getKeyMappings());
    else if (source == &playback.getDeviceManager())
    {
        if (auto state = playback.getDeviceManager().createStateXml())
            settings.saveAudioDeviceState (*state);
    }
    else if (source == &viewState)
    {
        updateScrollBar();
        commandManager.commandStatusChanged();
    }
}

void MainComponent::updateScrollBar()
{
    timeScrollBar.setRangeLimits (0.0, juce::jmax (0.001, viewState.getTotalLength()), juce::dontSendNotification);
    timeScrollBar.setCurrentRange (viewState.getVisibleRange().getStart(), viewState.getVisibleRange().getLength(),
                                   juce::dontSendNotification);
}

void MainComponent::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    viewState.setVisibleRange (viewState.getVisibleRange().movedToStartAt (newRangeStart));
}

void MainComponent::showShortcutEditor()
{
    auto* editor = new juce::KeyMappingEditorComponent (*commandManager.getKeyMappings(), true);
    editor->setSize (560, 620);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor);
    o.dialogTitle = "Keyboard Shortcuts";
    o.dialogBackgroundColour = AppLookAndFeel::window;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

void MainComponent::showAudioSettings()
{
    // Output only: the app plays audio back and never records. No MIDI options.
    auto* selector = new juce::AudioDeviceSelectorComponent (playback.getDeviceManager(), 0, 0, 0, 2,
                                                             false, false, true, false);
    selector->setSize (500, 360);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = AppLookAndFeel::window;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Snoise Editor",
                                            "Spectral-subtraction noise reduction.\n\n"
                                            "Mark a noise-only section (or load a noise file), then press Process.");
}

// ---- Loading, closing and the unsaved-work guard -------------------------------------------------------------

void MainComponent::confirmDiscardIfNeeded (std::function<void()> proceed)
{
    if (! hasProcessed || processedExported)
    {
        proceed();
        return;
    }

    const auto options = juce::MessageBoxOptions()
                             .withIconType (juce::MessageBoxIconType::QuestionIcon)
                             .withTitle ("Discard processed audio?")
                             .withMessage ("The processed audio has not been exported.")
                             .withButton ("Export...")
                             .withButton ("Discard")
                             .withButton ("Cancel");

    juce::AlertWindow::showAsync (options, [safeThis = juce::Component::SafePointer<MainComponent> (this), proceed] (int result)
    {
        if (safeThis == nullptr)
            return;

        if (result == 1)
            safeThis->chooseAndSaveFile (proceed);   // export first, then carry on
        else if (result == 2)
            proceed();
        // 0 = cancel
    });
}

void MainComponent::loadInputFile (const juce::File& file)
{
    if (isLoading || isProcessing)
        return;

    confirmDiscardIfNeeded ([safeThis = juce::Component::SafePointer<MainComponent> (this), file]
    {
        if (safeThis != nullptr)
            safeThis->startLoad (file, false);
    });
}

void MainComponent::loadNoiseFile (const juce::File& file)
{
    if (hasInputAudio())
        startLoad (file, true);
}

void MainComponent::closeCurrentFile()
{
    confirmDiscardIfNeeded ([safeThis = juce::Component::SafePointer<MainComponent> (this)]
    {
        auto* self = safeThis.getComponent();
        if (self == nullptr)
            return;

        self->playback.clear();
        self->input = {};
        self->inputFile = juce::File();
        self->processed = {};
        self->hasProcessed = self->processedSelected = false;
        self->processedExported = true;
        self->stale = true;
        ++self->settingsVersion;
        self->waveform.setAudio (juce::AudioBuffer<float> (1, 0), 44100.0);
        self->originalSpectrogram.clear();
        self->processedSpectrogram.clear();
        self->clearNoiseProfile();
        self->viewState.setTotalLength (0.0);
        self->refreshStatus();
        self->updateButtons();
    });
}

// ---- Drag and drop -------------------------------------------------------------------------------------------

juce::Rectangle<int> MainComponent::inputDropArea() const
{
    return getLocalBounds().withBottom (noiseProfileView.getY());
}

MainComponent::DropZone MainComponent::dropZoneAt (juce::Point<int> p) const
{
    if (noiseProfileView.getBounds().contains (p))
        return hasInputAudio() ? DropZone::noise : DropZone::input;   // no input yet: treat it as the input

    if (inputDropArea().contains (p))
        return DropZone::input;

    return DropZone::none;
}

bool MainComponent::isSupportedAudioFile (const juce::File& f) const
{
    static const auto extensions = [] { juce::AudioFormatManager m; m.registerBasicFormats(); return m.getWildcardForAllFormats() + ";"; }();
    return f.existsAsFile() && extensions.containsIgnoreCase ("*" + f.getFileExtension() + ";");
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (isProcessing || isLoading)
        return false;

    for (auto& path : files)
        if (isSupportedAudioFile (juce::File (path)))
            return true;

    return false;
}

void MainComponent::fileDragEnter (const juce::StringArray&, int x, int y) { updateDropZone ({ x, y }); }
void MainComponent::fileDragMove (const juce::StringArray&, int x, int y)  { updateDropZone ({ x, y }); }
void MainComponent::fileDragExit (const juce::StringArray&)                { updateDropZone ({ -1, -1 }); }

void MainComponent::updateDropZone (juce::Point<int> p)
{
    const auto zone = dropZoneAt (p);

    if (zone != currentDropZone)
    {
        currentDropZone = zone;
        repaint();
    }
}

void MainComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto zone = dropZoneAt ({ x, y });
    currentDropZone = DropZone::none;
    repaint();

    juce::File chosen;
    int supportedCount = 0;

    for (auto& path : files)
        if (const juce::File f (path); isSupportedAudioFile (f) && supportedCount++ == 0)
            chosen = f;

    if (! chosen.existsAsFile() || zone == DropZone::none)
        return;

    const bool noiseDroppedWithoutInput = ! hasInputAudio() && noiseProfileView.getBounds().contains (juce::Point<int> (x, y));

    if (zone == DropZone::noise)
        loadNoiseFile (chosen);
    else
        loadInputFile (chosen);

    if (noiseDroppedWithoutInput)
        setStatus ("Loaded as input - load a recording before adding a noise file");
    else if (supportedCount > 1)
        setStatus ("Loaded " + chosen.getFileName() + " (other dropped files ignored)");
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (currentDropZone == DropZone::none)
        return;

    const auto area = (currentDropZone == DropZone::noise ? noiseProfileView.getBounds() : inputDropArea()).toFloat();
    const auto label = currentDropZone == DropZone::noise ? "Drop to load as noise profile" : "Drop to load as input audio";

    g.setColour (AppLookAndFeel::accent.withAlpha (0.10f));
    g.fillRoundedRectangle (area, 12.0f);
    g.setColour (AppLookAndFeel::accent);
    g.drawRoundedRectangle (area.reduced (1.0f), 12.0f, 2.0f);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (label, area, juce::Justification::centred);
}
