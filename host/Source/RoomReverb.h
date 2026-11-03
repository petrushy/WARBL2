// Convolution reverb with zero added latency. Uses the generated room IRs (ImpulseResponse.h) or an IR file,
// e.g. a measured hall from the OpenAIR library. Mono in, stereo out (wet only; the caller adds the dry signal).

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "ImpulseResponse.h"

#include <atomic>

class RoomReverb
{
public:
    RoomReverb();
    ~RoomReverb();

    void prepare (double sampleRate, int maxBlockSize);

    // Audio thread. Adds the reverb of `input` to both output channels.
    void process (const float* input, float* left, float* right, int numSamples);

    // Message thread. Generates the room's IR in the background and switches to it when ready.
    void loadPreset (int index);
    // Message thread. Returns an error message, or empty on success.
    juce::String loadFile (const juce::File& file);

    std::atomic<float> wetDb { -6.0f };
    std::atomic<float> preDelayMs { 0.0f };

    juce::String getCurrentName() const;
    int getPresetIndex() const { return presetIndex; }  // -1 when an IR file is loaded
    juce::File getCurrentFile() const { return currentFile; }
    int getLatencySamples() { return convolution.getLatency(); }

private:
    void startGenerating (int index);

    double sampleRate = 48000.0;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 1024 } };  // Zero latency, efficient for long IRs.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelay { 48000 };
    juce::SmoothedValue<float> wetGain;
    juce::AudioBuffer<float> work;
    int presetIndex = -1;
    juce::File currentFile;

    mutable juce::CriticalSection nameLock;
    juce::String currentName;

    juce::ThreadPool generator { juce::ThreadPoolOptions().withNumberOfThreads (1) };  // Destroyed first, so jobs finish before the rest.
};
