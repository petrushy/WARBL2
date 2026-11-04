// Convolution reverb with zero added latency. Uses the generated room IRs (ImpulseResponse.h) or an IR file,
// e.g. a measured hall from the OpenAIR library. Two sources (the whistle's window and its tube), each with its
// own room response from its own place in the room; each comes in as the signal at the two ears. Stereo out
// (wet only; the caller adds the dry signal).

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

    // Audio thread. Adds the reverb of the two sources (each as the left and right ear's signal) to the outputs.
    void process (const float* windowLeft, const float* windowRight, const float* bodyLeft, const float* bodyRight,
                  float* left, float* right, int numSamples);

    // Message thread. How far apart the two sources are, as the angle between them seen by the listener
    // (degrees; the window to the left, the tube to the right). Regenerates the room's IRs.
    void setSpread (float degrees);
    float getSpread() const { return spreadDegrees.load(); }
    // Where the sources are, as seen by the listener (radians, + = right): for the direct sound. Any thread.
    float sourceAzimuth (int source) const
    {
        return (source == 0 ? -0.5f : 0.5f) * spreadDegrees.load() * juce::MathConstants<float>::pi / 180.0f;
    }

    // Message thread. Generates the room's IR in the background and switches to it when ready.
    void loadPreset (int index);
    // Message thread. Returns an error message, or empty on success.
    juce::String loadFile (const juce::File& file);

    std::atomic<float> wetDb { -6.0f };
    std::atomic<float> preDelayMs { 0.0f };

    juce::String getCurrentName() const;
    int getPresetIndex() const { return presetIndex; }  // -1 when an IR file is loaded
    juce::File getCurrentFile() const { return currentFile; }
    int getLatencySamples() { return convolution[0].getLatency(); }

private:
    void startGenerating (int index);

    double sampleRate = 48000.0;
    // Zero latency, efficient for long IRs: one per source.
    std::array<juce::dsp::Convolution, 2> convolution { juce::dsp::Convolution { juce::dsp::Convolution::NonUniform { 1024 } },
                                                         juce::dsp::Convolution { juce::dsp::Convolution::NonUniform { 1024 } } };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> preDelay { 48000 };  // 4 channels: two per source
    std::atomic<float> spreadDegrees { 30.0f };
    juce::SmoothedValue<float> wetGain;
    juce::AudioBuffer<float> work;  // 4 channels: window L R, body L R
    int presetIndex = -1;
    juce::File currentFile;

    mutable juce::CriticalSection nameLock;
    juce::String currentName;

    juce::ThreadPool generator { juce::ThreadPoolOptions().withNumberOfThreads (1) };  // Destroyed first, so jobs finish before the rest.
};
