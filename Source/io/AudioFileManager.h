#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

// Load / save audio files (plan §4.2, §8). No GUI code: FileChooser lives in the UI layer.
namespace AudioFileManager
{
    struct LoadedAudio
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
        int numNonFiniteReplaced = 0;   // NaN/Inf samples replaced with 0 after loading
    };

    // Fails for missing, unreadable, unsupported, corrupt or zero-length files.
    juce::Result load (const juce::File& file, LoadedAudio& out);

    // Writes a WAV at the given sample rate (16, 24 or 32 bit; 32 bit is float).
    juce::Result saveWav (const juce::File& file, const juce::AudioBuffer<float>& buffer,
                          double sampleRate, int bitsPerSample = 24);

    // Largest absolute sample value across all channels.
    float getPeak (const juce::AudioBuffer<float>& buffer);
}
