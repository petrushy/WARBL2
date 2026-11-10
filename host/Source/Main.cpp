#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"

class WarblStreamApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // Test runs (--snapshot) may run next to the user's own copy.
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains ("--snapshot"); }

    void initialise (const juce::String& commandLine) override
    {
        // A test run must not talk to WARBL (it would e.g. stop the user's stream when it quits).
        if (commandLine.contains ("--snapshot"))
            StreamReceiver::leaveDevicesAlone = true;

        mainWindow = std::make_unique<MainWindow> (getApplicationName());

       #if JUCE_MAC
        // The app menu's usual first item.
        juce::PopupMenu appleItems;
        appleItems.addItem ("About WARBL Stream...", [this]
        {
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                main->showAbout();
        });
        juce::MenuBarModel::setMacMainMenu (&menu, &appleItems);
       #endif

        // For testing: "--simulate" turns on the built-in simulated WARBL;
        // "--snapshot <file.png>" saves a picture of the window after a few seconds, then quits.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        // "--output <device>" picks the audio output; "--play" plays the test phrase at start.
        // "--monitor", "--instrument", "--sensors", "--model" open that tab; "--about" the About window (the
        // snapshot is then of that window).
        if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
        {
            // A snapshot run may run next to the player's own copy of the app: compute the sound, but play none.
            if (args.contains ("--snapshot"))
                main->getAudio().silent = true;
            auto& monitor = main->getMonitor();
            const auto output = args.indexOf ("--output");
            if (output >= 0 && output + 1 < args.size())
                monitor.setOutputDevice (args[output + 1].unquoted());
            if (args.contains ("--simulate"))
                monitor.setSimulating (true);
            if (args.contains ("--bluetooth"))  // The simulator sends Bluetooth batches
                main->getMonitor().simulateBluetooth();
            if (args.contains ("--play"))
                monitor.playTestPhrase();
            if (args.contains ("--record"))  // Starts recording at once (saved when the app quits)
                monitor.startRecording();
            if (args.contains ("--monitor"))
                main->showMonitor();
            if (args.contains ("--instrument"))
                main->showInstrument();
            if (args.contains ("--sensors"))
                main->showSensors();
            if (args.contains ("--model"))
                main->showModel();
            if (args.contains ("--about"))
                main->showAbout();
            const auto preset = args.indexOf ("--preset");
            if (preset >= 0 && preset + 1 < args.size())
                main->loadPreset (args[preset + 1].getIntValue());
        }

        const auto index = args.indexOf ("--snapshot");
        if (index >= 0 && index + 1 < args.size())
        {
            if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                main->setSaveStateOnExit (false);  // Don't overwrite the user's settings from a test run.
            const juce::File file (args[index + 1].unquoted());
            juce::Timer::callAfterDelay (6000, [this, file]
            {
                if (auto* main = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                    std::printf ("audio CPU %.1f %%\n", main->getMonitor().getCpuUsage() * 100.0);
                auto* shown = mainWindow->getContentComponent();
                if (auto* main = dynamic_cast<MainComponent*> (shown); main != nullptr && main->getAboutPanel() != nullptr)
                    shown = main->getAboutPanel();
                const auto image = shown->createComponentSnapshot (shown->getLocalBounds(), true, 1.0f);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
                quit();
            });
        }
    }
    void shutdown() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #endif
        mainWindow = nullptr;
    }
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
   #if JUCE_MAC
    // The app has no menus of its own: this is only there to carry the About item in the app menu.
    struct NoMenus : public juce::MenuBarModel
    {
        juce::StringArray getMenuBarNames() override { return {}; }
        juce::PopupMenu getMenuForIndex (int, const juce::String&) override { return {}; }
        void menuItemSelected (int, int) override {}
    } menu;
   #endif
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (WarblStreamApplication)
