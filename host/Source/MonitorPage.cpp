#include "MonitorPage.h"

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour grid { 0xff3a3d42 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour coveredColour { 0xffe0a24f };
} // namespace

//==============================================================================
void HoleView::update (const warbl::SensorFrame& f, const std::optional<warbl::DeviceInfo>& i)
{
    frame = f;
    info = i;
    repaint();
}

void HoleView::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto area = getLocalBounds().reduced (8);
    g.setColour (text);
    g.setFont (13.0f);
    g.drawText (info ? "Tone holes (coverage from calibration)" : "Tone holes (raw; request info for calibration)",
                area.removeFromTop (18), juce::Justification::centredLeft);

    // Draw thumb first and bell last, as on the instrument from top to bottom.
    const int order[] = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    const float columnWidth = (float) area.getWidth() / warbl::kNumHoles;
    auto labels = area.removeFromBottom (34);
    auto bars = area;

    for (int column = 0; column < warbl::kNumHoles; ++column)
    {
        const int hole = order[column];
        const auto raw = frame.holes[(size_t) hole];
        const float level = info ? info->holeCoverage (hole, raw) : raw / 1023.0f;
        const bool covered = (frame.holeCovered >> hole) & 1;

        auto bar = juce::Rectangle<float> ((float) bars.getX() + column * columnWidth + 6.0f, (float) bars.getY(),
                                           columnWidth - 12.0f, (float) bars.getHeight());
        g.setColour (grid);
        g.fillRect (bar);

        const float shown = juce::jlimit (0.0f, 1.2f, level) / 1.2f;  // Leave room above "covered".
        g.setColour (covered ? coveredColour : accent);
        g.fillRect (bar.withTop (bar.getBottom() - shown * bar.getHeight()));

        if (info)
        {
            const float coveredY = bar.getBottom() - bar.getHeight() / 1.2f;
            g.setColour (text.withAlpha (0.6f));
            g.drawHorizontalLine ((int) coveredY, bar.getX() - 3.0f, bar.getRight() + 3.0f);
        }

        auto label = labels.withX ((int) (bars.getX() + column * columnWidth)).withWidth ((int) columnWidth);
        g.setColour (text);
        g.drawText (warbl::holeName (hole), label.removeFromTop (16), juce::Justification::centred);
        g.setColour (text.withAlpha (0.7f));
        g.drawText (juce::String (raw), label, juce::Justification::centred);
    }
}

//==============================================================================
ScopeView::ScopeView (juce::String t, float minV, float maxV) : title (std::move (t)), minValue (minV), maxValue (maxV) {}

void ScopeView::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto area = getLocalBounds().reduced (8);
    auto header = area.removeFromTop (18);

    g.setColour (text);
    g.setFont (13.0f);
    g.drawText (title, header, juce::Justification::centredLeft);

    // Legend
    auto legend = header.removeFromRight (240);
    for (auto it = traces.rbegin(); it != traces.rend(); ++it)
    {
        g.setColour (it->colour);
        g.drawText (it->name, legend.removeFromRight (60), juce::Justification::centredRight);
    }

    const auto plot = area.toFloat();
    auto toY = [&] (float v) { return juce::jmap (v, minValue, maxValue, plot.getBottom(), plot.getY()); };

    g.setColour (grid);
    if (minValue < 0.0f && maxValue > 0.0f)
        g.drawHorizontalLine ((int) toY (0.0f), plot.getX(), plot.getRight());

    if (marker)
    {
        g.setColour (coveredColour.withAlpha (0.7f));
        g.drawHorizontalLine ((int) toY (*marker), plot.getX(), plot.getRight());
    }

    for (const auto& trace : traces)
    {
        if (trace.values.size() < 2)
            continue;

        juce::Path path;
        const float dx = plot.getWidth() / (float) (trace.values.size() - 1);
        for (size_t i = 0; i < trace.values.size(); ++i)
        {
            const juce::Point<float> p (plot.getX() + (float) i * dx, juce::jlimit (plot.getY(), plot.getBottom(), toY (trace.values[i])));
            if (i == 0)
                path.startNewSubPath (p);
            else
                path.lineTo (p);
        }
        g.setColour (trace.colour);
        g.strokePath (path, juce::PathStrokeType (1.2f));
    }
}

