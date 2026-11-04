// Room impulse responses built from simple room acoustics:
//  - early reflections from mirror images of the source in a box-shaped room (to the "mixing time"),
//    heard by two ears 17 cm apart;
//  - a diffuse tail of noise whose level follows from the room volume and whose decay time is set per
//    frequency (so highs die away sooner, as in real rooms), shaped smoothly in the frequency domain.
// The direct sound is left out (the dry signal provides it). The IR is scaled so the direct sound would
// have amplitude 1, which keeps the physically correct direct-to-reverberant balance for the distance.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace reverb
{

constexpr std::array<float, 8> kBandCentres { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };

struct RoomSpec
{
    juce::String name;
    float width, depth, height;   // m
    std::array<float, 8> rt60;    // Reverberation time (s) in the octave bands above.
    float distance;               // Source to listener (m).
};

const std::vector<RoomSpec>& presets();

// Reverberation time at any frequency, interpolated between bands on a log-frequency scale.
float rt60At (const RoomSpec& room, float hz);

// Mixing time: when reflections become dense enough to treat as diffuse (rule of thumb sqrt(V) ms).
double mixingTimeSeconds (const RoomSpec& room);

// Stereo IR, without the direct sound. `sourceOffset` moves the source sideways (m, + = to the listener's right)
// from straight ahead of the listener; a different `seed` gives an independent diffuse tail.
juce::AudioBuffer<float> generate (const RoomSpec& room, double sampleRate, uint32_t seed = 1, double sourceOffset = 0.0);

} // namespace reverb
