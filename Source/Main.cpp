#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class SncApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    void initialise (const juce::String&) override      { window = std::make_unique<MainWindow> (getApplicationName()); }
    void shutdown() override                            { window.reset(); }
    void systemRequestedQuit() override                 { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xfff5f5f7), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1000, 650, 4000, 3000);
            centreWithSize (1440, 900);
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SncApplication)
