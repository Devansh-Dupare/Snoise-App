#include "MainComponent.h"

MainComponent::MainComponent()
{
    setSize (1440, 900);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xfff5f5f7));
    g.setColour (juce::Colour (0xff1d1d1f));
    g.setFont (24.0f);
    g.drawText ("Spectral Noise Cancellation", getLocalBounds(), juce::Justification::centred);
}

void MainComponent::resized() {}
