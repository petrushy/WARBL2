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

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device->getCurrentSampleRate();
    const int maxBlock = std::max (device->getCurrentBufferSizeSamples(), 4096);
    dry.allocate ((size_t) maxBlock, true);
    dryCapacity = maxBlock;

    clickModel.prepare (rate, maxBlock);
    testPhrase.prepare (rate);
    reverb.prepare (rate, maxBlock);
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
    while (receiver.audioQueue.pop (received))
    {
        clickModel.handleFrame (received.frame);
        ++count;
    }

    if (count > maxFramesPerBlock.load())
        maxFramesPerBlock = count;

    if (numSamples > dryCapacity || numOutputs < 2 || outputs[0] == nullptr || outputs[1] == nullptr)
        return;

    // Instrument (for now the test phrase) -> dry to both channels + room reverb.
    juce::FloatVectorOperations::clear (dry.get(), numSamples);
    testPhrase.render (dry.get(), numSamples);

    juce::FloatVectorOperations::copy (outputs[0], dry.get(), numSamples);
    juce::FloatVectorOperations::copy (outputs[1], dry.get(), numSamples);
    reverb.process (dry.get(), outputs[0], outputs[1], numSamples);

    // The click stays dry so latency measurements aren't smeared by the reverb.
    if (clickEnabled)
        clickModel.render (outputs, numOutputs, numSamples);
}
