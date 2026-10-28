#include "NoiseReductionEngine.h"
#include "FrameProcessor.h"

juce::Result NoiseReductionEngine::estimateProfile (const juce::AudioBuffer<float>& noise,
                                                    std::vector<NoiseProfile>& profilesOut) const
{
    profilesOut.clear();

    const int numSamples = noise.getNumSamples();
    const int numFrames = numSamples < config.fftSize() ? 0 : 1 + (numSamples - config.fftSize()) / config.hopSize;

    if (numFrames == 0)
        return juce::Result::fail ("Select at least ~0.1 s of noise (at least "
                                   + juce::String (config.fftSize()) + " samples); 0.5 s or more is recommended.");

    const FrameProcessor analyser (config);
    std::vector<float> frame (static_cast<size_t> (2 * config.fftSize()));

    profilesOut.resize (static_cast<size_t> (noise.getNumChannels()));

    for (int ch = 0; ch < noise.getNumChannels(); ++ch)
    {
        auto& profile = profilesOut[static_cast<size_t> (ch)];
        profile.reset (config.numBins());

        for (int m = 0; m < numFrames; ++m)
        {
            analyser.analyseFrame (noise.getReadPointer (ch), numSamples, m, frame.data());
            profile.addFrame (frame.data());
        }

        profile.finalise();
    }

    return juce::Result::ok();
}
