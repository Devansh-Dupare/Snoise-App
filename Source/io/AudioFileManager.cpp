#include "AudioFileManager.h"

#include <cmath>

juce::Result AudioFileManager::load (const juce::File& file, LoadedAudio& out)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("File not found: " + file.getFullPathName());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return juce::Result::fail ("Unsupported or corrupt audio file: " + file.getFileName());

    if (reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0)
        return juce::Result::fail ("The audio file is empty: " + file.getFileName());

    if (reader->lengthInSamples > std::numeric_limits<int>::max())
        return juce::Result::fail ("The audio file is too long to process.");

    const auto numSamples = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), numSamples);

    if (! reader->read (&buffer, 0, numSamples, 0, true, true))
        return juce::Result::fail ("Could not read audio data from " + file.getFileName());

    int replaced = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            if (! std::isfinite (data[i]))
            {
                data[i] = 0.0f;
                ++replaced;
            }
        }
    }

    out.buffer = std::move (buffer);
    out.sampleRate = reader->sampleRate;
    out.numNonFiniteReplaced = replaced;
    return juce::Result::ok();
}

juce::Result AudioFileManager::saveWav (const juce::File& file, const juce::AudioBuffer<float>& buffer,
                                        double sampleRate, int bitsPerSample)
{
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return juce::Result::fail ("There is no audio to save.");

    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
        return juce::Result::fail ("Unsupported bit depth.");

    auto stream = file.createOutputStream();

    if (stream == nullptr)
        return juce::Result::fail ("Cannot write to " + file.getFullPathName() + " (check permissions and disk space).");

    stream->setPosition (0);
    stream->truncate();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate,
                                                                          static_cast<unsigned int> (buffer.getNumChannels()),
                                                                          bitsPerSample, {}, 0));

    if (writer == nullptr)
        return juce::Result::fail ("Could not create a WAV writer for " + file.getFileName());

    stream.release();   // the writer now owns the stream

    if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        return juce::Result::fail ("Writing to " + file.getFileName() + " failed.");

    return juce::Result::ok();
}

float AudioFileManager::getPeak (const juce::AudioBuffer<float>& buffer)
{
    return buffer.getMagnitude (0, buffer.getNumSamples());
}
