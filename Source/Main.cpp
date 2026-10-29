#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"
#include "ui/AppLookAndFeel.h"

class SncApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        window = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (auto* mc = window != nullptr ? dynamic_cast<MainComponent*> (window->getContentComponent()) : nullptr)
            mc->confirmDiscardIfNeeded ([] { juce::JUCEApplication::quit(); });
        else
            quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xfff5f5f7), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);
           #if ! JUCE_MAC
            setMenuBar (&content->getMenuModel());
           #endif
            setResizable (true, true);
            setResizeLimits (1000, 650, 4000, 3000);
            centreWithSize (1440, 900);
            setVisible (true);
        }

        ~MainWindow() override
        {
           #if ! JUCE_MAC
            setMenuBar (nullptr);
           #endif
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    AppLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SncApplication)
