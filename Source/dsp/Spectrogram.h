#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include "StftConfig.h"

// dB magnitude spectrogram for drawing only (plan §9); the processing path never uses dB.
struct SpectrogramData
{
    int numFrames = 0;
    int numBins = 0;
    std::vector<float> decibels;   // frame-major: decibels[frame * numBins + bin]

    float at (int frame, int bin) const { return decibels[static_cast<size_t> (frame * numBins + bin)]; }
};

// Channels are averaged. Magnitudes are normalised by half the window sum, so a full-scale sine reads
// about 0 dB. Files with more than maxFrames STFT frames are decimated by skipping frames.
// Returns an empty result if `shouldCancel` becomes true.
SpectrogramData computeSpectrogram (const juce::AudioBuffer<float>& audio, const StftConfig& config,
                                    int maxFrames = 4096, const std::atomic<bool>* shouldCancel = nullptr);
