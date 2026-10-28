#include "WaveformView.h"

namespace
{
    const juce::Colour cardColour (0xffffffff), borderColour (0xffe5e5ea), textSecondary (0xff6e6e73),
                       waveformColour (0xff2d1b3d), accentColour (0xfffc8961);
}

WaveformView::WaveformView()
    : thumbnail (512, formats, thumbnailCache)
{
    formats.registerBasicFormats();
}

void WaveformView::setAudio (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    thumbnail.reset (audio.getNumChannels(), sampleRate, audio.getNumSamples());
    thumbnail.addBlock (0, audio, 0, audio.getNumSamples());
    playheadSeconds = 0.0;
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

double WaveformView::secondsAtX (int x) const
{
    const auto plot = getPlotBounds();
    const double fraction = juce::jlimit (0.0, 1.0, (x - plot.getX()) / static_cast<double> (juce::jmax (1, plot.getWidth())));
    return fraction * thumbnail.getTotalLength();
}

float WaveformView::xAtSeconds (double seconds) const
{
    const auto plot = getPlotBounds();
    const double total = thumbnail.getTotalLength();
    const auto fraction = static_cast<float> (total > 0.0 ? seconds / total : 0.0);
    return static_cast<float> (plot.getX()) + fraction * static_cast<float> (plot.getWidth());
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

    g.setColour (waveformColour);
    thumbnail.drawChannels (g, plot, 0.0, thumbnail.getTotalLength(), 1.0f);

    const float x = xAtSeconds (playheadSeconds);
    g.setColour (accentColour);
    g.drawLine (x, static_cast<float> (plot.getY()), x, static_cast<float> (plot.getBottom()), 2.0f);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (onSeek && thumbnail.getTotalLength() > 0.0)
        onSeek (secondsAtX (e.x));
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    mouseDown (e);
}
