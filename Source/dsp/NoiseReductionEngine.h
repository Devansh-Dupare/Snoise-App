#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include "NoiseProfile.h"
#include "StftConfig.h"

// Runs the pipeline per channel (plan §4.1). Phase 3: noise-profile estimation only.
class NoiseReductionEngine
{
public:
    explicit NoiseReductionEngine (const StftConfig& cfg = {}) : config (cfg) {}

    // One profile per noise channel, using the same N, H and window as processing (plan §6.6).
    // Frames come straight from the noise samples (no padding), so M = 1 + (len - N) / H.
    // Fails when the noise is shorter than N samples.
    juce::Result estimateProfile (const juce::AudioBuffer<float>& noise,
                                  std::vector<NoiseProfile>& profilesOut) const;

private:
    StftConfig config;
};