//==============================================================================
void TimingView::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto area = getLocalBounds().reduced (8);

    uint64_t total = stats.histogramOverflow;
    uint32_t peak = 1;
    for (auto c : stats.histogram)
    {
        total += c;
        peak = std::max (peak, c);
    }

    auto percentile = [&] (double p) -> juce::String
    {
        if (total == 0)
            return "-";
        const auto target = (uint64_t) std::ceil (p * (double) total);
        uint64_t sum = 0;
        for (size_t i = 0; i < stats.histogram.size(); ++i)
        {
            sum += stats.histogram[i];
            if (sum >= target)
                return juce::String ((double) (i + 1) * StreamReceiver::kHistogramBinMs, 1) + " ms";
        }
        return "> " + juce::String (StreamReceiver::kHistogramBins * StreamReceiver::kHistogramBinMs, 0) + " ms";
    };

    g.setColour (text);
    g.setFont (13.0f);
    g.drawText ("Arrival jitter (delay beyond the fastest recent frame)   p50 " + percentile (0.5)
                    + "   p99 " + percentile (0.99) + "   p99.9 " + percentile (0.999),
                area.removeFromTop (18), juce::Justification::centredLeft);

    auto axis = area.removeFromBottom (16);
    const float binWidth = (float) area.getWidth() / StreamReceiver::kHistogramBins;
    g.setColour (accent);
    for (size_t i = 0; i < stats.histogram.size(); ++i)
    {
        if (stats.histogram[i] == 0)
            continue;
        // Log scale so rare late frames stay visible.
        const float h = std::log1p ((float) stats.histogram[i]) / std::log1p ((float) peak) * (float) area.getHeight();
        g.fillRect ((float) area.getX() + (float) i * binWidth, (float) area.getBottom() - h, std::max (1.0f, binWidth - 1.0f), h);
    }

    g.setColour (text.withAlpha (0.7f));
    for (int ms = 0; ms <= 10; ms += 2)
        g.drawText (juce::String (ms) + " ms", (int) (area.getX() + ms / StreamReceiver::kHistogramBinMs * binWidth), axis.getY(), 40, 16,
                    juce::Justification::centredLeft);
}

