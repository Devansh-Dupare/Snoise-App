#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "StftConfig.h"

// Inverse FFT, synthesis window, overlap-add and sum(wa * ws) normalisation (plan §5, brief §4.6).
class AudioReconstructor
{
public:
    AudioReconstructor (const StftConfig&, std::vector<float> analysisWindow);

    // Clears the accumulators for a signal of paddedLength samples.
    void prepare (int paddedLength);

    // fftData holds bins 0..N/2 at [0, N + 1] (size 2N). The upper half is overwritten with the
    // conjugate mirror, so the buffer is modified.
    void addFrame (float* fftData, int frameIndex);

    // y[n] = numerator / denominator for n in [startOffset, startOffset + numSamples).
    void finalise (float* output, int startOffset, int numSamples) const;

    // Measured at construction (plan §6.4): 1 if JUCE's inverse already divides by N, else 1 / N.
    float getIfftScale() const { return ifftScale; }
    const std::vector<float>& getDenominator() const { return denominator; }

private:
    void measureIfftScale();

    StftConfig config;
    juce::dsp::FFT fft;
    std::vector<float> analysisWindow, synthesisWindow;
    std::vector<float> numerator, denominator;
    float ifftScale = 1.0f;
};
