#include "NoiseProfileView.h"

#include <cmath>

namespace
{
    const juce::Colour cardColour (0xffffffff), borderColour (0xffe5e5ea), textPrimary (0xff1d1d1f),
                       textSecondary (0xff6e6e73), accentColour (0xfffc8961);
}

NoiseProfileView::NoiseProfileView()
{
    loadButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { if (onLoadNoiseFile) onLoadNoiseFile(); };
    loadButton.setTooltip ("Load a separate noise-only recording (must match the input's sample rate)");
}

void NoiseProfileView::setProfile (const std::vector<float>& averageMagnitude, double newSampleRate,
                                   const juce::String& newDescription)
{
    decibels.resize (averageMagnitude.size());

    for (size_t k = 0; k < averageMagnitude.size(); ++k)
        decibels[k] = 20.0f * std::log10 (averageMagnitude[k] + 1.0e-9f);

    sampleRate = newSampleRate;
    description = newDescription;
    repaint();
}

void NoiseProfileView::clearProfile()
{
    decibels.clear();
    description = {};
    repaint();
}

void NoiseProfileView::resized()
{
    loadButton.setBounds (getLocalBounds().reduced (12).removeFromTop (28).removeFromRight (130));
}

juce::Rectangle<int> NoiseProfileView::getPlotBounds() const
{
    return getLocalBounds().reduced (12).withTrimmedTop (44).withTrimmedLeft (44).withTrimmedBottom (22);
}

void NoiseProfileView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (cardColour);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (textPrimary);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Noise profile", getLocalBounds().reduced (12).removeFromTop (28).removeFromLeft (200),
                juce::Justification::centredLeft);

    const auto plot = getPlotBounds();

    if (decibels.empty())
    {
        g.setColour (textSecondary);
        g.setFont (14.0f);
        g.drawText ("Drag on the waveform to mark noise, or load a noise file", plot, juce::Justification::centred);
        return;
    }

    float maxDb = decibels[0];
    for (const float v : decibels)
        maxDb = juce::jmax (maxDb, v);

    const float top = std::ceil ((maxDb + 3.0f) / 10.0f) * 10.0f, bottom = top - 80.0f;
    auto yAt = [&] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - bottom) / (top - bottom));
        return static_cast<float> (plot.getBottom()) - t * static_cast<float> (plot.getHeight());
    };

    g.setFont (11.0f);
    for (float db = top; db >= bottom - 0.1f; db -= 20.0f)
    {
        const float y = yAt (db);
        g.setColour (borderColour);
        g.drawHorizontalLine (juce::roundToInt (y), static_cast<float> (plot.getX()), static_cast<float> (plot.getRight()));
        g.setColour (textSecondary);
        g.drawText (juce::String (juce::roundToInt (db)) + " dB", plot.getX() - 44, juce::roundToInt (y) - 8, 40, 16,
                    juce::Justification::centredRight);
    }

    const double nyquist = sampleRate * 0.5;
    const double tickStep = nyquist > 12000.0 ? 5000.0 : 2000.0;
    for (double f = 0.0; f <= nyquist; f += tickStep)
    {
        const auto x = static_cast<float> (plot.getX()) + static_cast<float> (f / nyquist) * static_cast<float> (plot.getWidth());
        g.setColour (textSecondary);
        g.drawText (juce::String (f / 1000.0, 0) + " kHz", juce::roundToInt (x) - 25, plot.getBottom() + 4, 50, 16,
                    juce::Justification::centred);
    }

    juce::Path path;
    const auto lastBin = static_cast<float> (decibels.size() - 1);
    for (size_t k = 0; k < decibels.size(); ++k)
    {
        const float x = static_cast<float> (plot.getX()) + static_cast<float> (k) / lastBin * static_cast<float> (plot.getWidth());
        if (k == 0)
            path.startNewSubPath (x, yAt (decibels[k]));
        else
            path.lineTo (x, yAt (decibels[k]));
    }

    g.setColour (accentColour);
    g.strokePath (path, juce::PathStrokeType (1.5f));

    g.setColour (textSecondary);
    g.setFont (12.0f);
    g.drawText (description, getLocalBounds().reduced (12).removeFromTop (28).withTrimmedRight (140),
                juce::Justification::centredRight);
}
