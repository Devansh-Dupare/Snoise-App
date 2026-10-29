#include "ActionsPanel.h"
#include "AppLookAndFeel.h"

ActionsPanel::ActionsPanel()
{
    addAndMakeVisible (processButton);
    addAndMakeVisible (playButton);
}

void ActionsPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (AppLookAndFeel::card);
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (AppLookAndFeel::border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 12.0f, 1.0f);

    g.setColour (AppLookAndFeel::textPrimary);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("Actions", getLocalBounds().reduced (12).removeFromTop (22), juce::Justification::centredLeft);
}

void ActionsPanel::resized()
{
    auto area = getLocalBounds().reduced (12).withTrimmedTop (34);
    const int rowHeight = juce::jmin (44, (area.getHeight() - 10) / 2);

    processButton.setBounds (area.removeFromTop (rowHeight));
    area.removeFromTop (10);
    playButton.setBounds (area.removeFromTop (rowHeight));
}
