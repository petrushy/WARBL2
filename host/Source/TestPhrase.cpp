#include "TestPhrase.h"

#include <cmath>

void TestPhrase::prepare (double rate)
{
    sampleRate = rate;
    playing = false;
}

void TestPhrase::render (float* out, int numSamples)
{
    // D major phrase on a D whistle, with a rest at the end to hear the tail.
    static const Note notes[] = { { 74, 1 }, { 78, 1 }, { 81, 1 }, { 83, 0.5 }, { 81, 0.5 }, { 78, 1 }, { 76, 1 },
                                  { 74, 3 }, { 0, 6 } };
    constexpr size_t numNotes = sizeof (notes) / sizeof (notes[0]);

    if (startRequested.exchange (false))
    {
        playing = true;
        noteIndex = 0;
        noteTime = 0.0;
        envelope = 0.0;
    }

    const double dt = 1.0 / sampleRate;
    for (int i = 0; i < numSamples && playing; ++i)
    {
        const auto& note = notes[noteIndex];
        const double length = note.beats * kBeatSeconds;

        // Slurred notes: the level dips only briefly between notes, like a tongued whistle.
        const bool sounding = note.midi > 0 && noteTime < length - 0.015;
        const double target = sounding ? 1.0 : 0.0;
        envelope += (target - envelope) * (1.0 - std::exp (-dt / (sounding ? 0.015 : 0.02)));

        if (note.midi > 0)
        {
            const double vibrato = noteTime > 0.35 ? 0.15 * std::min (1.0, (noteTime - 0.35) / 0.3) * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * noteTime) : 0.0;
            const double hz = 440.0 * std::pow (2.0, (note.midi - 69 + vibrato) / 12.0);
            phase += hz * dt;
            phase -= std::floor (phase);
        }

        const double w = 2.0 * juce::MathConstants<double>::pi * phase;
        const double tone = std::sin (w) + 0.12 * std::sin (2.0 * w) + 0.04 * std::sin (3.0 * w);
        noiseState = 0.7f * noiseState + 0.3f * (random.nextFloat() * 2.0f - 1.0f);  // Soft breath noise.
        out[i] += (float) (envelope * (0.25 * tone + 0.03 * noiseState));

        noteTime += dt;
        if (noteTime >= length)
        {
            noteTime = 0.0;
            if (++noteIndex >= numNotes)
                playing = false;
        }
    }
}
