#include "MainComponent.h"

namespace
{
juce::var sensorsToVar (const AudioEngine& a)
{
    auto o = juce::var (new juce::DynamicObject());
    auto* d = o.getDynamicObject();
    d->setProperty ("breathInputMin_Pa", a.breathInputMin.load());
    d->setProperty ("breathInputMax_Pa", a.breathInputMax.load());
    d->setProperty ("breathCurve", a.breathCurve.load());
    d->setProperty ("breathOutputMax_Pa", a.breathOutputMax.load());
    d->setProperty ("holeOpenBelow", a.holeOpenBelow.load());
    d->setProperty ("holeClosedAt", a.holeClosedAt.load());
    d->setProperty ("holeCurve", a.holeCurve.load());
    d->setProperty ("holeSCurve", a.holeSCurve.load());
    d->setProperty ("holeCentre", a.holeCentre.load());
    d->setProperty ("holeFingerHeight", a.holeFingerHeight.load());
    d->setProperty ("holeFingerRange_mm", a.holeFingerRange.load());
    d->setProperty ("thumbOwn", a.thumbOwn.load());
    d->setProperty ("thumbOpenBelow", a.thumbOpenBelow.load());
    d->setProperty ("thumbClosedAt", a.thumbClosedAt.load());
    d->setProperty ("thumbCurve", a.thumbCurve.load());
    d->setProperty ("tiltEnabled", a.tiltEnabled.load());
    d->setProperty ("tiltInvert", a.tiltInvert.load());
    d->setProperty ("tiltZero_deg", a.tiltZero.load());
    d->setProperty ("tiltDeadZone_deg", a.tiltDeadZone.load());
    d->setProperty ("tiltRange_deg", a.tiltRange.load());
    d->setProperty ("tiltMaxShift_mm", a.tiltMaxShift.load());
    return o;
}

void sensorsFromVar (const juce::var& o, AudioEngine& a)
{
    auto read = [&] (const char* key, std::atomic<float>& value)
    {
        if (o.hasProperty (key))
            value = (float) (double) o[key];
    };
    read ("breathInputMin_Pa", a.breathInputMin);
    read ("breathInputMax_Pa", a.breathInputMax);
    read ("breathCurve", a.breathCurve);
    read ("breathOutputMax_Pa", a.breathOutputMax);
    read ("holeOpenBelow", a.holeOpenBelow);
    read ("holeClosedAt", a.holeClosedAt);
    read ("holeCurve", a.holeCurve);
    read ("holeCentre", a.holeCentre);
    read ("holeFingerRange_mm", a.holeFingerRange);
    read ("thumbOpenBelow", a.thumbOpenBelow);
    read ("thumbClosedAt", a.thumbClosedAt);
    read ("thumbCurve", a.thumbCurve);
    if (o.hasProperty ("thumbOwn"))
        a.thumbOwn = (bool) o["thumbOwn"];
    read ("tiltZero_deg", a.tiltZero);
    read ("tiltDeadZone_deg", a.tiltDeadZone);
    read ("tiltRange_deg", a.tiltRange);
    read ("tiltMaxShift_mm", a.tiltMaxShift);
    if (o.hasProperty ("tiltEnabled"))
        a.tiltEnabled = (bool) o["tiltEnabled"];
    if (o.hasProperty ("tiltInvert"))
        a.tiltInvert = (bool) o["tiltInvert"];
    if (o.hasProperty ("holeFingerHeight"))
        a.holeFingerHeight = (bool) o["holeFingerHeight"];
    if (o.hasProperty ("holeSCurve"))
        a.holeSCurve = (bool) o["holeSCurve"];
}
} // namespace

MainComponent::MainComponent()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "WARBL Stream";
    options.folderName = "WARBL Stream";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters (options);

    // The engine's settings are restored first, so the pages show them when they are made.
    restoreState();

    const juce::Colour background { 0xff1e1f22 };
    monitor = new MonitorPage (receiver, audio);
    instrument = new InstrumentPage (audio);
    monitor->recordingHeader = [this]
    {
        juce::StringArray lines;
        lines.add ("WARBL Stream recording " + juce::Time::getCurrentTime().toISO8601 (true));
        lines.add ("instrument " + juce::JSON::toString (whistle::toVar (instrument->getInstrument()), true));
        lines.add ("sensors " + juce::JSON::toString (sensorsToVar (audio), true));
        return lines;
    };
    tabs.addTab ("Monitor", background, monitor, true);
    tabs.addTab ("Instrument", background, instrument, true);
    tabs.addTab ("Sensors", background, new SensorsPage (audio), true);
    tabs.addTab ("Model", background, new ModelPage (audio), true);
    tabs.setTabBarDepth (30);
    addAndMakeVisible (tabs);

    if (auto* settings = properties.getUserSettings())
    {
        juce::String error;
        if (auto saved = whistle::fromJson (settings->getValue ("instrument"), error))
            instrument->setInstrument (*saved);
        tabs.setCurrentTabIndex (settings->getIntValue ("tab", 0));
    }

    setSize (1200, 910);
}

MainComponent::~MainComponent()
{
    if (saveOnExit)
        saveState();
    tabs.clearTabs();  // Pages go before the engine they use.
}

void MainComponent::restoreState()
{
    auto* settings = properties.getUserSettings();
    if (settings == nullptr)
        return;

    if (auto device = settings->getXmlValue ("audioDevice"))
        audio.deviceManager.initialise (0, 2, device.get(), true);

    sensorsFromVar (juce::JSON::parse (settings->getValue ("sensors")), audio);

    if (settings->containsKey ("smoothBluetooth"))
        audio.smoothBluetooth = settings->getBoolValue ("smoothBluetooth");
    if (settings->containsKey ("sourceSpread_deg"))
        audio.reverb.setSpread ((float) settings->getDoubleValue ("sourceSpread_deg"));
    const auto irFile = juce::File (settings->getValue ("reverbFile"));
    if (irFile.existsAsFile())
        audio.reverb.loadFile (irFile);
    else if (settings->containsKey ("reverbRoom"))
        audio.reverb.loadPreset (settings->getIntValue ("reverbRoom"));
    if (settings->containsKey ("reverbLevel_dB"))
        audio.reverb.wetDb = (float) settings->getDoubleValue ("reverbLevel_dB");
    if (settings->containsKey ("reverbPreDelay_ms"))
        audio.reverb.preDelayMs = (float) settings->getDoubleValue ("reverbPreDelay_ms");
}

void MainComponent::saveState()
{
    auto* settings = properties.getUserSettings();
    if (settings == nullptr)
        return;

    if (instrument != nullptr)
        settings->setValue ("instrument", whistle::toJson (instrument->getInstrument()));
    settings->setValue ("sensors", juce::JSON::toString (sensorsToVar (audio), true));
    settings->setValue ("reverbRoom", audio.reverb.getPresetIndex());
    settings->setValue ("reverbFile", audio.reverb.getCurrentFile().getFullPathName());
    settings->setValue ("reverbLevel_dB", audio.reverb.wetDb.load());
    settings->setValue ("reverbPreDelay_ms", audio.reverb.preDelayMs.load());
    settings->setValue ("sourceSpread_deg", audio.reverb.getSpread());
    settings->setValue ("smoothBluetooth", audio.smoothBluetooth.load());
    settings->setValue ("tab", tabs.getCurrentTabIndex());
    if (auto device = audio.deviceManager.createStateXml())
        settings->setValue ("audioDevice", device.get());
    properties.saveIfNeeded();
}

void MainComponent::resized()
{
    tabs.setBounds (getLocalBounds());
}
