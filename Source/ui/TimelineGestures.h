#pragma once

#include "ViewState.h"

// Wheel / pinch handling shared by the waveform and spectrogram views.
// Cmd + wheel or pinch zooms around the pointer; plain wheel or horizontal swipe scrolls.
inline void handleTimelineWheel (ViewState& viewState, const juce::MouseEvent& e, const juce::MouseWheelDetails& w,
                                 juce::Rectangle<float> plot)
{
    if (viewState.getTotalLength() <= 0.0)
        return;

    if (e.mods.isCommandDown())
    {
        const auto factor = std::pow (2.0, static_cast<double> (w.deltaY) * 4.0);
        viewState.zoomAround (viewState.xToTime (e.position.x, plot), factor);
    }
    else
    {
        const auto delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        viewState.scrollBy (-static_cast<double> (delta) * viewState.getVisibleRange().getLength() * 0.5);
    }
}

inline void handleTimelineMagnify (ViewState& viewState, const juce::MouseEvent& e, float scaleFactor,
                                   juce::Rectangle<float> plot)
{
    if (viewState.getTotalLength() > 0.0 && scaleFactor > 0.0f)
        viewState.zoomAround (viewState.xToTime (e.position.x, plot), static_cast<double> (scaleFactor));
}
