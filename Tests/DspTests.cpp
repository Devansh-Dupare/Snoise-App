#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "dsp/StftConfig.h"
#include "dsp/FrameProcessor.h"
#include "dsp/AudioReconstructor.h"

struct StftConfigTests : public juce::UnitTest
{
    StftConfigTests() : juce::UnitTest ("StftConfig", "SNC") {}

    void runTest() override
    {
        StftConfig config;

        beginTest ("bin count and frame count");
        expectEquals (config.numBins(), 1025);

        for (int length : { 1, 2047, 2048, 44100, 100001 })
        {
            const int padded = config.paddedLength (length);
            expect (padded >= length + 2 * (config.fftSize() - config.hopSize));
            expectEquals ((padded - config.fftSize()) % config.hopSize, 0);
            expect (config.numFrames (padded) >= 1);
        }
    }
};

static StftConfigTests stftConfigTests;

// Runs the full analysis -> (no modification) -> synthesis chain on a signal.
static std::vector<float> runIdentityPipeline (const StftConfig& config, const std::vector<float>& input)
{
    const int n = config.fftSize();
    const int length = static_cast<int> (input.size());
    const int padded = config.paddedLength (length);
    const int frontPad = n - config.hopSize;

    std::vector<float> paddedSignal (static_cast<size_t> (padded), 0.0f);
    std::copy (input.begin(), input.end(), paddedSignal.begin() + frontPad);

    FrameProcessor analyser (config);
    AudioReconstructor reconstructor (config, analyser.getAnalysisWindow());
    reconstructor.prepare (padded);

    std::vector<float> frame (static_cast<size_t> (2 * n));
    for (int m = 0; m < config.numFrames (padded); ++m)
    {
        analyser.analyseFrame (paddedSignal.data(), padded, m, frame.data());
        reconstructor.addFrame (frame.data(), m);
    }

    std::vector<float> output (input.size());
    reconstructor.finalise (output.data(), frontPad, length);
    return output;
}

struct StftEngineTests : public juce::UnitTest
{
    StftEngineTests() : juce::UnitTest ("STFT engine", "SNC") {}

    void runTest() override
    {
        StftConfig config;

        beginTest ("IFFT scale is measured (1 or 1/N)");
        {
            FrameProcessor analyser (config);
            AudioReconstructor reconstructor (config, analyser.getAnalysisWindow());
            const float s = reconstructor.getIfftScale();
            logMessage ("Measured ifftScale = " + juce::String (s, 8));
            expect (std::abs (s - 1.0f) < 1.0e-4f || std::abs (s - 1.0f / (float) config.fftSize()) < 1.0e-8f);
        }

        beginTest ("STFT identity (alpha = 0): output == input");
        for (int length : { 5000, 10000, 44100 })
        {
            juce::Random random (42 + length);
            std::vector<float> input (static_cast<size_t> (length));
            for (auto& v : input)
                v = random.nextFloat() * 2.0f - 1.0f;

            const auto output = runIdentityPipeline (config, input);
            float maxError = 0.0f;
            for (size_t i = 0; i < input.size(); ++i)
                maxError = std::max (maxError, std::abs (output[i] - input[i]));

            logMessage ("length " + juce::String (length) + " max error " + juce::String (maxError, 9));
            expect (maxError < 1.0e-4f);
        }

        beginTest ("Window normalisation: sum(wa * ws) == 1.5 over the unpadded region");
        {
            const int length = 10000;
            const int padded = config.paddedLength (length);
            const int frontPad = config.fftSize() - config.hopSize;

            FrameProcessor analyser (config);
            AudioReconstructor reconstructor (config, analyser.getAnalysisWindow());
            reconstructor.prepare (padded);

            std::vector<float> frame (static_cast<size_t> (2 * config.fftSize()), 0.0f);
            for (int m = 0; m < config.numFrames (padded); ++m)
                reconstructor.addFrame (frame.data(), m);

            float worst = 0.0f;
            for (int i = 0; i < length; ++i)
                worst = std::max (worst, std::abs (reconstructor.getDenominator()[static_cast<size_t> (frontPad + i)] - 1.5f));

            expect (worst < 1.0e-4f);
        }
    }
};

static StftEngineTests stftEngineTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (runner.getResult (i)->failures > 0)
            return 1;

    return 0;
}
