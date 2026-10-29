#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Card with Process (filled) and Play (outlined) (plan §9).
class ActionsPanel : public juce::Component
{
public:
    ActionsPanel();

    juce::TextButton processButton { "Process" }, playButton { "Play" };

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActionsPanel)
};