//==============================================================================
MonitorPage::MonitorPage (StreamReceiver& r, AudioEngine& a) : receiver (r), audio (a)
{
    for (auto* l : { &statusLabel, &infoLabel, &statsLabel, &audioLabel, &reverbLabel, &wetLabel, &preDelayLabel, &spreadLabel, &reverbStatus })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (l);
    }

    for (auto* b : { &streamButton, &suppressButton, &simulateButton, &bluetoothSimButton, &smoothBluetoothButton })
    {
        b->setColour (juce::ToggleButton::textColourId, text);
        b->setWantsKeyboardFocus (false);  // Keep the space bar for the test phrase.
        addAndMakeVisible (b);
    }

    for (auto* b : { &infoButton, &audioButton, &resetButton, &loadIrButton, &playButton, &recordButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &holeView, &pressureScope, &accelScope, &gyroScope, &timingView })
        addAndMakeVisible (c);

    streamButton.setToggleState (true, juce::dontSendNotification);  // Streaming is what the app is for.
    streamButton.onClick = [this] { applyStreamSettings(); };
    suppressButton.onClick = [this] { applyStreamSettings(); };
    infoButton.onClick = [this] { receiver.requestInfo(); };
    audioButton.onClick = [this] { showAudioSettings(); };
    resetButton.onClick = [this]
    {
        receiver.resetStats();
        audio.maxFramesPerBlock = 0;
    };

    const auto& rooms = reverb::presets();
    for (size_t i = 0; i < rooms.size(); ++i)
        roomBox.addItem (rooms[i].name, (int) i + 1);
    roomBox.setSelectedId (audio.reverb.getPresetIndex() + 1, juce::dontSendNotification);  // 0 (none) for an IR file
    roomBox.onChange = [this]
    {
        if (roomBox.getSelectedId() > 0)
            audio.reverb.loadPreset (roomBox.getSelectedId() - 1);
    };
    addAndMakeVisible (roomBox);

    wetSlider.setRange (-40.0, 6.0, 0.5);
    wetSlider.setTextValueSuffix (" dB");
    wetSlider.setValue (audio.reverb.wetDb.load(), juce::dontSendNotification);
    wetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    for (auto* s : { &wetSlider, &preDelaySlider, &spreadSlider })
        s->setScrollWheelEnabled (false);  // Only dragging or typing changes a value
    wetSlider.setTooltip ("0 dB is the natural balance for a listener at the room's distance; lower is drier.");
    wetSlider.onValueChange = [this] { audio.reverb.wetDb = (float) wetSlider.getValue(); };
    addAndMakeVisible (wetSlider);

    preDelaySlider.setRange (0.0, 100.0, 1.0);
    preDelaySlider.setTextValueSuffix (" ms");
    preDelaySlider.setValue (audio.reverb.preDelayMs.load(), juce::dontSendNotification);
    preDelaySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    preDelaySlider.onValueChange = [this] { audio.reverb.preDelayMs = (float) preDelaySlider.getValue(); };
    addAndMakeVisible (preDelaySlider);

    spreadSlider.setRange (0.0, 90.0, 1.0);
    spreadSlider.setTextValueSuffix (juce::String::fromUTF8 ("°"));
    spreadSlider.setValue (audio.reverb.getSpread(), juce::dontSendNotification);
    spreadSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    spreadSlider.setTooltip ("The whistle is two sound sources: the window at the mouth and the tone holes and foot along the tube. "
                             "This is the angle between them as you hear them (window left, tube right): each gets its own "
                             "direction and its own reflections in the room. 0 = one source, as before.");
    spreadSlider.onValueChange = [this] { audio.reverb.setSpread ((float) spreadSlider.getValue()); };
    addAndMakeVisible (spreadSlider);

    playButton.onClick = [this] { audio.playScale(); };
    recordButton.setTooltip ("Records what you play: the sound (WAV) and every sensor frame with your settings (CSV), into "
                             "Downloads/WARBL Stream recordings, so a problem can be replayed and analysed offline.");
    recordButton.onClick = [this] { toggleRecording(); };
    playButton.setTooltip ("Plays the instrument (Instrument tab) through a scale of its own fingerings, two octaves up, with the breath each note needs (space bar does the same).");
    simulateButton.setTooltip ("Plays a scripted scale as if from WARBL, through the same path as the real stream.");
    bluetoothSimButton.setTooltip ("The simulator sends its frames as Bluetooth would: in batches, one every 15 ms.");
    bluetoothSimButton.onClick = [this] { receiver.simulateBluetooth = bluetoothSimButton.getToggleState(); };
    smoothBluetoothButton.setToggleState (audio.smoothBluetooth.load(), juce::dontSendNotification);
    smoothBluetoothButton.setTooltip ("Over Bluetooth, frames arrive in bursts. On: play them with their own spacing, delayed just "
                                      "enough to absorb the bursts (smooth). Off: apply each burst as it arrives (lowest delay, steppier).");
    smoothBluetoothButton.onClick = [this] { audio.smoothBluetooth = smoothBluetoothButton.getToggleState(); };
    simulateButton.onClick = [this]
    {
        receiver.setSimulating (simulateButton.getToggleState());
        history.clear();
    };
    setWantsKeyboardFocus (true);
    loadIrButton.onClick = [this]
    {
        irChooser = std::make_unique<juce::FileChooser> ("Choose an impulse response", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        irChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file == juce::File())
                return;
            const auto error = audio.reverb.loadFile (file);
            if (error.isNotEmpty())
                reverbStatus.setText (error, juce::dontSendNotification);
            else
                roomBox.setSelectedId (0, juce::dontSendNotification);
        });
    };

    receiver.onInfo = [this] (const warbl::DeviceInfo& info) { showInfo (info); };

    // Rescan whenever MIDI devices come or go, e.g. when WARBL is plugged in.
    deviceListConnection = juce::MidiDeviceListConnection::make ([this] { tryConnect(); });

    tryConnect();
    startTimerHz (30);
}

MonitorPage::~MonitorPage()
{
    if (receiver.isConnected() && streamButton.getToggleState())
        receiver.setStream (false, false);  // Leave WARBL in its normal state.
}

void MonitorPage::tryConnect()
{
    if (receiver.connect())
    {
        statusLabel.setText ("Ports: " + receiver.getPortNames(), juce::dontSendNotification);
        if (! receiver.isSimulating())
            receiver.requestInfo();
        applyStreamSettings();
    }
    else
    {
        statusLabel.setText ("WARBL not found (connect it with a USB cable, or use Simulate WARBL)", juce::dontSendNotification);
    }
}

void MonitorPage::applyStreamSettings()
{
    receiver.setStream (streamButton.getToggleState(), suppressButton.getToggleState());
    receiver.resetStats();
}

