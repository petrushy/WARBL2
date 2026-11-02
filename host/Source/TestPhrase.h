#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

// A short synthetic whistle phrase (sine-based tone with breath noise and delayed vibrato), for hearing the
// reverb before there is an instrument model. Audio thread only, except start().
class TestPhrase
{
public:
    void prepare (double sampleRate);
    void start() { startRequested = true; }
    void render (float* out, int numSamples);  // Adds to out.

private:
    struct Note { int midi; double beats; };
    static constexpr double kBeatSeconds = 0.32;

    double sampleRate = 48000.0;
    std::atomic<bool> startRequested { false };
    bool playing = false;
    size_t noteIndex = 0;
    double noteTime = 0.0, phase = 0.0, envelope = 0.0;
    juce::Random random;
    float noiseState = 0.0f;
};
