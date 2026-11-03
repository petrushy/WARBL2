#include "AudioEngine.h"

void ClickModel::prepare (double sampleRate, int)
{
    clickLength = juce::roundToInt (sampleRate * 0.001);  // 1 ms
    clickRemaining = 0;
}

void ClickModel::handleFrame (const warbl::SensorFrame& frame)
{
    const int pressure = (int) frame.pressure - ambient12.load();
    const int t = threshold.load();

    if (! above && pressure > t)
    {
        above = true;
        clickRemaining = clickLength;
    }
    else if (above && pressure < t - 10)  // Hysteresis, so noise around the threshold doesn't retrigger.
    {
        above = false;
    }
}

void ClickModel::render (float* const* outputs, int numChannels, int numSamples)
{
    const float g = gain.load();
    for (int i = 0; i < numSamples && clickRemaining > 0; ++i, --clickRemaining)
        for (int ch = 0; ch < numChannels; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][i] += g;
}

AudioEngine::AudioEngine (StreamReceiver& r) : receiver (r)
{
    // Until WARBL reports its calibration, assume typical values.
    for (int i = 0; i < warbl::kNumHoles; ++i)
    {
        baseline[(size_t) i] = 40;
        covered[(size_t) i] = 700;
    }
    for (auto& o : manualOpenness)
        o = 1.0f;

    deviceManager.initialiseWithDefaultDevices (0, 2);

    // Ask for a small buffer; the device may round it.
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = 64;
    deviceManager.setAudioDeviceSetup (setup, true);

    reverb.loadPreset (2);  // Chamber music hall
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::setCalibration (const warbl::DeviceInfo& info)
{
    for (int i = 0; i < warbl::kNumHoles; ++i)
    {
        baseline[(size_t) i] = info.baseline[(size_t) i];
        covered[(size_t) i] = info.covered[(size_t) i];
    }
    ambient12 = info.ambientPressure * 4;
    clickModel.ambient12 = info.ambientPressure * 4;
}

whistle::Controls AudioEngine::controlsFromFrame (const warbl::SensorFrame& frame) const
{
    whistle::Controls c;
    for (int hole = 0; hole < whistle::kNumHoles; ++hole)
    {
        const auto sensor = (size_t) whistle::kSensorForHole[(size_t) hole];
        const float span = (float) std::max (1, covered[sensor].load() - baseline[sensor].load());
        const float coverage = ((float) frame.holes[sensor] - (float) baseline[sensor].load()) / span;
        c.openness[(size_t) hole] = 1.0f - juce::jlimit (0.0f, 1.0f, coverage);
    }
    c.mouthPressure = std::max (0.0f, (float) ((int) frame.pressure - ambient12.load()) * kPascalPerCount * breathScale.load());
    return c;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device->getCurrentSampleRate();
    sampleRate = rate;
    const int maxBlock = std::max (device->getCurrentBufferSizeSamples(), 4096);
    dry.allocate ((size_t) maxBlock, true);
    dryCapacity = maxBlock;

    clickModel.prepare (rate, maxBlock);
    testPhrase.prepare (rate);
    reverb.prepare (rate, maxBlock);
    whistleModel.prepare (rate);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    blocksProcessed++;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // Apply every frame that has arrived as soon as possible: no de-jitter buffer, lowest latency.
    ReceivedFrame received;
    int count = 0;
    whistle::Controls controls;
    bool haveControls = false;
    while (receiver.audioQueue.pop (received))
    {
        clickModel.handleFrame (received.frame);
        controls = controlsFromFrame (received.frame);
        haveControls = true;
        ++count;
    }

    if (manualPlay)
    {
        for (int i = 0; i < whistle::kNumHoles; ++i)
            controls.openness[(size_t) i] = manualOpenness[(size_t) i].load();
        controls.mouthPressure = manualPressure.load();
        haveControls = true;
    }

    // If frames stop arriving (stream off, cable pulled), stop blowing rather than hold the last breath.
    if (! haveControls)
    {
        samplesSinceControls += numSamples;
        if (samplesSinceControls > (int) (0.05 * sampleRate) && lastControls.mouthPressure > 0.0f)
        {
            controls = lastControls;
            controls.mouthPressure = 0.0f;
            haveControls = true;
        }
    }

    if (haveControls)
    {
        samplesSinceControls = 0;
        lastControls = controls;
        whistleModel.setControls (controls);
        for (int i = 0; i < whistle::kNumHoles; ++i)
            shownOpenness[(size_t) i] = controls.openness[(size_t) i];
        shownPressure = controls.mouthPressure;
    }

    if (count > maxFramesPerBlock.load())
        maxFramesPerBlock = count;

    if (numSamples > dryCapacity || numOutputs < 2 || outputs[0] == nullptr || outputs[1] == nullptr)
        return;

    // Instrument and test phrase -> dry to both channels + room reverb.
    juce::FloatVectorOperations::clear (dry.get(), numSamples);
    if (whistleEnabled)
        whistleModel.render (dry.get(), numSamples);
    testPhrase.render (dry.get(), numSamples);

    juce::FloatVectorOperations::copy (outputs[0], dry.get(), numSamples);
    juce::FloatVectorOperations::copy (outputs[1], dry.get(), numSamples);
    reverb.process (dry.get(), outputs[0], outputs[1], numSamples);

    // The click stays dry so latency measurements aren't smeared by the reverb.
    if (clickEnabled)
        clickModel.render (outputs, numOutputs, numSamples);
}
