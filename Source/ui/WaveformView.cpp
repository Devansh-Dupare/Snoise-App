#include "WaveformView.h"
#include "TimelineGestures.h"

namespace
{
    const juce::Colour cardColour (0xffffffff), borderColour (0xffe5e5ea), textSecondary (0xff6e6e73),
                       waveformColour (0xff2d1b3d), accentColour (0xfffc8961);
}

WaveformView::WaveformView (ViewState& vs)
    : viewState (vs), thumbnail (512, formats, thumbnailCache)
{
    formats.registerBasicFormats();
    viewState.addChangeListener (this);
}

WaveformView::~WaveformView()
{
    viewState.removeChangeListener (this);
}

void WaveformView::setAudio (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    thumbnail.reset (audio.getNumChannels(), sampleRate, audio.getNumSamples());
    thumbnail.addBlock (0, audio, 0, audio.getNumSamples());
    playheadSeconds = 0.0;
    clearRegion();
    repaint();
}

void WaveformView::setRegion (double startSeconds, double endSeconds)
{
    hasRegion = true;
    regionStart = juce::jmin (startSeconds, endSeconds);
    regionEnd = juce::jmax (startSeconds, endSeconds);
    repaint();
}

void WaveformView::clearRegion()
{
    hasRegion = dragging = false;
    repaint();
}

void WaveformView::setPlayheadSeconds (double seconds)
{
    if (! juce::approximatelyEqual (seconds, playheadSeconds))
    {
        playheadSeconds = seconds;
        repaint();
    }
}

juce::Rectangle<int> WaveformView::getPlotBounds() const
{
    return getLocalBounds().reduced (12);
}

double WaveformView::secondsAtX (float x) const
{
    const auto plot = getPlotBounds().toFloat();
    return juce::jlimit (0.0, thumbnail.getTotalLength(), viewState.xToTime (x, plot));
}

float WaveformView::xAtSeconds (double seconds) const
{
    return viewState.timeToX (seconds, getPlotBounds().toFloat());
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (cardColour);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    const auto plot = getPlotBounds();

    if (thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (textSecondary);
        g.setFont (15.0f);
        g.drawText ("Load an audio file to see its waveform", plot, juce::Justification::centred);
        return;
    }

    const auto visible = viewState.getVisibleRange();
    g.saveState();
    g.reduceClipRegion (plot);
    g.setColour (waveformColour);
    thumbnail.drawChannels (g, plot, visible.getStart(), visible.getEnd(), 1.0f);

    if (hasRegion)
    {
        const float x1 = xAtSeconds (regionStart), x2 = xAtSeconds (regionEnd);
        g.setColour (accentColour.withAlpha (0.25f));
        g.fillRect (x1, static_cast<float> (plot.getY()), x2 - x1, static_cast<float> (plot.getHeight()));
        g.setColour (accentColour);
        g.drawRect (x1, static_cast<float> (plot.getY()), x2 - x1, static_cast<float> (plot.getHeight()), 1.0f);
        g.setFont (12.0f);
        g.drawText ("Noise", juce::Rectangle<float> (x1 + 4.0f, static_cast<float> (plot.getY()) + 2.0f, 60.0f, 16.0f),
                    juce::Justification::centredLeft);
    }

    if (visible.contains (playheadSeconds))
    {
        const float x = xAtSeconds (playheadSeconds);
        g.setColour (accentColour);
        g.drawLine (x, static_cast<float> (plot.getY()), x, static_cast<float> (plot.getBottom()), 2.0f);
    }

    g.restoreState();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    handleTimelineWheel (viewState, e, w, getPlotBounds().toFloat());
}

void WaveformView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    handleTimelineMagnify (viewState, e, scaleFactor, getPlotBounds().toFloat());
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    dragAnchorSeconds = secondsAtX (e.position.x);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (thumbnail.getTotalLength() <= 0.0)
        return;

    if (! dragging && e.getDistanceFromDragStartX() < 4 && e.getDistanceFromDragStartX() > -4)
        return;

    dragging = true;
    setRegion (dragAnchorSeconds, secondsAtX (e.position.x));
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (thumbnail.getTotalLength() <= 0.0)
        return;

    if (dragging)
    {
        dragging = false;

        if (onRegionSelected)
            onRegionSelected (regionStart, regionEnd);
    }
    else if (onSeek)
    {
        onSeek (secondsAtX (e.position.x));
    }
}
