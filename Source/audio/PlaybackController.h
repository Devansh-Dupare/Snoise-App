#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Device -> AudioSourcePlayer -> AudioTransportSource -> MemoryAudioSource (plan §7.2).
// Message-thread use only.
class PlaybackController
{
public:
    // `savedDeviceState` is a previous AudioDeviceManager::createStateXml(); null (or an unavailable
    // device) falls back to the system default output.
    explicit PlaybackController (const juce::XmlElement* savedDeviceState = nullptr);
    ~PlaybackController();

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // Empty string when the audio device opened; otherwise the reason it did not.
    const juce::String& getDeviceError() const { return deviceError; }

    // Copies the audio, so the caller's buffer may change afterwards.
    void setAudio (const juce::AudioBuffer<float>& audio, double sampleRate);
    // Swaps the audio but keeps the playback position and play state (A/B switching, plan §7.2).
    void replaceAudio (const juce::AudioBuffer<float>& audio, double sampleRate);
    void clear();
    bool hasAudio() const { return memorySource != nullptr; }

    void play();
    void stop();
    bool isPlaying() const { return transport.isPlaying(); }

    double getPositionSeconds() const { return transport.getCurrentPosition(); }
    void setPositionSeconds (double seconds) { transport.setPosition (seconds); }
    double getLengthSeconds() const { return transport.getLengthInSeconds(); }

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer player;
    juce::AudioTransportSource transport;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<juce::MemoryAudioSource> memorySource;
    juce::String deviceError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaybackController)
};
