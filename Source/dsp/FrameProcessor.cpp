#include "FrameProcessor.h"

#include <algorithm>
#include <cmath>

FrameProcessor::FrameProcessor (const StftConfig& cfg)
    : config (cfg),
      fft (cfg.fftOrder),
      analysisWindow (makePeriodicHann (cfg.fftSize()))
{
}

std::vector<float> FrameProcessor::makePeriodicHann (int size)
{
    std::vector<float> w (static_cast<size_t> (size));

    for (int r = 0; r < size; ++r)
        w[static_cast<size_t> (r)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * r / size));

    return w;
}

// NaN/Inf become 0 and huge values are limited, so one bad sample cannot poison a whole frame (plan §8).
static float sanitise (float v)
{
    return std::isfinite (v) ? std::clamp (v, -FrameProcessor::maxSampleMagnitude, FrameProcessor::maxSampleMagnitude) : 0.0f;
}

void FrameProcessor::analyseFrame (const float* signal, int numSamples, int frameIndex, float* fftData) const
{
    const int n     = config.fftSize();
    const int start = frameIndex * config.hopSize;
    const int avail = std::clamp (numSamples - start, 0, n);

    for (int r = 0; r < avail; ++r)
        fftData[r] = sanitise (signal[start + r]) * analysisWindow[static_cast<size_t> (r)];

    std::fill (fftData + avail, fftData + 2 * n, 0.0f);

    fft.performRealOnlyForwardTransform (fftData, true);
}
