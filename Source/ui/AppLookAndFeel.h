#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Light theme, white cards, 12 px corners, one accent colour (plan §9).
class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour window { 0xfff5f5f7 }, card { 0xffffffff }, border { 0xffe5e5ea },
                                     textPrimary { 0xff1d1d1f }, textSecondary { 0xff6e6e73 },
                                     accent { 0xfffc8961 }, waveform { 0xff2d1b3d };

    AppLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};
