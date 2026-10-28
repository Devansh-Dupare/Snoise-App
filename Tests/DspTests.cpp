#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "dsp/StftConfig.h"
#include "dsp/FrameProcessor.h"
#include "dsp/AudioReconstructor.h"
#include "dsp/NoiseProfile.h"
#include "dsp/NoiseReductionEngine.h"
#include <random>

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

struct NoiseProfileTests : public juce::UnitTest
{
    NoiseProfileTests() : juce::UnitTest ("Noise profile", "SNC") {}

    void runTest() override
    {
        const StftConfig config;

        beginTest ("white noise of known RMS gives a flat noise profile; M matches the frame count");
        {
            const float sigma = 0.1f;
            const int length = 88200;
            std::mt19937 rng (1234);
            std::normal_distribution<float> dist (0.0f, sigma);

            juce::AudioBuffer<float> noise (1, length);
            for (int i = 0; i < length; ++i)
                noise.setSample (0, i, dist (rng));

            std::vector<NoiseProfile> profiles;
            const auto result = NoiseReductionEngine (config).estimateProfile (noise, profiles);
            expect (result.wasOk());
            expectEquals (static_cast<int> (profiles.size()), 1);

            const auto& profile = profiles[0];
            expect (profile.isReady());
            expectEquals (profile.getFrameCount(), 1 + (length - config.fftSize()) / config.hopSize);

            // E|V| = sigma * sqrt(sum(w^2)) * sqrt(pi) / 2, sum(w^2) = 3N/8 for a periodic Hann.
            const double expected = sigma * std::sqrt (3.0 * config.fftSize() / 8.0) * std::sqrt (juce::MathConstants<double>::pi) / 2.0;
            const auto& avg = profile.getAverageMagnitude();

            double meanOverBins = 0.0;
            float worstRelative = 0.0f;
            for (int k = 1; k < config.numBins() - 1; ++k)   // DC and Nyquist are real-only
            {
                meanOverBins += avg[static_cast<size_t> (k)];
                worstRelative = std::max (worstRelative, static_cast<float> (std::abs (avg[static_cast<size_t> (k)] / expected - 1.0)));
            }
            meanOverBins /= (config.numBins() - 2);

            logMessage ("expected " + juce::String (expected) + ", mean " + juce::String (meanOverBins)
                        + ", worst per-bin relative error " + juce::String (worstRelative));
            expect (std::abs (meanOverBins / expected - 1.0) < 0.02);
            expect (worstRelative < 0.4f);
        }

        beginTest ("noise shorter than N is rejected (M = 0)");
        {
            std::vector<NoiseProfile> profiles;
            juce::AudioBuffer<float> shortNoise (1, config.fftSize() - 1);
            shortNoise.clear();
            expect (NoiseReductionEngine (config).estimateProfile (shortNoise, profiles).failed());

            juce::AudioBuffer<float> exact (1, config.fftSize());
            exact.clear();
            expect (NoiseReductionEngine (config).estimateProfile (exact, profiles).wasOk());
            expectEquals (profiles[0].getFrameCount(), 1);
        }

        beginTest ("stereo noise gives one profile per channel");
        {
            juce::AudioBuffer<float> stereo (2, 8192);
            stereo.clear();
            std::vector<NoiseProfile> profiles;
            expect (NoiseReductionEngine (config).estimateProfile (stereo, profiles).wasOk());
            expectEquals (static_cast<int> (profiles.size()), 2);
        }
    }
};

static NoiseProfileTests noiseProfileTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (runner.getResult (i)->failures > 0)
            return 1;

    return 0;
}
