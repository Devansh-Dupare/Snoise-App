#include "Spectrogram.h"
#include "FrameProcessor.h"

#include <algorithm>
#include <cmath>

SpectrogramData computeSpectrogram (const juce::AudioBuffer<float>& audio, const StftConfig& config,
                                    int maxFrames, const std::atomic<bool>* shouldCancel)
{
    SpectrogramData data;
    const int length = audio.getNumSamples();

    if (length == 0 || audio.getNumChannels() == 0 || maxFrames < 1)
        return data;

    std::vector<float> mono (static_cast<size_t> (length), 0.0f);
    const float channelWeight = 1.0f / static_cast<float> (audio.getNumChannels());

    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
    {
        const float* in = audio.getReadPointer (ch);
        for (int i = 0; i < length; ++i)
            if (std::isfinite (in[i]))
                mono[static_cast<size_t> (i)] += in[i] * channelWeight;
    }

    const FrameProcessor analyser (config);
    const int n = config.fftSize();
    const int totalFrames = length < n ? 1 : 1 + (length - n) / config.hopSize;
    const int stride = (totalFrames + maxFrames - 1) / maxFrames;

    double windowSum = 0.0;
    for (const float w : analyser.getAnalysisWindow())
        windowSum += w;
    const auto scale = static_cast<float> (2.0 / windowSum);

    data.numFrames = (totalFrames + stride - 1) / stride;
    data.numBins = config.numBins();
    data.decibels.resize (static_cast<size_t> (data.numFrames) * static_cast<size_t> (data.numBins));

    std::vector<float> frame (static_cast<size_t> (2 * n));

    for (int m = 0; m < data.numFrames; ++m)
    {
        if (shouldCancel != nullptr && shouldCancel->load())
            return {};

        analyser.analyseFrame (mono.data(), length, m * stride, frame.data());

        for (int k = 0; k < data.numBins; ++k)
        {
            const float magnitude = std::hypot (frame[static_cast<size_t> (2 * k)], frame[static_cast<size_t> (2 * k + 1)]);
            data.decibels[static_cast<size_t> (m * data.numBins + k)] = 20.0f * std::log10 (magnitude * scale + 1.0e-9f);
        }
    }

    return data;
}
