#include "NoiseReductionPanel.h"
#include "AppLookAndFeel.h"

NoiseReductionPanel::NoiseReductionPanel()
{
    alphaSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    alphaSlider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    alphaSlider.setRange (0.0, 5.0, 0.05);
    alphaSlider.setValue (1.8, juce::dontSendNotification);
    alphaSlider.setDoubleClickReturnValue (true, 1.8);
    alphaSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 24);
    alphaSlider.setTooltip ("alpha: how much of the noise estimate is subtracted (double-click to reset)");
    addAndMakeVisible (alphaSlider);
}

void NoiseReductionPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (AppLookAndFeel::card);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (AppLookAndFeel::border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    auto area = getLocalBounds().reduced (12);
    g.setColour (AppLookAndFeel::textPrimary);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Noise reduction", area.removeFromTop (22), juce::Justification::centredLeft);

    g.setColour (AppLookAndFeel::textSecondary);
    g.setFont (12.0f);
    g.drawFittedText ("Higher alpha removes more noise but can leave musical-noise artifacts.",
                      area.removeFromBottom (34), juce::Justification::centred, 2);
}

void NoiseReductionPanel::resized()
{
    alphaSlider.setBounds (getLocalBounds().reduced (12).withTrimmedTop (26).withTrimmedBottom (36));
}
