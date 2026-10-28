#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include "dsp/StftConfig.h"
#include "dsp/FrameProcessor.h"
#include "dsp/AudioReconstructor.h"
#include "dsp/NoiseProfile.h"
#include "dsp/NoiseReductionEngine.h"
#include "dsp/SpectralSubtractor.h"
#include "dsp/Spectrogram.h"
#include "io/AudioFileManager.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <random>

// ---- Heap allocation counter (plan §10 / phase 6: no allocations in the frame loop) ----
static std::atomic<long> gAllocationCount { 0 };

void* operator new (std::size_t size)
{
    ++gAllocationCount;

    if (void* p = std::malloc (size > 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

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

struct AudioFileManagerTests : public juce::UnitTest
{
    AudioFileManagerTests() : juce::UnitTest ("AudioFileManager", "SNC") {}

    void runTest() override
    {
        juce::TemporaryFile temp (".wav");
        const auto file = temp.getFile();

        beginTest ("save then load returns the same audio (24-bit)");
        {
            juce::AudioBuffer<float> original (2, 10000);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < original.getNumSamples(); ++i)
                    original.setSample (ch, i, 0.8f * std::sin (0.01f * static_cast<float> (i) * static_cast<float> (ch + 1)));

            expect (AudioFileManager::saveWav (file, original, 48000.0, 24).wasOk());

            AudioFileManager::LoadedAudio loaded;
            expect (AudioFileManager::load (file, loaded).wasOk());
            expectEquals (loaded.sampleRate, 48000.0);
            expectEquals (loaded.buffer.getNumChannels(), 2);
            expectEquals (loaded.buffer.getNumSamples(), 10000);
            expectEquals (loaded.numNonFiniteReplaced, 0);

            float worst = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 10000; ++i)
                    worst = std::max (worst, std::abs (loaded.buffer.getSample (ch, i) - original.getSample (ch, i)));

            logMessage ("max round-trip error " + juce::String (worst));
            expect (worst < 1.0e-6f);
        }

        beginTest ("missing, corrupt and empty files are rejected");
        {
            AudioFileManager::LoadedAudio loaded;
            expect (AudioFileManager::load (file.getSiblingFile ("does_not_exist_snc.wav"), loaded).failed());

            file.replaceWithText ("this is not audio");
            expect (AudioFileManager::load (file, loaded).failed());

            juce::AudioBuffer<float> empty (1, 0);
            expect (AudioFileManager::saveWav (file, empty, 44100.0).failed());

            file.deleteFile();
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (file.createOutputStream().release(), 44100.0, 1, 16, {}, 0));
                expect (writer != nullptr);
            }
            expect (AudioFileManager::load (file, loaded).failed());   // valid header, zero samples
        }

        beginTest ("NaN/Inf samples are replaced with 0 and counted");
        {
            juce::AudioBuffer<float> bad (1, 100);
            bad.clear();
            bad.setSample (0, 10, std::numeric_limits<float>::quiet_NaN());
            bad.setSample (0, 20, std::numeric_limits<float>::infinity());
            bad.setSample (0, 30, 0.5f);

            expect (AudioFileManager::saveWav (file, bad, 44100.0, 32).wasOk());

            AudioFileManager::LoadedAudio loaded;
            expect (AudioFileManager::load (file, loaded).wasOk());
            expectEquals (loaded.numNonFiniteReplaced, 2);
            expectEquals (loaded.buffer.getSample (0, 10), 0.0f);
            expectEquals (loaded.buffer.getSample (0, 20), 0.0f);
            expectEquals (loaded.buffer.getSample (0, 30), 0.5f);
        }

        beginTest ("saving to an unwritable location fails");
        expect (AudioFileManager::saveWav (juce::File ("/nonexistent_dir_snc/out.wav"),
                                           juce::AudioBuffer<float> (1, 10), 44100.0).failed());

        beginTest ("getPeak");
        {
            juce::AudioBuffer<float> b (2, 4);
            b.clear();
            b.setSample (1, 2, -0.75f);
            expectEquals (AudioFileManager::getPeak (b), 0.75f);
        }
    }
};

static AudioFileManagerTests audioFileManagerTests;

struct NoiseSelectionTests : public juce::UnitTest
{
    NoiseSelectionTests() : juce::UnitTest ("Noise selection", "SNC") {}

