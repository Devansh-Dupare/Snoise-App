#include "NoiseReductionEngine.h"
#include "AudioReconstructor.h"
#include "FrameProcessor.h"
#include "SpectralSubtractor.h"

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

juce::Result NoiseReductionEngine::process (const juce::AudioBuffer<float>& input,
                                            const std::vector<NoiseProfile>& profiles,
                                            float alpha,
                                            juce::AudioBuffer<float>& output,
                                            const Progress& progress,
                                            const std::atomic<bool>& shouldCancel) const
{
    if (input.getNumSamples() == 0 || input.getNumChannels() == 0)
        return juce::Result::fail ("There is no input audio to process.");

    if (profiles.empty())
        return juce::Result::fail ("Select a noise region or load a noise file first.");

    for (const auto& profile : profiles)
        if (! profile.isReady() || static_cast<int> (profile.getAverageMagnitude().size()) != config.numBins())
            return juce::Result::fail ("The noise profile is not valid; select the noise again.");

    const int n = config.fftSize();
    const int numSamples = input.getNumSamples();
    const int padded = config.paddedLength (numSamples);
    const int frontPad = n - config.hopSize;
    const int numFrames = config.numFrames (padded);

    SpectralSubtractor subtractor;
    subtractor.setAlpha (alpha);

    const FrameProcessor analyser (config);
    AudioReconstructor reconstructor (config, analyser.getAnalysisWindow());

    // All buffers are allocated once, outside the frame loop.
    std::vector<float> paddedSignal (static_cast<size_t> (padded));
    std::vector<float> frame (static_cast<size_t> (2 * n));
    juce::AudioBuffer<float> result (input.getNumChannels(), numSamples);

    const int numChannels = input.getNumChannels();
    const int totalFrames = numChannels * numFrames;
    int framesDone = 0;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto& profile = profiles[ch < static_cast<int> (profiles.size()) ? static_cast<size_t> (ch) : 0];

        std::fill (paddedSignal.begin(), paddedSignal.end(), 0.0f);
        const float* in = input.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            paddedSignal[static_cast<size_t> (frontPad + i)] = in[i];   // analyseFrame sanitises

        reconstructor.prepare (padded);

        for (int m = 0; m < numFrames; ++m)
        {
            if (shouldCancel.load())
                return juce::Result::fail ("Processing was cancelled.");

            analyser.analyseFrame (paddedSignal.data(), padded, m, frame.data());
            subtractor.process (frame.data(), profile, config.numBins());
            reconstructor.addFrame (frame.data(), m);

            ++framesDone;
            if (progress && (m % 16 == 0 || m == numFrames - 1))
                progress (static_cast<float> (framesDone) / static_cast<float> (totalFrames));
        }

        reconstructor.finalise (result.getWritePointer (ch), frontPad, numSamples);
    }

    output = std::move (result);
    return juce::Result::ok();
}
