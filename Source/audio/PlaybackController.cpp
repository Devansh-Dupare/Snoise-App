#include "PlaybackController.h"

PlaybackController::PlaybackController()
{
    deviceError = deviceManager.initialiseWithDefaultDevices (0, 2);
    player.setSource (&transport);
    deviceManager.addAudioCallback (&player);
}

PlaybackController::~PlaybackController()
{
    transport.stop();
    deviceManager.removeAudioCallback (&player);
    player.setSource (nullptr);
    transport.setSource (nullptr);
}

void PlaybackController::setAudio (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    transport.stop();
    transport.setSource (nullptr);
    memorySource.reset();

    buffer = audio;
    memorySource = std::make_unique<juce::MemoryAudioSource> (buffer, false, false);
    transport.setSource (memorySource.get(), 0, nullptr, sampleRate);
}

void PlaybackController::play()
{
    if (! hasAudio())
        return;

    if (transport.getCurrentPosition() >= transport.getLengthInSeconds())
        transport.setPosition (0.0);

    transport.start();
}

void PlaybackController::stop()
{
    transport.stop();
}

void PlaybackController::replaceAudio (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    const bool wasPlaying = transport.isPlaying();
    const double position = transport.getCurrentPosition();

    setAudio (audio, sampleRate);
    transport.setPosition (position);

    if (wasPlaying)
        transport.start();
}
