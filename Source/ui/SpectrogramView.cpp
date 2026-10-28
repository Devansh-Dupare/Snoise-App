#include "SpectrogramView.h"
#include "MagmaColourMap.h"
#include "dsp/Spectrogram.h"

namespace
{
    const juce::Colour cardColour (0xffffffff), borderColour (0xffe5e5ea), textPrimary (0xff1d1d1f),
                       textSecondary (0xff6e6e73);

    juce::Image renderImage (const SpectrogramData& data)
    {
        // Row 0 is the highest frequency, so low frequencies sit at the bottom.
        juce::Image img (juce::Image::ARGB, data.numFrames, data.numBins, false, juce::SoftwareImageType());
        juce::Image::BitmapData pixels (img, juce::Image::BitmapData::writeOnly);

        for (int m = 0; m < data.numFrames; ++m)
            for (int k = 0; k < data.numBins; ++k)
                pixels.setPixelColour (m, data.numBins - 1 - k, juce::Colour (MagmaColourMap::fromDecibels (data.at (m, k))));

        return img;
    }
}

SpectrogramView::SpectrogramView (juce::String t, juce::String p)
    : title (std::move (t)), placeholder (std::move (p))
{
}

SpectrogramView::~SpectrogramView()
{
    ++*generation;   // lets a running job bail out early
    pool.removeAllJobs (true, 10000);
}

void SpectrogramView::clear()
{
    ++*generation;
    image = {};
    durationSeconds = 0.0;
    computing = false;
    repaint();
}

void SpectrogramView::setAudio (const juce::AudioBuffer<float>& audio, double newSampleRate)
{
    const int myGeneration = ++*generation;
    image = {};
    sampleRate = newSampleRate;
    durationSeconds = audio.getNumSamples() / newSampleRate;
    computing = true;
    repaint();

    pool.addJob ([safeThis = juce::Component::SafePointer<SpectrogramView> (this), gen = generation,
                  myGeneration, copy = audio]
    {
        if (gen->load() != myGeneration)
            return;   // superseded while queued

        const auto data = computeSpectrogram (copy, StftConfig());

        if (gen->load() != myGeneration || data.numFrames == 0)
            return;

        auto img = renderImage (data);

        juce::MessageManager::callAsync ([safeThis, gen, myGeneration, img]
        {
            if (safeThis != nullptr && gen->load() == myGeneration)
            {
                safeThis->image = img;
                safeThis->computing = false;
                safeThis->repaint();
            }
        });
    });
}

void SpectrogramView::setPlayheadSeconds (double seconds)
{
    if (! juce::approximatelyEqual (seconds, playheadSeconds))
    {
        playheadSeconds = seconds;

        if (image.isValid())
            repaint();
    }
}

juce::Rectangle<int> SpectrogramView::getPlotBounds() const
{
    return getLocalBounds().reduced (12).withTrimmedTop (24).withTrimmedLeft (36).withTrimmedBottom (18);
}

void SpectrogramView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (cardColour);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (textPrimary);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (12).removeFromTop (22), juce::Justification::centredLeft);

    const auto plot = getPlotBounds();

    if (! image.isValid())
    {
        g.setColour (juce::Colour (0xfff5f5f7));
        g.fillRoundedRectangle (plot.toFloat(), 6.0f);
        g.setColour (textSecondary);
        g.setFont (14.0f);
        g.drawText (computing ? "Computing spectrogram..." : placeholder, plot, juce::Justification::centred);
        return;
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, plot.toFloat(), juce::RectanglePlacement::stretchToFit);

    // Frequency axis (kHz) and time axis (s).
    g.setColour (textSecondary);
    g.setFont (11.0f);
    const double nyquistKHz = sampleRate * 0.5 / 1000.0;
    const double freqStep = nyquistKHz > 12.0 ? 5.0 : 2.0;
    for (double f = 0.0; f <= nyquistKHz; f += freqStep)
    {
        const auto y = static_cast<float> (plot.getBottom()) - static_cast<float> (f / nyquistKHz) * static_cast<float> (plot.getHeight());
        g.drawText (juce::String (f, 0) + "k", plot.getX() - 34, juce::roundToInt (y) - 7, 30, 14, juce::Justification::centredRight);
    }

    double timeStep = 1.0;
    for (const double candidate : { 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0 })
    {
        timeStep = candidate;
        if (candidate / durationSeconds * plot.getWidth() >= 70.0)
            break;
    }
    for (double t = 0.0; t <= durationSeconds; t += timeStep)
    {
        const auto x = static_cast<float> (plot.getX()) + static_cast<float> (t / durationSeconds) * static_cast<float> (plot.getWidth());
        g.drawText (juce::String (t, 0) + "s", juce::roundToInt (x) - 20, plot.getBottom() + 3, 40, 14, juce::Justification::centred);
    }

    const auto px = static_cast<float> (plot.getX()) + static_cast<float> (playheadSeconds / durationSeconds) * static_cast<float> (plot.getWidth());
    g.setColour (juce::Colours::white);
    g.drawLine (px, static_cast<float> (plot.getY()), px, static_cast<float> (plot.getBottom()), 2.0f);
}
