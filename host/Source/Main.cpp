#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"

class WarblStreamApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // For testing: "--simulate" turns on the built-in simulated WARBL;
        // "--snapshot <file.png>" saves a picture of the window after a few seconds, then quits.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        // "--output <device>" picks the audio output; "--play" plays the test phrase at start.
        // "--instrument" opens the Instrument tab.
        if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
        {
            auto& monitor = main->getMonitor();
            const auto output = args.indexOf ("--output");
            if (output >= 0 && output + 1 < args.size())
                monitor.setOutputDevice (args[output + 1].unquoted());
            if (args.contains ("--simulate"))
                monitor.setSimulating (true);
            if (args.contains ("--play"))
                monitor.playTestPhrase();
            if (args.contains ("--instrument"))
                main->showInstrument();
            if (args.contains ("--sensors"))
                main->showSensors();
            const auto preset = args.indexOf ("--preset");
            if (preset >= 0 && preset + 1 < args.size())
                main->loadPreset (args[preset + 1].getIntValue());
        }

        const auto index = args.indexOf ("--snapshot");
        if (index >= 0 && index + 1 < args.size())
        {
            const juce::File file (args[index + 1].unquoted());
            juce::Timer::callAfterDelay (6000, [this, file]
            {
                if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    std::printf ("audio CPU %.1f %%\n", main->getMonitor().getCpuUsage() * 100.0);
                const auto image = mainWindow->getContentComponent()->createComponentSnapshot (mainWindow->getContentComponent()->getLocalBounds(), true, 1.0f);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
                quit();
            });
        }
    }
    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff1e1f22), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (WarblStreamApplication)
