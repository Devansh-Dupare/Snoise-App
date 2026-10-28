#include "AudioReconstructor.h"

#include <algorithm>
#include <cmath>

AudioReconstructor::AudioReconstructor (const StftConfig& cfg, std::vector<float> window)
    : config (cfg),
      fft (cfg.fftOrder),
      analysisWindow (std::move (window)),
      synthesisWindow (analysisWindow)
{
    measureIfftScale();
}

void AudioReconstructor::prepare (int paddedLength)
{
    numerator.assign (static_cast<size_t> (paddedLength), 0.0f);
    denominator.assign (static_cast<size_t> (paddedLength), 0.0f);
}

static void mirrorConjugateBins (float* data, int n)
{
    for (int k = 1; k < n / 2; ++k)
    {
        data[2 * (n - k)]     =  data[2 * k];
        data[2 * (n - k) + 1] = -data[2 * k + 1];
    }
}

void AudioReconstructor::addFrame (float* fftData, int frameIndex)
{
    const int n     = config.fftSize();
    const int start = frameIndex * config.hopSize;
    jassert (start + n <= static_cast<int> (numerator.size()));

    mirrorConjugateBins (fftData, n);
    fft.performRealOnlyInverseTransform (fftData);

    for (int r = 0; r < n; ++r)
    {
        const auto i = static_cast<size_t> (start + r);
        numerator[i]   += synthesisWindow[static_cast<size_t> (r)] * fftData[r] * ifftScale;
        denominator[i] += analysisWindow[static_cast<size_t> (r)] * synthesisWindow[static_cast<size_t> (r)];
    }
}

void AudioReconstructor::finalise (float* output, int startOffset, int numSamples) const
{
    for (int i = 0; i < numSamples; ++i)
    {
        const auto idx = static_cast<size_t> (startOffset + i);
        const float den = denominator[idx];
        const float y = den < 1.0e-6f ? 0.0f : numerator[idx] / den;
        output[i] = std::isfinite (y) ? y : 0.0f;
    }
}

void AudioReconstructor::measureIfftScale()
{
    const int n = config.fftSize();
    std::vector<float> original (static_cast<size_t> (n)), data (static_cast<size_t> (2 * n), 0.0f);

    juce::Random random (1234);
    for (auto& v : original)
        v = random.nextFloat() * 2.0f - 1.0f;

    std::copy (original.begin(), original.end(), data.begin());
    fft.performRealOnlyForwardTransform (data.data(), true);
    mirrorConjugateBins (data.data(), n);
    fft.performRealOnlyInverseTransform (data.data());

    // Least-squares scale s such that s * result == original.
    double xy = 0.0, yy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        xy += static_cast<double> (original[static_cast<size_t> (i)]) * data[static_cast<size_t> (i)];
        yy += static_cast<double> (data[static_cast<size_t> (i)]) * data[static_cast<size_t> (i)];
    }

    ifftScale = yy > 0.0 ? static_cast<float> (xy / yy) : 1.0f;
}
