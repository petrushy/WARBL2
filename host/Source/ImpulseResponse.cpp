#include "ImpulseResponse.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <random>

namespace reverb
{

namespace
{
constexpr double kSpeedOfSound = 343.0;
constexpr double kEarSpacing = 0.17;
constexpr double kPi = juce::MathConstants<double>::pi;

// Adds a band-limited impulse at a fractional sample position (Hann-windowed sinc).
void addImpulse (float* out, int length, double position, float amplitude)
{
    constexpr int halfWidth = 8;
    const int centre = (int) std::floor (position);
    for (int i = centre - halfWidth + 1; i <= centre + halfWidth; ++i)
    {
        if (i < 0 || i >= length)
            continue;
        const double x = (double) i - position;
        const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (kPi * x) / (kPi * x);
        const double window = 0.5 + 0.5 * std::cos (kPi * x / halfWidth);
        out[i] += (float) (amplitude * sinc * window);
    }
}

double imageCoordinate (int index, double size, double position)
{
    return (index % 2 == 0) ? index * size + position : (index + 1) * size - position;
}
} // namespace

const std::vector<RoomSpec>& presets()
{
    // Reverberation times are typical of measured rooms of each kind (125 Hz ... 16 kHz).
    static const std::vector<RoomSpec> rooms {
        { "Small wooden room", 5.0f, 4.0f, 2.7f, { 0.45f, 0.42f, 0.40f, 0.38f, 0.36f, 0.32f, 0.26f, 0.20f }, 1.2f },
        { "Studio live room", 9.0f, 7.0f, 4.0f, { 0.75f, 0.70f, 0.65f, 0.60f, 0.55f, 0.48f, 0.38f, 0.26f }, 2.0f },
        { "Chamber music hall", 22.0f, 15.0f, 9.0f, { 1.70f, 1.60f, 1.50f, 1.40f, 1.30f, 1.10f, 0.80f, 0.50f }, 4.0f },
        { "Concert hall", 40.0f, 25.0f, 17.0f, { 2.60f, 2.40f, 2.20f, 2.00f, 1.85f, 1.55f, 1.10f, 0.70f }, 6.0f },
        { "Stone church", 45.0f, 18.0f, 20.0f, { 5.00f, 4.60f, 4.20f, 3.80f, 3.20f, 2.50f, 1.70f, 1.00f }, 6.0f },
    };
    return rooms;
}

float rt60At (const RoomSpec& room, float hz)
{
    if (hz <= kBandCentres.front())
        return room.rt60.front();
    if (hz >= kBandCentres.back())
        return room.rt60.back();

    size_t band = 0;
    while (hz > kBandCentres[band + 1])
        ++band;

    const float t = std::log2 (hz / kBandCentres[band]);  // Bands are an octave apart.
    return room.rt60[band] + t * (room.rt60[band + 1] - room.rt60[band]);
}

double mixingTimeSeconds (const RoomSpec& room)
{
    const double volume = (double) room.width * room.depth * room.height;
    return juce::jlimit (0.008, 0.12, std::sqrt (volume) * 0.001);
}

juce::AudioBuffer<float> generate (const RoomSpec& room, double sampleRate, uint32_t seed, double sourceOffset)
{
    const double W = room.width, D = room.depth, H = room.height;
    const double volume = W * D * H;
    const double surface = 2.0 * (W * D + W * H + D * H);

    float maxRt = 0.0f;
    for (auto t : room.rt60)
        maxRt = std::max (maxRt, t);

    const int length = (int) (sampleRate * std::min (8.0, 1.2 * maxRt + 0.1));
    juce::AudioBuffer<float> ir (2, length);
    ir.clear();

    // Source near the front of the room (moved sideways by sourceOffset), listener in the middle further back,
    // both at ear height.
    const double lx = W * 0.5;
    const double sx = juce::jlimit (0.3, W - 0.3, lx + sourceOffset), sy = std::min (D * 0.25, 4.0), sz = std::min (1.5, H * 0.5);
    const double ly = std::min (sy + room.distance, D - 0.5), lz = std::min (1.6, H * 0.6);
    const double distance = std::hypot (ly - sy, lz - sz);
    const double ears[2] = { lx - kEarSpacing * 0.5, lx + kEarSpacing * 0.5 };

    const double mixTime = mixingTimeSeconds (room);
    const double rampStart = 0.5 * mixTime, rampEnd = 1.5 * mixTime;
    auto tailWeight = [&] (double t)  // 0 before the mixing time, 1 after, raised cosine between.
    {
        if (t <= rampStart) return 0.0;
        if (t >= rampEnd) return 1.0;
        return 0.5 - 0.5 * std::cos (kPi * (t - rampStart) / (rampEnd - rampStart));
    };

    //==========================================================================
    // Early reflections: mirror images up to the end of the crossfade. Energy loss per reflection from
    // Sabine's formula at mid frequencies; the high-frequency loss is applied below with a gentle lowpass.
    const double alpha = juce::jlimit (0.01, 0.95, 0.161 * volume / (surface * rt60At (room, 1000.0f)));
    const double reflectionGain = std::sqrt (1.0 - alpha);
    const double maxPath = distance + kSpeedOfSound * rampEnd;
    const int nx = (int) std::ceil (maxPath / W) + 1, ny = (int) std::ceil (maxPath / D) + 1, nz = (int) std::ceil (maxPath / H) + 1;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = ir.getWritePointer (ch);
        const double ex = ears[ch];
        const double direct = std::sqrt ((ex - sx) * (ex - sx) + (ly - sy) * (ly - sy) + (lz - sz) * (lz - sz));

        for (int i = -nx; i <= nx; ++i)
            for (int j = -ny; j <= ny; ++j)
                for (int k = -nz; k <= nz; ++k)
                {
                    if (i == 0 && j == 0 && k == 0)
                        continue;

                    const double x = imageCoordinate (i, W, sx), y = imageCoordinate (j, D, sy), z = imageCoordinate (k, H, sz);
                    const double path = std::sqrt ((x - ex) * (x - ex) + (y - ly) * (y - ly) + (z - lz) * (z - lz));
                    const double t = (path - direct) / kSpeedOfSound;
                    if (t >= rampEnd)
                        continue;

                    const int order = std::abs (i) + std::abs (j) + std::abs (k);
                    const double amplitude = std::pow (reflectionGain, order) * direct / path * (1.0 - tailWeight (t));
                    addImpulse (out, length, t * sampleRate, (float) amplitude);
                }

        // Walls and air absorb highs: one-pole lowpass around 7 kHz on the reflections.
        const float coeff = (float) std::exp (-2.0 * kPi * 7000.0 / sampleRate);
        float state = 0.0f;
        const int erLength = std::min (length, (int) (rampEnd * sampleRate) + 32);
        for (int n = 0; n < erLength; ++n)
            out[n] = state = (1.0f - coeff) * out[n] + coeff * state;
    }

