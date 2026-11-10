// The app window: owns the stream receiver and audio engine, with a tab per page.

#pragma once

#include "InstrumentPage.h"
#include "ModelPage.h"
#include "MonitorPage.h"
#include "SensorsPage.h"

class MainComponent : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    MonitorPage& getMonitor() { return *monitor; }
    void showMonitor() { tabs.setCurrentTabIndex (0); }
    void showInstrument() { tabs.setCurrentTabIndex (1); }
    void showSensors() { tabs.setCurrentTabIndex (2); }
    void showModel() { tabs.setCurrentTabIndex (3); }
    void loadPreset (int index) { instrument->loadPreset (index); }
    void showAbout();
    juce::Component* getAboutPanel() { return aboutWindow != nullptr ? aboutWindow->getContentComponent() : nullptr; }
    void setSaveStateOnExit (bool shouldSave) { saveOnExit = shouldSave; }
    AudioEngine& getAudio() { return audio; }

private:
    // Restores and saves the instrument, sensor response, reverb and audio device between runs.
    void restoreState();
    void saveState();

    juce::ApplicationProperties properties;
    StreamReceiver receiver;
    AudioEngine audio { receiver };
    InstrumentPage* instrument = nullptr;  // Owned by tabs.
    bool saveOnExit = true;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    MonitorPage* monitor = nullptr;  // Owned by tabs.
    juce::TextButton aboutButton { "About" };
    juce::Component::SafePointer<juce::DialogWindow> aboutWindow;  // Deletes itself when closed.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
