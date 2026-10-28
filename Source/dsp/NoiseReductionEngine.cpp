#include "NoiseReductionEngine.h"
#include "FrameProcessor.h"

#include <cmath>

juce::Result NoiseReductionEngine::estimateProfile (const juce::AudioBuffer<float>& noise,
                                                    std::vector<NoiseProfile>& profilesOut) const
{
    return estimate (noise.getArrayOfReadPointers(), noise.getNumChannels(), noise.getNumSamples(), profilesOut);
}

juce::Result NoiseReductionEngine::estimateProfileFromRegion (const juce::AudioBuffer<float>& source, int startSample,
                                                              int numSamples, std::vector<NoiseProfile>& profilesOut) const
{
    const int start = juce::jlimit (0, source.getNumSamples(), startSample);
    const int length = juce::jlimit (0, source.getNumSamples() - start, numSamples);

    std::vector<const float*> channels (static_cast<size_t> (source.getNumChannels()));
    for (int ch = 0; ch < source.getNumChannels(); ++ch)
        channels[static_cast<size_t> (ch)] = source.getReadPointer (ch) + start;

    return estimate (channels.data(), source.getNumChannels(), length, profilesOut);
}

juce::Result NoiseReductionEngine::checkSampleRates (double inputRate, double noiseRate)
{
    if (std::abs (inputRate - noiseRate) > 0.5)
        return juce::Result::fail ("Noise file must use the same sample rate as the input ("
                                   + juce::String (inputRate, 0) + " Hz, but the noise file is "
                                   + juce::String (noiseRate, 0) + " Hz).");

    return juce::Result::ok();
}

juce::Result NoiseReductionEngine::estimate (const float* const* channels, int numChannels, int numSamples,
                                             std::vector<NoiseProfile>& profilesOut) const
{
    profilesOut.clear();

    const int numFrames = numSamples < config.fftSize() ? 0 : 1 + (numSamples - config.fftSize()) / config.hopSize;

    if (numFrames == 0)
        return juce::Result::fail ("Select at least ~0.1 s of noise (at least "
                                   + juce::String (config.fftSize()) + " samples); 0.5 s or more is recommended.");

    const FrameProcessor analyser (config);
    std::vector<float> frame (static_cast<size_t> (2 * config.fftSize()));

    profilesOut.resize (static_cast<size_t> (numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& profile = profilesOut[static_cast<size_t> (ch)];
        profile.reset (config.numBins());

        for (int m = 0; m < numFrames; ++m)
        {
            analyser.analyseFrame (channels[ch], numSamples, m, frame.data());
            profile.addFrame (frame.data());
        }

        profile.finalise();
    }

    return juce::Result::ok();
}