    void runTest() override
    {
        const StftConfig config;
        const NoiseReductionEngine engine (config);

        beginTest ("a region profile only sees the selected samples");
        {
            // Silence, then loud noise from sample 20000 on.
            juce::AudioBuffer<float> source (1, 40000);
            source.clear();
            std::mt19937 rng (7);
            std::normal_distribution<float> dist (0.0f, 0.1f);
            for (int i = 20000; i < 40000; ++i)
                source.setSample (0, i, dist (rng));

            std::vector<NoiseProfile> quiet, loud;
            expect (engine.estimateProfileFromRegion (source, 0, 10000, quiet).wasOk());
            expect (engine.estimateProfileFromRegion (source, 25000, 10000, loud).wasOk());
            expectEquals (quiet[0].getFrameCount(), 1 + (10000 - config.fftSize()) / config.hopSize);
            expectEquals (quiet[0].getAverageMagnitude()[100], 0.0f);
            expect (loud[0].getAverageMagnitude()[100] > 1.0f);
        }

        beginTest ("region shorter than N is rejected; out-of-range regions are clamped");
        {
            juce::AudioBuffer<float> source (1, 10000);
            source.clear();
            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfileFromRegion (source, 100, config.fftSize() - 1, profiles).failed());
            expect (engine.estimateProfileFromRegion (source, 9000, 5000, profiles).failed());     // clamps to 1000 samples
            expect (engine.estimateProfileFromRegion (source, -500, 20000, profiles).wasOk());     // clamps to whole buffer
            expect (engine.estimateProfileFromRegion (source, 0, 0, profiles).failed());
        }

        beginTest ("noise file with a different sample rate is rejected");
        {
            expect (NoiseReductionEngine::checkSampleRates (44100.0, 44100.0).wasOk());
            expect (NoiseReductionEngine::checkSampleRates (44100.0, 48000.0).failed());
        }
    }
};

static NoiseSelectionTests noiseSelectionTests;

// ---- Phase 4: spectral subtraction (plan §10.1 tests 5-7) ----
static juce::AudioBuffer<float> makeWhiteNoise (int channels, int length, float sigma, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> dist (0.0f, sigma);
    juce::AudioBuffer<float> b (channels, length);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < length; ++i)
            b.setSample (ch, i, dist (rng));
    return b;
}

