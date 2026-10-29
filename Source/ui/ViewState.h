#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The visible time range, shared by the waveform and both spectrograms so they always zoom together.
class ViewState : public juce::ChangeBroadcaster
{
public:
    void setTotalLength (double seconds)
    {
        totalLength = juce::jmax (0.0, seconds);
        zoomToFit();
    }

    double getTotalLength() const { return totalLength; }
    juce::Range<double> getVisibleRange() const { return visible; }

    void setVisibleRange (juce::Range<double> r)
    {
        if (totalLength <= 0.0)
            return;

        const auto len = juce::jlimit (minVisibleLength(), totalLength, r.getLength());
        const auto start = juce::jlimit (0.0, totalLength - len, r.getStart());
        const juce::Range<double> clamped (start, start + len);

        if (clamped != visible)
        {
            visible = clamped;
            sendSynchronousChangeMessage();
        }
    }

    // factor > 1 zooms in; the time under anchorTime stays at the same screen position.
    void zoomAround (double anchorTime, double factor)
    {
        if (totalLength <= 0.0)
            return;

        const auto newLen = visible.getLength() / factor;
        const auto proportion = (anchorTime - visible.getStart()) / visible.getLength();
        const auto newStart = anchorTime - proportion * newLen;
        setVisibleRange ({ newStart, newStart + newLen });
    }

    void scrollBy (double deltaSeconds) { setVisibleRange (visible.movedToStartAt (visible.getStart() + deltaSeconds)); }

    void zoomToFit()
    {
        visible = { 0.0, totalLength };
        sendSynchronousChangeMessage();
    }

    bool canZoomIn() const { return visible.getLength() > minVisibleLength() + 1e-9; }
    bool canZoomOut() const { return visible.getLength() < totalLength - 1e-9; }

    float timeToX (double t, juce::Rectangle<float> plot) const
    {
        return plot.getX() + static_cast<float> ((t - visible.getStart()) / visible.getLength()) * plot.getWidth();
    }

    double xToTime (float x, juce::Rectangle<float> plot) const
    {
        return visible.getStart() + static_cast<double> ((x - plot.getX()) / plot.getWidth()) * visible.getLength();
    }

private:
    double minVisibleLength() const { return juce::jmin (0.05, totalLength); }   // 50 ms

    double totalLength = 0.0;
    juce::Range<double> visible;
};
