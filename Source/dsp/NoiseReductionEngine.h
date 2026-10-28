#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <functional>
#include <vector>
#include "NoiseProfile.h"
#include "StftConfig.h"

// Runs the pipeline per channel (plan §4.1): profile estimation and offline spectral subtraction.
class NoiseReductionEngine
{
public:
    explicit NoiseReductionEngine (const StftConfig& cfg = {}) : config (cfg) {}

    // One profile per noise channel, using the same N, H and window as processing (plan §6.6).
    // Frames come straight from the noise samples (no padding), so M = 1 + (len - N) / H.
    // Fails when the noise is shorter than N samples.
    juce::Result estimateProfile (const juce::AudioBuffer<float>& noise,
                                  std::vector<NoiseProfile>& profilesOut) const;

    // Same, for the samples [startSample, startSample + numSamples) of `source` (a waveform selection).
    // The range is clamped to the buffer.
    juce::Result estimateProfileFromRegion (const juce::AudioBuffer<float>& source, int startSample, int numSamples,
                                            std::vector<NoiseProfile>& profilesOut) const;

    using Progress = std::function<void (float)>;   // 0..1, called from the processing thread

    // Analyse -> subtract -> overlap-add for every channel. Channel c uses profiles[c]; if the noise
    // had fewer channels, channel 0's profile is reused (plan §6.7). Non-finite input samples are
    // treated as 0. `output` is resized to match `input`. Fails on bad arguments or when cancelled.
    juce::Result process (const juce::AudioBuffer<float>& input,
                          const std::vector<NoiseProfile>& profiles,
                          float alpha,
                          juce::AudioBuffer<float>& output,
                          const Progress& progress,
                          const std::atomic<bool>& shouldCancel) const;

    // A noise file must use the input's sample rate (plan §8).
    static juce::Result checkSampleRates (double inputRate, double noiseRate);

private:
    juce::Result estimate (const float* const* channels, int numChannels, int numSamples,
                           std::vector<NoiseProfile>& profilesOut) const;

    StftConfig config;
};