static double rms (const juce::AudioBuffer<float>& b, int channel = 0)
{
    double sum = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        sum += static_cast<double> (b.getSample (channel, i)) * b.getSample (channel, i);
    return std::sqrt (sum / juce::jmax (1, b.getNumSamples()));
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

struct SpectralSubtractionTests : public juce::UnitTest
{
    SpectralSubtractionTests() : juce::UnitTest ("Spectral subtraction", "SNC") {}

    juce::Result run (const NoiseReductionEngine& engine, const juce::AudioBuffer<float>& in,
                      const std::vector<NoiseProfile>& profiles, float alpha, juce::AudioBuffer<float>& out)
    {
        const std::atomic<bool> noCancel { false };
        return engine.process (in, profiles, alpha, out, {}, noCancel);
    }

    void runTest() override
    {
        const StftConfig config;
        const NoiseReductionEngine engine (config);

        beginTest ("subtractor formula: gain, clamping and phase preservation");
        {
            NoiseProfile profile;
            profile.reset (config.numBins());
            std::vector<float> frame (static_cast<size_t> (2 * config.fftSize()), 0.0f);
            // bin 0: |X| = 5, noise 2 -> alpha 1 leaves 3; bin 1: |X| = 1, noise 2 -> clamped to 0
            frame[0] = 3.0f; frame[1] = 4.0f;
            frame[2] = 1.0f; frame[3] = 0.0f;
            std::vector<float> noiseFrame (frame.size(), 0.0f);
            noiseFrame[0] = 2.0f;
            noiseFrame[2] = 2.0f;
            profile.addFrame (noiseFrame.data());
            profile.finalise();

            SpectralSubtractor sub;
            sub.setAlpha (1.0f);
            sub.process (frame.data(), profile, config.numBins());
            expectWithinAbsoluteError (frame[0], 1.8f, 1.0e-5f);   // 3 * (3/5)
            expectWithinAbsoluteError (frame[1], 2.4f, 1.0e-5f);   // 4 * (3/5): same phase as before
            expectEquals (frame[2], 0.0f);
            sub.setAlpha (100.0f);
            expectEquals (sub.getAlpha(), SpectralSubtractor::maxAlpha);
            sub.setAlpha (-1.0f);
            expectEquals (sub.getAlpha(), 0.0f);
        }

        beginTest ("alpha = 0 through the engine reproduces the input (mono and stereo)");
        {
            auto input = makeWhiteNoise (2, 30000, 0.1f, 3);
            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (makeWhiteNoise (1, 20000, 0.05f, 4), profiles).wasOk());

            juce::AudioBuffer<float> out;
            expect (run (engine, input, profiles, 0.0f, out).wasOk());
            expectEquals (out.getNumChannels(), 2);
            expectEquals (out.getNumSamples(), 30000);

            float worst = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 30000; ++i)
                    worst = std::max (worst, std::abs (out.getSample (ch, i) - input.getSample (ch, i)));
            expect (worst < 1.0e-4f);
        }

        beginTest ("test 5: subtracting a noise-only signal with alpha = 1 drops its RMS by at least 8 dB");
        {
            const auto noise = makeWhiteNoise (1, 88200, 0.1f, 5);
            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (noise, profiles).wasOk());

            juce::AudioBuffer<float> out;
            expect (run (engine, noise, profiles, 1.0f, out).wasOk());
            const double dropDb = 20.0 * std::log10 (rms (out) / rms (noise));
            logMessage ("noise-only RMS change at alpha 1: " + juce::String (dropDb, 2) + " dB");
            // Bin magnitudes of white noise are Rayleigh distributed, so subtracting only their mean leaves
            // E[(R - mean)+^2] / E[R^2] ~ 12.6% of the energy: about -9 dB in theory, not -10.
            expect (dropDb < -8.0);
        }

        beginTest ("test 6: SNR improves for a 1 kHz sine in white noise");
        {
            const double fs = 44100.0;
            const int length = 88200;
            juce::AudioBuffer<float> clean (1, length);
            for (int i = 0; i < length; ++i)
                clean.setSample (0, i, 0.3f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / fs)));

            auto noise = makeWhiteNoise (1, length, 0.1f, 6);
            juce::AudioBuffer<float> noisy (clean);
            noisy.addFrom (0, 0, noise, 0, 0, length);

            std::vector<NoiseProfile> profiles;   // profile from independent noise of the same level
            expect (engine.estimateProfile (makeWhiteNoise (1, 44100, 0.1f, 99), profiles).wasOk());

            juce::AudioBuffer<float> out;
            expect (run (engine, noisy, profiles, 1.8f, out).wasOk());

            juce::AudioBuffer<float> residualBefore (noisy), residualAfter (out);
            residualBefore.addFrom (0, 0, clean, 0, 0, length, -1.0f);
            residualAfter.addFrom (0, 0, clean, 0, 0, length, -1.0f);
            const double snrBefore = 20.0 * std::log10 (rms (clean) / rms (residualBefore));
            const double snrAfter = 20.0 * std::log10 (rms (clean) / rms (residualAfter));
            logMessage ("SNR before " + juce::String (snrBefore, 2) + " dB, after " + juce::String (snrAfter, 2) + " dB");
            expect (snrAfter > snrBefore + 3.0);
        }

        beginTest ("test 7: edge cases (short, silent, stereo, NaN input)");
        {
            std::vector<NoiseProfile> mono, stereo;
            expect (engine.estimateProfile (makeWhiteNoise (1, 20000, 0.1f, 8), mono).wasOk());
            expect (engine.estimateProfile (makeWhiteNoise (2, 20000, 0.1f, 9), stereo).wasOk());

            juce::AudioBuffer<float> out;

            auto shortInput = makeWhiteNoise (1, 100, 0.1f, 10);        // shorter than N
            expect (run (engine, shortInput, mono, 1.8f, out).wasOk());
            expectEquals (out.getNumSamples(), 100);
            expect (allFinite (out));

            juce::AudioBuffer<float> one (1, 1);
            one.clear();
            expect (run (engine, one, mono, 1.8f, out).wasOk());
            expectEquals (out.getNumSamples(), 1);

            juce::AudioBuffer<float> silent (1, 10000);
            silent.clear();
            expect (run (engine, silent, mono, 5.0f, out).wasOk());
            expect (allFinite (out));
            expectEquals (out.getMagnitude (0, 10000), 0.0f);

            const auto stereoInput = makeWhiteNoise (2, 15000, 0.1f, 11);
            expect (run (engine, stereoInput, stereo, 1.8f, out).wasOk());   // per-channel profiles
            expectEquals (out.getNumChannels(), 2);
            expect (allFinite (out));
            expect (run (engine, stereoInput, mono, 1.8f, out).wasOk());     // channel 0's profile reused
            expect (allFinite (out));

            auto bad = makeWhiteNoise (1, 8000, 0.1f, 12);
            bad.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
            bad.setSample (0, 200, std::numeric_limits<float>::infinity());
            expect (run (engine, bad, mono, 1.8f, out).wasOk());
            expect (allFinite (out));
        }

        beginTest ("invalid arguments and cancellation are reported");
        {
            std::vector<NoiseProfile> none, notReady (1);
            notReady[0].reset (config.numBins());   // M = 0
            const auto input = makeWhiteNoise (1, 5000, 0.1f, 13);
            juce::AudioBuffer<float> out;
            expect (run (engine, input, none, 1.0f, out).failed());
            expect (run (engine, input, notReady, 1.0f, out).failed());

            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (makeWhiteNoise (1, 8000, 0.1f, 14), profiles).wasOk());
            expect (run (engine, juce::AudioBuffer<float> (1, 0), profiles, 1.0f, out).failed());

            const std::atomic<bool> cancelled { true };
            expect (engine.process (input, profiles, 1.0f, out, {}, cancelled).failed());

            float lastProgress = 0.0f;
            const std::atomic<bool> noCancel { false };
            expect (engine.process (input, profiles, 1.0f, out, [&] (float p) { lastProgress = p; }, noCancel).wasOk());
            expectWithinAbsoluteError (lastProgress, 1.0f, 1.0e-6f);
        }
    }
};

