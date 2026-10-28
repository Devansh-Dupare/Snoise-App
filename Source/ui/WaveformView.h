#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

// Waveform card with a playhead; click to seek.
class WaveformView : public juce::Component
{
public:
    WaveformView();

    void setAudio (const juce::AudioBuffer<float>& audio, double sampleRate);
    void setPlayheadSeconds (double seconds);

    std::function<void (double)> onSeek;   // seconds

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    double secondsAtX (int x) const;
    float xAtSeconds (double seconds) const;
    juce::Rectangle<int> getPlotBounds() const;

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail thumbnail;
    double playheadSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
