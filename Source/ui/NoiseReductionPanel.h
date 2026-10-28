#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Card with the alpha knob (plan §9).
class NoiseReductionPanel : public juce::Component
{
public:
    NoiseReductionPanel();

    juce::Slider alphaSlider;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoiseReductionPanel)
};
