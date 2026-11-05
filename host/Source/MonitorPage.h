#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AudioEngine.h"
#include "StreamReceiver.h"

#include <deque>

// Tone hole bars: coverage per hole with the firmware's covered decision.
class HoleView : public juce::Component
{
public:
    void update (const warbl::SensorFrame& f, const std::optional<warbl::DeviceInfo>& i);
    void paint (juce::Graphics&) override;

private:
    warbl::SensorFrame frame;
    std::optional<warbl::DeviceInfo> info;
};

// Scrolling plot of a few traces over the last few seconds.
class ScopeView : public juce::Component
{
public:
    struct Trace
    {
        juce::String name;
        juce::Colour colour;
        std::vector<float> values;
    };

    ScopeView (juce::String title, float minValue, float maxValue);
    void setTraces (std::vector<Trace> t) { traces = std::move (t); repaint(); }
    void setMarker (std::optional<float> value) { marker = value; }
    void paint (juce::Graphics&) override;

private:
    juce::String title;
    float minValue, maxValue;
    std::vector<Trace> traces;
    std::optional<float> marker;
};

// Histogram of extra transport delay with percentiles.
class TimingView : public juce::Component
{
public:
    void update (const StreamReceiver::Stats& s) { stats = s; repaint(); }
    void paint (juce::Graphics&) override;

private:
    StreamReceiver::Stats stats;
};

// The "Monitor" tab: connection, stream statistics, sensor plots, latency click and reverb.
class MonitorPage : public juce::Component, private juce::Timer
{
public:
    MonitorPage (StreamReceiver&, AudioEngine&);

    // Lines describing the instrument and settings, for the top of a recording's CSV (set by the main window).
    std::function<juce::StringArray()> recordingHeader;
    void startRecording() { if (! audio.recorder.isRecording()) toggleRecording(); }
    void simulateBluetooth() { bluetoothSimButton.setToggleState (true, juce::sendNotificationSync); }
    ~MonitorPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void setSimulating (bool on) { simulateButton.setToggleState (on, juce::sendNotificationSync); }
    void playTestPhrase() { audio.playScale(); }
    void setOutputDevice (const juce::String& name)
    {
        auto setup = audio.deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = name;
        audio.deviceManager.setAudioDeviceSetup (setup, true);
    }
    double getCpuUsage() const { return audio.deviceManager.getCpuUsage(); }

private:
    void timerCallback() override;
    void tryConnect();
    void applyStreamSettings();
    void showAudioSettings();
    void showInfo (const warbl::DeviceInfo&);

    StreamReceiver& receiver;
    AudioEngine& audio;
    juce::MidiDeviceListConnection deviceListConnection;

    juce::Label statusLabel, infoLabel, statsLabel, audioLabel;
    juce::ToggleButton bluetoothSimButton { "as Bluetooth" }, smoothBluetoothButton { "Smooth Bluetooth" };
    juce::ToggleButton streamButton { "Stream" }, suppressButton { "Suppress notes" }, clickButton { "Click on breath" },
        simulateButton { "Simulate WARBL" };
    juce::TextButton infoButton { "Request info" }, audioButton { "Audio settings..." }, resetButton { "Reset stats" };
    juce::Slider thresholdSlider;
    juce::Label thresholdLabel { {}, "Click threshold" };

    juce::Label reverbLabel { {}, "Reverb" }, wetLabel { {}, "Level" }, preDelayLabel { {}, "Pre-delay" }, spreadLabel { {}, "Spread" },
        reverbStatus;
    juce::ComboBox roomBox;
    juce::Slider wetSlider, preDelaySlider, spreadSlider;
    void toggleRecording();
    juce::String recordingMessage;  // Shown instead of the streaming status for a few seconds after saving
    double recordingMessageUntilMs = 0.0;
    juce::TextButton loadIrButton { "Load IR file..." }, playButton { "Play scale" }, recordButton { "Record" };
    std::unique_ptr<juce::FileChooser> irChooser;

    HoleView holeView;
    ScopeView pressureScope { "Pressure (12-bit, above ambient)", -100.0f, 1500.0f };
    ScopeView accelScope { "Accelerometer (g)", -2.0f, 2.0f };
    ScopeView gyroScope { "Gyro (deg/s)", -500.0f, 500.0f };
    TimingView timingView;

    std::optional<warbl::DeviceInfo> deviceInfo;
    std::deque<ReceivedFrame> history;  // Last few seconds, for the plots.
    static constexpr size_t kHistoryFrames = 1500;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonitorPage)
};
