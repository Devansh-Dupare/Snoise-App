#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

// Waveform card with a playhead and a draggable noise region.
// Click = seek; drag = mark the noise-only section.
class WaveformView : public juce::Component
{
public:
    WaveformView();

    void setAudio (const juce::AudioBuffer<float>& audio, double sampleRate);
    void setPlayheadSeconds (double seconds);

    void setRegion (double startSeconds, double endSeconds);
    void clearRegion();

    std::function<void (double)> onSeek;                    // seconds
    std::function<void (double, double)> onRegionSelected;  // start, end seconds (start < end)

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    double secondsAtX (int x) const;
    float xAtSeconds (double seconds) const;
    juce::Rectangle<int> getPlotBounds() const;

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail thumbnail;
    double playheadSeconds = 0.0;

    bool hasRegion = false, dragging = false;
    double regionStart = 0.0, regionEnd = 0.0, dragAnchorSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