    //==========================================================================
    // Diffuse tail. In a room of volume V, reflections arrive with an energy rate of 4*pi*c/V relative
    // to a direct sound of energy 1/r^2 (statistical room acoustics), decaying 60 dB per RT60.
    // White noise is shaped frame by frame in the frequency domain with that envelope per frequency.
    constexpr int fftOrder = 10;
    constexpr int N = 1 << fftOrder, hop = N / 4;
    juce::dsp::FFT fft (fftOrder);

    std::vector<float> window ((size_t) N);
    for (int n = 0; n < N; ++n)
        window[(size_t) n] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * n / N));  // sqrt-Hann: analysis * synthesis = Hann.
    constexpr float olaGain = 1.0f / 2.0f;  // Sum of Hann windows at 75 % overlap is 2.

    const double energyRate = 4.0 * kPi * kSpeedOfSound / volume * distance * distance;  // Per second, with direct = 1.
    const double noiseScale = std::sqrt (energyRate / sampleRate);

    std::vector<double> decayRate ((size_t) N / 2 + 1), spectralShape ((size_t) N / 2 + 1);
    for (int b = 0; b <= N / 2; ++b)
    {
        const double hz = (double) b * sampleRate / N;
        decayRate[(size_t) b] = 6.9078 / rt60At (room, (float) std::max (hz, 20.0));  // Amplitude: exp(-6.9 t / RT60) = -60 dB at RT60.
        const double lowCut = 1.0 / std::sqrt (1.0 + std::pow (60.0 / std::max (hz, 1.0), 4.0));      // Below ~60 Hz.
        const double highCut = 1.0 / std::sqrt (1.0 + std::pow (hz / 18000.0, 8.0));               // Above ~18 kHz.
        spectralShape[(size_t) b] = lowCut * highCut;
    }

    std::mt19937 rng (seed);
    std::normal_distribution<float> gauss (0.0f, 1.0f);
    std::vector<float> noise ((size_t) (length + N)), frame ((size_t) N * 2), tail ((size_t) (length + N));

    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& v : noise)
            v = gauss (rng);
        std::fill (tail.begin(), tail.end(), 0.0f);

        for (int start = -N; start < length; start += hop)
        {
            std::fill (frame.begin(), frame.end(), 0.0f);
            for (int n = 0; n < N; ++n)
                frame[(size_t) n] = start + n >= 0 ? noise[(size_t) (start + n)] * window[(size_t) n] : 0.0f;

            fft.performRealOnlyForwardTransform (frame.data(), true);

            const double t = std::max (0.0, (start + N / 2) / sampleRate);
            for (int b = 0; b <= N / 2; ++b)
            {
                const auto gain = (float) (spectralShape[(size_t) b] * std::exp (-decayRate[(size_t) b] * t));
                frame[(size_t) (2 * b)] *= gain;
                frame[(size_t) (2 * b + 1)] *= gain;
            }

            fft.performRealOnlyInverseTransform (frame.data());

            for (int n = 0; n < N; ++n)
                if (start + n >= 0 && start + n < length)
                    tail[(size_t) (start + n)] += frame[(size_t) n] * window[(size_t) n] * olaGain;
        }

        auto* out = ir.getWritePointer (ch);
        for (int n = 0; n < length; ++n)
            out[n] += (float) (tail[(size_t) n] * noiseScale * tailWeight (n / sampleRate));

        // Short fade at the very end.
        const int fade = std::min (length, (int) (0.05 * sampleRate));
        for (int n = 0; n < fade; ++n)
            out[length - 1 - n] *= (float) n / (float) fade;
    }

    return ir;
}

} // namespace reverb
