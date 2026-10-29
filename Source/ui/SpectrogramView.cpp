#include "SpectrogramView.h"
#include "MagmaColourMap.h"
#include "TimelineGestures.h"
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

SpectrogramView::SpectrogramView (ViewState& vs, juce::String t, juce::String p)
    : viewState (vs), title (std::move (t)), placeholder (std::move (p))
{
    viewState.addChangeListener (this);
}

SpectrogramView::~SpectrogramView()
{
    viewState.removeChangeListener (this);
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

    const StftConfig config;
    const int length = audio.getNumSamples();
    const int totalFrames = length < config.fftSize() ? 1 : 1 + (length - config.fftSize()) / config.hopSize;
    frameStride = juce::jmax (1, (totalFrames + 4095) / 4096);   // must match computeSpectrogram's default
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

    const auto visible = viewState.getVisibleRange();
    const StftConfig config;
    const double columnsPerSecond = sampleRate / (static_cast<double> (config.hopSize) * frameStride);
    // Column m is the STFT frame whose centre is at (m*stride*H + N/2) / fs; the +0.5 centres the column's pixel.
    const double centreOffset = (config.fftSize() * 0.5) / (static_cast<double> (config.hopSize) * frameStride) - 0.5;
    const double srcX = visible.getStart() * columnsPerSecond - centreOffset;
    const double srcW = visible.getLength() * columnsPerSecond;

    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (plot);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // show true STFT frames when zoomed in

        const auto transform = juce::AffineTransform::translation (static_cast<float> (-srcX), 0.0f)
                                   .scaled (static_cast<float> (plot.getWidth() / srcW),
                                            static_cast<float> (plot.getHeight()) / static_cast<float> (image.getHeight()))
                                   .translated (static_cast<float> (plot.getX()), static_cast<float> (plot.getY()));
        g.drawImageTransformed (image, transform);
    }

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
    for (const double candidate : { 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0 })
    {
        timeStep = candidate;
        if (candidate / visible.getLength() * plot.getWidth() >= 70.0)
            break;
    }

    const int decimals = timeStep < 0.1 ? 2 : (timeStep < 1.0 ? 1 : 0);
    const auto plotF = plot.toFloat();
    for (double t = std::ceil (visible.getStart() / timeStep) * timeStep; t <= visible.getEnd(); t += timeStep)
    {
        const auto x = viewState.timeToX (t, plotF);
        g.drawText (juce::String (t, decimals) + "s", juce::roundToInt (x) - 20, plot.getBottom() + 3, 40, 14, juce::Justification::centred);
    }

    if (visible.contains (playheadSeconds))
    {
        const auto px = viewState.timeToX (playheadSeconds, plotF);
        g.setColour (juce::Colours::white);
        g.drawLine (px, static_cast<float> (plot.getY()), px, static_cast<float> (plot.getBottom()), 2.0f);
    }
}

void SpectrogramView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    handleTimelineWheel (viewState, e, w, getPlotBounds().toFloat());
}

void SpectrogramView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    handleTimelineMagnify (viewState, e, scaleFactor, getPlotBounds().toFloat());
}
