#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "StftConfig.h"

// Framing + analysis window + forward FFT (plan §5, brief §4.1).
class FrameProcessor
{
public:
    explicit FrameProcessor (const StftConfig&);

    // Periodic Hann (divides by N, not N - 1) so Hann^2 at 75% overlap sums to a constant.
    static std::vector<float> makePeriodicHann (int size);

    // Copies signal[frameIndex * H + r] * wa[r] (zero beyond numSamples) into fftData
    // (size 2N, zero-padded) and runs the forward real FFT. Bin k ends up at fftData[2k], fftData[2k + 1].
    void analyseFrame (const float* signal, int numSamples, int frameIndex, float* fftData) const;

    const std::vector<float>& getAnalysisWindow() const { return analysisWindow; }

private:
    StftConfig config;
    juce::dsp::FFT fft;
    std::vector<float> analysisWindow;
};