static SpectralSubtractionTests spectralSubtractionTests;

struct SpectrogramTests : public juce::UnitTest
{
    SpectrogramTests() : juce::UnitTest ("Spectrogram", "SNC") {}

    void runTest() override
    {
        const StftConfig config;

        beginTest ("a full-scale sine peaks near 0 dB in the right bin");
        {
            const double fs = 44100.0;
            const int bin = 46;                                   // bin-centred frequency
            const double freq = bin * fs / config.fftSize();
            juce::AudioBuffer<float> sine (1, 44100);
            for (int i = 0; i < sine.getNumSamples(); ++i)
                sine.setSample (0, i, static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / fs)));

            const auto data = computeSpectrogram (sine, config);
            expectEquals (data.numBins, config.numBins());
            expectEquals (data.numFrames, 1 + (44100 - config.fftSize()) / config.hopSize);

            const int m = data.numFrames / 2;
            int peakBin = 0;
            for (int k = 1; k < data.numBins; ++k)
                if (data.at (m, k) > data.at (m, peakBin))
                    peakBin = k;

            logMessage ("peak bin " + juce::String (peakBin) + " at " + juce::String (data.at (m, peakBin), 2) + " dB");
            expectEquals (peakBin, bin);
            expect (std::abs (data.at (m, peakBin)) < 0.5f);
        }

        beginTest ("silence is very quiet and long files are decimated to maxFrames");
        {
            juce::AudioBuffer<float> silent (2, 200000);
            silent.clear();
            const auto data = computeSpectrogram (silent, config, 100);
            expect (data.numFrames <= 100 && data.numFrames > 50);
            for (const float db : data.decibels)
                expect (db < -150.0f);
        }

        beginTest ("inputs shorter than N and empty inputs");
        {
            juce::AudioBuffer<float> tiny (1, 10);
            tiny.clear();
            expectEquals (computeSpectrogram (tiny, config).numFrames, 1);
            expectEquals (computeSpectrogram (juce::AudioBuffer<float> (1, 0), config).numFrames, 0);

            const std::atomic<bool> cancelled { true };
            juce::AudioBuffer<float> some (1, 20000);
            some.clear();
            expectEquals (computeSpectrogram (some, config, 4096, &cancelled).numFrames, 0);
        }
    }
};