void MonitorPage::showInfo (const warbl::DeviceInfo& info)
{
    deviceInfo = info;
    audio.setCalibration (info);
    infoLabel.setText ("Firmware " + juce::String (info.firmwareVersion / 10.0, 1) + "   ambient pressure "
                           + juce::String (info.ambientPressure) + " (14-bit)   loop " + juce::String (info.loopPeriodMs) + " ms",
                       juce::dontSendNotification);
}

void MonitorPage::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (audio.deviceManager, 0, 0, 2, 2, false, false, true, false);
    selector->setSize (500, 400);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MonitorPage::toggleRecording()
{
    auto& recorder = audio.recorder;
    if (recorder.isRecording())
    {
        recorder.stop();
        recordButton.setButtonText ("Record");
        recordingMessage = "Saved " + recorder.getBaseName() + ".wav and .csv in Downloads/WARBL Stream recordings";
        recordingMessageUntilMs = juce::Time::getMillisecondCounterHiRes() + 8000.0;
        return;
    }
    auto header = recordingHeader ? recordingHeader() : juce::StringArray();
    header.add ("sample_rate " + juce::String (audio.getSampleRate()));
    header.add (audio.calibrationText());
    if (recorder.start (audio.getSampleRate(), header))
        recordButton.setButtonText ("Stop recording");
    else
    {
        recordingMessage = "Couldn't create files in " + recorder.getFolder().getFullPathName();
        recordingMessageUntilMs = juce::Time::getMillisecondCounterHiRes() + 8000.0;
    }
}

void MonitorPage::timerCallback()
{
    if (isShowing() && ! hasKeyboardFocus (true))
        grabKeyboardFocus();  // So the space bar works without clicking the window first.

    // The MIDI device list callback can come before the device is ready, so also retry here.
    static int retry = 0;
    if (! receiver.isConnected() && ++retry % 30 == 0)
        tryConnect();

    ReceivedFrame received;
    while (receiver.uiQueue.pop (received))
    {
        audio.recorder.addFrame (received);
        history.push_back (received);
        if (history.size() > kHistoryFrames)
            history.pop_front();
    }

    const auto streamingPort = receiver.getStreamingPortName();
    if (streamingPort.isNotEmpty())
        statusLabel.setText ("Streaming from: " + streamingPort, juce::dontSendNotification);
    if (audio.recorder.isRecording())
        statusLabel.setText ("Recording " + juce::String (audio.recorder.getSeconds(), 0) + " s", juce::dontSendNotification);
    else if (juce::Time::getMillisecondCounterHiRes() < recordingMessageUntilMs)
        statusLabel.setText (recordingMessage, juce::dontSendNotification);

    const auto stats = receiver.getStats();
    timingView.update (stats);
    statsLabel.setText (juce::String (stats.framesPerSecond, 0) + " frames/s   lost " + juce::String (stats.lostFrames)
                            + "   new hole readings in " + juce::String (juce::roundToInt (stats.freshHoles * 100.0)) + " % of frames"
                            + "   garbled ones rejected " + juce::String (stats.rejectedHoleReadings)
                            + (stats.bluetooth ? "   Bluetooth: " + juce::String (stats.framesPerBatch, 1) + " frames per batch every "
                                                     + juce::String (stats.batchIntervalMs, 1) + " ms"
                                                     + (audio.smoothBluetooth.load() ? ", smoothing +" + juce::String (audio.bluetoothDelayMs.load(), 1) + " ms"
                                                                                     : juce::String (", not smoothed"))
                                               : juce::String()),
                        juce::dontSendNotification);

    if (auto* device = audio.deviceManager.getCurrentAudioDevice())
    {
        const double rate = device->getCurrentSampleRate();
        const int buffer = device->getCurrentBufferSizeSamples();
        const int outLatency = device->getOutputLatencyInSamples();
        audioLabel.setText (device->getName() + "   " + juce::String (rate / 1000.0, 1) + " kHz   buffer " + juce::String (buffer)
                                + " (" + juce::String (buffer * 1000.0 / rate, 2) + " ms)   output latency "
                                + juce::String ((buffer + outLatency) * 1000.0 / rate, 2) + " ms   max frames/block "
                                + juce::String (audio.maxFramesPerBlock.load()) + "   blocks " + juce::String ((juce::int64) audio.blocksProcessed.load()),
                            juce::dontSendNotification);
    }
    else
    {
        audioLabel.setText ("No audio output", juce::dontSendNotification);
    }

    reverbStatus.setText ("IR: " + audio.reverb.getCurrentName() + "   added latency " + juce::String (audio.reverb.getLatencySamples()) + " samples",
                          juce::dontSendNotification);

    if (history.empty())
        return;

    holeView.update (history.back().frame, deviceInfo);

    const int ambient = deviceInfo ? deviceInfo->ambientPressure : 0;
    ScopeView::Trace pressure { "pressure", accent, {} };
    ScopeView::Trace ax { "x", juce::Colours::indianred, {} }, ay { "y", juce::Colours::lightgreen, {} }, az { "z", juce::Colours::cornflowerblue, {} };
    ScopeView::Trace gx { "x", juce::Colours::indianred, {} }, gy { "y", juce::Colours::lightgreen, {} }, gz { "z", juce::Colours::cornflowerblue, {} };
    const warbl::DeviceInfo units = deviceInfo.value_or (warbl::DeviceInfo {});

    for (const auto& r : history)
    {
        const auto& f = r.frame;
        pressure.values.push_back ((float) ((int) f.pressure - ambient) * AudioEngine::kPascalPerCount);
        ax.values.push_back (units.accelToG (f.accel[0]));
        ay.values.push_back (units.accelToG (f.accel[1]));
        az.values.push_back (units.accelToG (f.accel[2]));
        gx.values.push_back (units.gyroToDps (f.gyro[0]));
        gy.values.push_back (units.gyroToDps (f.gyro[1]));
        gz.values.push_back (units.gyroToDps (f.gyro[2]));
    }

    pressureScope.setTraces ({ pressure });
    accelScope.setTraces ({ ax, ay, az });
    gyroScope.setTraces ({ gx, gy, gz });
}

