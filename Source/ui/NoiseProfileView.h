#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

// Plots the average noise magnitude D̄v(k) in dB against frequency (dB is for drawing only, plan §6.6).
class NoiseProfileView : public juce::Component
{
public:
    NoiseProfileView();

    void setProfile (const std::vector<float>& averageMagnitude, double sampleRate, const juce::String& description);
    void clearProfile();
    void setLoadEnabled (bool shouldBeEnabled) { loadButton.setEnabled (shouldBeEnabled); }

    std::function<void()> onLoadNoiseFile;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> getPlotBounds() const;

    juce::TextButton loadButton { "Load noise file" };
    std::vector<float> decibels;
    double sampleRate = 0.0;
    juce::String description;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseProfileView)
};
