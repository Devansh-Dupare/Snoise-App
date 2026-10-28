#include "AppLookAndFeel.h"

AppLookAndFeel::AppLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, window);
    setColour (juce::Label::textColourId, textPrimary);
    setColour (juce::TextButton::textColourOffId, textPrimary);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, card);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, textPrimary);
    setColour (juce::Slider::textBoxOutlineColourId, border);
    setColour (juce::Slider::textBoxBackgroundColourId, card);
    setColour (juce::TooltipWindow::backgroundColourId, textPrimary);
    setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
}

juce::Font AppLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (15.0f, static_cast<float> (buttonHeight) * 0.5f), juce::Font::bold));
}

void AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                           bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = backgroundColour;

    if (! button.isEnabled())
        fill = fill.withAlpha (0.5f);
    else if (isButtonDown)
        fill = fill.darker (0.15f);
    else if (isMouseOverButton)
        fill = fill.darker (0.05f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (border);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);
}

void AppLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackWidth = 8.0f;
    const float arcRadius = radius - trackWidth * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (border);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float knobRadius = arcRadius - trackWidth;
    g.setColour (card);
    g.fillEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);
    g.setColour (border);
    g.drawEllipse (centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobRadius + 5.0f, 3.0f, knobRadius * 0.4f, 1.5f);
    g.setColour (slider.isEnabled() ? textPrimary : textSecondary);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void AppLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                       float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    g.setColour (border);
    g.fillRoundedRectangle (static_cast<float> (x), cy - 2.0f, static_cast<float> (width), 4.0f, 2.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (static_cast<float> (x), cy - 2.0f, sliderPos - static_cast<float> (x), 4.0f, 2.0f);
    g.setColour (slider.isEnabled() ? card : border);
    g.fillEllipse (sliderPos - 8.0f, cy - 8.0f, 16.0f, 16.0f);
    g.setColour (border.darker (0.2f));
    g.drawEllipse (sliderPos - 8.0f, cy - 8.0f, 16.0f, 16.0f, 1.0f);
}