bool MonitorPage::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        audio.playScale();
        return true;
    }
    return false;
}

void MonitorPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void MonitorPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto row1 = area.removeFromTop (28);
    streamButton.setBounds (row1.removeFromLeft (90));
    suppressButton.setBounds (row1.removeFromLeft (140));
    infoButton.setBounds (row1.removeFromLeft (110).reduced (2));
    resetButton.setBounds (row1.removeFromLeft (100).reduced (2));
    audioButton.setBounds (row1.removeFromLeft (130).reduced (2));
    recordButton.setBounds (row1.removeFromLeft (130).reduced (2));
    statusLabel.setBounds (row1);

    infoLabel.setBounds (area.removeFromTop (22));
    statsLabel.setBounds (area.removeFromTop (22));
    audioLabel.setBounds (area.removeFromTop (22));

    auto clickRow = area.removeFromTop (28);
    simulateButton.setBounds (clickRow.removeFromRight (150));
    bluetoothSimButton.setBounds (clickRow.removeFromRight (120));
    smoothBluetoothButton.setBounds (clickRow.removeFromRight (160));

    auto reverbRow = area.removeFromTop (28);
    reverbLabel.setBounds (reverbRow.removeFromLeft (60));
    roomBox.setBounds (reverbRow.removeFromLeft (180).reduced (2));
    loadIrButton.setBounds (reverbRow.removeFromLeft (120).reduced (2));
    wetLabel.setBounds (reverbRow.removeFromLeft (45));
    wetSlider.setBounds (reverbRow.removeFromLeft (170));
    preDelayLabel.setBounds (reverbRow.removeFromLeft (70));
    preDelaySlider.setBounds (reverbRow.removeFromLeft (160));
    spreadLabel.setBounds (reverbRow.removeFromLeft (52));
    spreadSlider.setBounds (reverbRow.removeFromLeft (160));
    playButton.setBounds (reverbRow.removeFromLeft (130).reduced (2));
    reverbStatus.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);

    auto top = area.removeFromTop (area.getHeight() / 2);
    holeView.setBounds (top.removeFromLeft (400));
    top.removeFromLeft (6);
    pressureScope.setBounds (top);

    area.removeFromTop (6);
    auto bottom = area;
    timingView.setBounds (bottom.removeFromBottom (150));
    bottom.removeFromBottom (6);
    auto left = bottom.removeFromLeft (bottom.getWidth() / 2);
    accelScope.setBounds (left.withTrimmedRight (3));
    gyroScope.setBounds (bottom.withTrimmedLeft (3));
}
