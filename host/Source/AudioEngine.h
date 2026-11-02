// Audio output. Each block it takes the newest sensor frames from the receiver and feeds them to a SoundModel.
// For now the only model is a click on breath onset, for measuring end-to-end latency. The physical model
// (Faust) will be another SoundModel.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "RoomReverb.h"
#include "TestPhrase.h"
#include "StreamReceiver.h"

#include <atomic>

// Something that turns sensor frames into sound. Called on the audio thread only: no locks or allocation.
class SoundModel
{
public:
    virtual ~SoundModel() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    // Called for each new frame, in order, before render() for the block.
    virtual void handleFrame (const warbl::SensorFrame& frame) = 0;
    virtual void render (float* const* outputs, int numChannels, int numSamples) = 0;
};

// Outputs a short full-scale click when the pressure rises through a threshold, so a recording of
// microphone-at-mouthpiece + this output shows the full breath-to-sound delay.
class ClickModel : public SoundModel
{
public:
    std::atomic<int> threshold { 40 };   // 12-bit pressure counts above ambient.
    std::atomic<int> ambient12 { 0 };    // Ambient pressure, 12-bit (set from the device info).
    std::atomic<float> gain { 0.5f };

    void prepare (double sampleRate, int) override;
    void handleFrame (const warbl::SensorFrame& frame) override;
    void render (float* const* outputs, int numChannels, int numSamples) override;

private:
    int clickLength = 48;
    int clickRemaining = 0;
    bool above = false;
};

class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (StreamReceiver& receiver);
    ~AudioEngine() override;

    juce::AudioDeviceManager deviceManager;
    ClickModel clickModel;
    RoomReverb reverb;
    TestPhrase testPhrase;

    std::atomic<bool> clickEnabled { true };

    // Most frames taken from the queue in one audio block (shows whether frames bunch up).
    std::atomic<int> maxFramesPerBlock { 0 };
    std::atomic<uint64_t> blocksProcessed { 0 };  // Shows whether audio is running.

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    StreamReceiver& receiver;
    juce::HeapBlock<float> dry;  // Mono instrument signal before the reverb.
    int dryCapacity = 0;
};