static SpectrogramTests spectrogramTests;

struct RobustnessTests : public juce::UnitTest
{
    RobustnessTests() : juce::UnitTest ("Robustness", "SNC") {}

    void runTest() override
    {
        const StftConfig config;
        const NoiseReductionEngine engine (config);
        const std::atomic<bool> noCancel { false };

        beginTest ("the per-frame loop performs no heap allocations");
        {
            const int length = 50000;
            const int padded = config.paddedLength (length);
            const int n = config.fftSize();

            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (makeWhiteNoise (1, 20000, 0.1f, 21), profiles).wasOk());

            std::vector<float> signal (static_cast<size_t> (padded), 0.1f), frame (static_cast<size_t> (2 * n));
            const FrameProcessor analyser (config);
            AudioReconstructor reconstructor (config, analyser.getAnalysisWindow());
            reconstructor.prepare (padded);
            SpectralSubtractor subtractor;

            const long before = gAllocationCount.load();
            for (int m = 0; m < config.numFrames (padded); ++m)
            {
                analyser.analyseFrame (signal.data(), padded, m, frame.data());
                subtractor.process (frame.data(), profiles[0], config.numBins());
                reconstructor.addFrame (frame.data(), m);
            }
            const long during = gAllocationCount.load() - before;
            logMessage ("allocations during " + juce::String (config.numFrames (padded)) + " frames: " + juce::String (during));
            expectEquals (during, 0L);
        }

        beginTest ("engine allocation count does not grow with the number of frames");
        {
            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (makeWhiteNoise (1, 20000, 0.1f, 22), profiles).wasOk());
            const auto shortInput = makeWhiteNoise (1, 10000, 0.1f, 23);
            const auto longInput = makeWhiteNoise (1, 400000, 0.1f, 24);
            juce::AudioBuffer<float> out;
            auto progress = [] (float) {};

            long before = gAllocationCount.load();
            expect (engine.process (shortInput, profiles, 1.0f, out, progress, noCancel).wasOk());
            const long shortCount = gAllocationCount.load() - before;

            before = gAllocationCount.load();
            expect (engine.process (longInput, profiles, 1.0f, out, progress, noCancel).wasOk());
            const long longCount = gAllocationCount.load() - before;

            logMessage ("allocations: short input " + juce::String (shortCount) + ", 40x longer input " + juce::String (longCount));
            expectEquals (longCount, shortCount);
        }

        beginTest ("NaN, Inf and huge samples never produce non-finite output");
        {
            const float huge = 3.0e38f, nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();

            auto noise = makeWhiteNoise (1, 20000, 0.1f, 25);
            noise.setSample (0, 5, nan);
            noise.setSample (0, 6, -inf);
            noise.setSample (0, 7, huge);
            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (noise, profiles).wasOk());
            for (const float v : profiles[0].getAverageMagnitude())
                expect (std::isfinite (v));

            auto input = makeWhiteNoise (2, 20000, 0.1f, 26);
            for (int i = 0; i < 20000; i += 97)
            {
                input.setSample (0, i, (i % 3 == 0) ? nan : (i % 3 == 1) ? inf : -huge);
                input.setSample (1, i, huge);
            }

            juce::AudioBuffer<float> out;
            for (const float alpha : { 0.0f, 1.8f, 5.0f })
            {
                expect (engine.process (input, profiles, alpha, out, {}, noCancel).wasOk());
                expect (allFinite (out));
            }

            const auto data = computeSpectrogram (input, config);
            for (const float db : data.decibels)
                expect (std::isfinite (db));
        }

        beginTest ("a profile built from silence and a silent input stay finite (epsilon guard)");
        {
            juce::AudioBuffer<float> silent (1, 20000);
            silent.clear();
            std::vector<NoiseProfile> profiles;
            expect (engine.estimateProfile (silent, profiles).wasOk());

            juce::AudioBuffer<float> out;
            expect (engine.process (makeWhiteNoise (1, 10000, 0.1f, 27), profiles, 5.0f, out, {}, noCancel).wasOk());
            expect (allFinite (out));
            expect (rms (out) > 0.05);   // nothing to subtract, so the signal passes through
        }
    }
};

static RobustnessTests robustnessTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (runner.getResult (i)->failures > 0)
            return 1;

    return 0;
}
