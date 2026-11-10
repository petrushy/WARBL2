#include "ImpulseResponse.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <random>

namespace reverb
{

namespace
{
constexpr double kSpeedOfSound = 343.0;
constexpr double kHeadRadius = 0.0875;  // m, as the direct sound's head in AudioEngine::placeAtEars.
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

// How many times an image's sound has struck the wall at 0 and the wall at `size` along one axis. Image `index` has
// |index| bounces, alternating; a positive index strikes the far wall first.
void bouncesOnWalls (int index, int& nearWall, int& farWall)
{
    const int more = (std::abs (index) + 1) / 2, fewer = std::abs (index) / 2;
    nearWall = index > 0 ? fewer : more;
    farWall = index > 0 ? more : fewer;
}

// A sound's arrival at one ear relative to the centre of the head (s; negative = earlier), by the angle between its
// direction and that ear's axis (spherical head, Woodworth).
double earDelaySeconds (double angleFromEarAxis)
{
    return kHeadRadius / kSpeedOfSound * (angleFromEarAxis < 0.5 * kPi ? -std::cos (angleFromEarAxis) : angleFromEarAxis - 0.5 * kPi);
}

// Level at the ear for that angle (Brown & Duda's spherical-head filter), relative to a sound from straight ahead,
// which is how the direct sound is heard. x = pi * frequency * head radius / c.
double headShadowGain (double angleFromEarAxis, double x)
{
    constexpr double alphaMin = 0.1, angleOfMin = 150.0 * kPi / 180.0;
    auto alpha = [] (double angle) { return 1.0 + 0.5 * alphaMin + (1.0 - 0.5 * alphaMin) * std::cos (kPi * angle / angleOfMin); };
    const double a = alpha (angleFromEarAxis) * x, ahead = alpha (0.5 * kPi) * x;
    return std::sqrt ((1.0 + a * a) / (1.0 + ahead * ahead));
}

// Fraction of a bounce's sound energy that is scattered away from the mirror direction: more at short wavelengths.
double scatteringAt (const RoomSpec& room, double hz)
{
    return juce::jlimit (0.02, 0.9, room.scattering * std::sqrt (std::max (hz, 1.0) / 1000.0));
}

// Correlation between the two ears in a diffuse sound field: high while the wavelength is long compared with the
// head, nothing to speak of above ~2 kHz. Effective ear spacing 22 cm, wider than the head because sound bends around it.
double diffuseEarCorrelation (double hz)
{
    const double x = 2.0 * kPi * hz * 0.22 / kSpeedOfSound;
    const double sinc = x < 1.0e-6 ? 1.0 : std::sin (x) / x;
    return sinc / (1.0 + std::pow (hz / 2000.0, 4.0));
}
} // namespace

const std::vector<RoomSpec>& presets()
{
    // Reverberation times are typical of measured rooms of each kind (125 Hz ... 16 kHz). Wall weights: left, right,
    // front (behind the player), back, floor, ceiling; halls have seats on the floor and hard surfaces behind the stage.
    static const std::vector<RoomSpec> rooms {
        { "Small wooden room", 5.0f, 4.0f, 2.7f, { 0.45f, 0.42f, 0.40f, 0.38f, 0.36f, 0.32f, 0.26f, 0.20f }, 1.2f, { 1.0f, 1.0f, 1.0f, 1.0f, 1.3f, 0.8f }, 0.20f },
        { "Studio live room", 9.0f, 7.0f, 4.0f, { 0.75f, 0.70f, 0.65f, 0.60f, 0.55f, 0.48f, 0.38f, 0.26f }, 2.0f, { 1.0f, 1.0f, 0.9f, 1.1f, 1.2f, 0.8f }, 0.25f },
        { "Chamber music hall", 22.0f, 15.0f, 9.0f, { 1.70f, 1.60f, 1.50f, 1.40f, 1.30f, 1.10f, 0.80f, 0.50f }, 4.0f, { 1.0f, 1.0f, 0.6f, 1.3f, 1.6f, 0.7f }, 0.35f },
        { "Concert hall", 40.0f, 25.0f, 17.0f, { 2.60f, 2.40f, 2.20f, 2.00f, 1.85f, 1.55f, 1.10f, 0.70f }, 6.0f, { 1.0f, 1.0f, 0.5f, 1.4f, 1.9f, 0.6f }, 0.45f },
        { "Stone church", 45.0f, 18.0f, 20.0f, { 5.00f, 4.60f, 4.20f, 3.80f, 3.20f, 2.50f, 1.70f, 1.00f }, 6.0f, { 0.9f, 0.9f, 1.0f, 1.0f, 1.1f, 1.1f }, 0.25f },
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
    const double W = room.width, D = room.depth, H = room.height;
    return 0.001 * (20.0 * W * D * H / (2.0 * (W * D + W * H + D * H)) + 12.0);
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

    // Source near the front of the room (moved sideways by sourceOffset), the listener further back, both at about
    // ear height. Both are a little off the room's centre line: dead centre, the two side walls' reflections arrive
    // together in pairs.
    const double lx = W * 0.41;
    const double sx = juce::jlimit (0.3, W - 0.3, lx + sourceOffset), sy = std::min (D * 0.25, 4.0), sz = std::min (1.5, H * 0.5);
    const double ly = std::min (sy + room.distance, D - 0.5), lz = std::min (1.6, H * 0.6);
    const double distance = std::sqrt ((sx - lx) * (sx - lx) + (ly - sy) * (ly - sy) + (lz - sz) * (lz - sz));

    // Angle between a sound from (x, y, z) and an ear's axis (ch 0 = left, along -x).
    auto angleFromEar = [&] (int ch, double x, double y, double z)
    {
        const double along = (ch == 0 ? lx - x : x - lx) / std::max (1.0e-6, std::sqrt ((x - lx) * (x - lx) + (y - ly) * (y - ly) + (z - lz) * (z - lz)));
        return std::acos (juce::jlimit (-1.0, 1.0, along));
    };

    // The specular reflections fade out over this time; the scattered sound takes over what they give up.
    const double mixTime = mixingTimeSeconds (room);
    const double fadeStart = mixTime, fadeEnd = 2.0 * mixTime;
    auto specularAmplitude = [&] (double t)  // 1 before the mixing time, 0 after twice it, raised cosine between.
    {
        if (t <= fadeStart) return 1.0;
        if (t >= fadeEnd) return 0.0;
        return 0.5 + 0.5 * std::cos (kPi * (t - fadeStart) / (fadeEnd - fadeStart));
    };

    //==========================================================================
    // Early reflections: mirror images of the source, band by band so each bounce can take its wall's absorption and
    // the head's shadow for its direction. Energy kept per bounce from Eyring's formula, so reflections die away at the
    // room's reverberation time; each wall's weight says how much more than average it absorbs (normalised over the
    // area, so the room's reverberation time stays the target).
    constexpr int numBands = (int) kBandCentres.size();
    const double wallArea[6] = { D * H, D * H, W * H, W * H, W * D, W * D };
    double weightedArea = 0.0;
    for (int w = 0; w < 6; ++w)
        weightedArea += wallArea[w] * room.wallAbsorption[(size_t) w];
    const double weightScale = surface / weightedArea;

    std::array<double, numBands> logEnergyPerBounce, logSpecularPerBounce, bandX;
    for (int b = 0; b < numBands; ++b)
    {
        const double hz = kBandCentres[(size_t) b];
        logEnergyPerBounce[(size_t) b] = -0.161 * volume / (surface * room.rt60[(size_t) b]);
        logSpecularPerBounce[(size_t) b] = std::log (1.0 - scatteringAt (room, hz));
        bandX[(size_t) b] = kPi * hz * kHeadRadius / kSpeedOfSound;
    }

    const double maxPath = distance + kSpeedOfSound * fadeEnd;
    const int nx = (int) std::ceil (maxPath / W) + 1, ny = (int) std::ceil (maxPath / D) + 1, nz = (int) std::ceil (maxPath / H) + 1;

    constexpr int fftMargin = 256;  // Samples after the last reflection, for the band filters to ring out into.
    const int earlyLength = std::min (length, (int) (fadeEnd * sampleRate) + 32 + fftMargin);
    std::vector<std::vector<float>> bands ((size_t) numBands, std::vector<float> ((size_t) earlyLength, 0.0f));
    double firstReflection = fadeEnd;

    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& band : bands)
            std::fill (band.begin(), band.end(), 0.0f);

        // The signal coming in already has the direct sound's delay for this ear (AudioEngine::placeAtEars), so each
        // reflection is timed relative to that.
        const double directEarDelay = earDelaySeconds (angleFromEar (ch, sx, sy, sz));

        for (int i = -nx; i <= nx; ++i)
            for (int j = -ny; j <= ny; ++j)
                for (int k = -nz; k <= nz; ++k)
                {
                    if (i == 0 && j == 0 && k == 0)
                        continue;

                    const double x = imageCoordinate (i, W, sx), y = imageCoordinate (j, D, sy), z = imageCoordinate (k, H, sz);
                    const double path = std::sqrt ((x - lx) * (x - lx) + (y - ly) * (y - ly) + (z - lz) * (z - lz));
                    const double angle = angleFromEar (ch, x, y, z);
                    const double t = (path - distance) / kSpeedOfSound + earDelaySeconds (angle) - directEarDelay;
                    if (t >= fadeEnd || t < 0.0)
                        continue;
                    firstReflection = std::min (firstReflection, t);

                    int bounces[6];
                    bouncesOnWalls (i, bounces[0], bounces[1]);
                    bouncesOnWalls (j, bounces[2], bounces[3]);
                    bouncesOnWalls (k, bounces[4], bounces[5]);
                    double weightedBounces = 0.0;
                    for (int w = 0; w < 6; ++w)
                        weightedBounces += bounces[w] * room.wallAbsorption[(size_t) w] * weightScale;
                    const int order = std::abs (i) + std::abs (j) + std::abs (k);

                    const double common = distance / path * specularAmplitude (t);
                    for (int b = 0; b < numBands; ++b)
                    {
                        const double energy = weightedBounces * logEnergyPerBounce[(size_t) b] + order * logSpecularPerBounce[(size_t) b];
                        addImpulse (bands[(size_t) b].data(), earlyLength, t * sampleRate,
                                    (float) (common * std::exp (0.5 * energy) * headShadowGain (angle, bandX[(size_t) b])));
                    }
                }

        // Bands overlap with complementary raised-cosine slopes an octave wide, so equal gains give back a plain impulse.
        int order = 1;
        while ((1 << order) < 2 * earlyLength)
            ++order;
        const int fftSize = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> sum ((size_t) fftSize * 2, 0.0f), spectrum ((size_t) fftSize * 2);

        for (int b = 0; b < numBands; ++b)
        {
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            std::copy (bands[(size_t) b].begin(), bands[(size_t) b].end(), spectrum.begin());
            fft.performRealOnlyForwardTransform (spectrum.data(), true);
            for (int bin = 0; bin <= fftSize / 2; ++bin)
            {
                const double hz = std::max (1.0, bin * sampleRate / fftSize);
                const double position = juce::jlimit (0.0, (double) numBands - 1.0, std::log2 (hz / kBandCentres.front()));
                const double away = std::abs (position - b);
                const auto mask = away >= 1.0 ? 0.0f : (float) juce::square (std::cos (0.5 * kPi * away));
                sum[(size_t) (2 * bin)] += spectrum[(size_t) (2 * bin)] * mask;
                sum[(size_t) (2 * bin + 1)] += spectrum[(size_t) (2 * bin + 1)] * mask;
            }
        }
        fft.performRealOnlyInverseTransform (sum.data());
        juce::FloatVectorOperations::copy (ir.getWritePointer (ch), sum.data(), earlyLength);
    }

    //==========================================================================
    // Scattered sound and diffuse tail. In a room of volume V, reflections arrive with an energy rate of 4*pi*c/V
    // relative to a direct sound of energy 1/r^2 (statistical room acoustics), decaying 60 dB per RT60. What each
    // bounce scatters leaves the mirror reflections above and joins this field, so it starts as a thin share of that
    // energy and grows into all of it. That fills the gaps between the few mirror images of a large room, and the
    // total stays the statistical level. White noise is shaped frame by frame in the frequency domain, with:
    constexpr int fftOrder = 10;
    constexpr int N = 1 << fftOrder, hop = N / 4;
    juce::dsp::FFT fft (fftOrder);

    std::vector<float> window ((size_t) N);
    for (int n = 0; n < N; ++n)
        window[(size_t) n] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * n / N));  // sqrt-Hann: analysis * synthesis = Hann.
    constexpr float olaGain = 1.0f / 2.0f;  // Sum of Hann windows at 75 % overlap is 2.

    const double energyRate = 4.0 * kPi * kSpeedOfSound / volume * distance * distance;  // Per second, with direct = 1.
    const double noiseScale = std::sqrt (energyRate / sampleRate);
    const double bouncesPerSecond = kSpeedOfSound * surface / (4.0 * volume);  // Mean free path 4V/S.
    const double travelTime = distance / kSpeedOfSound;  // The sound has already been going this long at t = 0.

    std::vector<double> decayRate ((size_t) N / 2 + 1), spectralShape ((size_t) N / 2 + 1), logSpecular ((size_t) N / 2 + 1);
    std::vector<float> earMix ((size_t) N / 2 + 1), earKeep ((size_t) N / 2 + 1);
    for (int b = 0; b <= N / 2; ++b)
    {
        const double hz = (double) b * sampleRate / N;
        decayRate[(size_t) b] = 6.9078 / rt60At (room, (float) std::max (hz, 20.0));  // Amplitude: exp(-6.9 t / RT60) = -60 dB at RT60.
        const double lowCut = 1.0 / std::sqrt (1.0 + std::pow (60.0 / std::max (hz, 1.0), 4.0));      // Below ~60 Hz.
        const double highCut = 1.0 / std::sqrt (1.0 + std::pow (hz / 18000.0, 8.0));               // Above ~18 kHz.
        spectralShape[(size_t) b] = lowCut * highCut;
        logSpecular[(size_t) b] = std::log (1.0 - scatteringAt (room, hz));

        // Two independent noises mixed as cos/sin of half the arcsine of the correlation: they then have that correlation.
        const double angle = 0.5 * std::asin (juce::jlimit (-1.0, 1.0, diffuseEarCorrelation (hz)));
        earKeep[(size_t) b] = (float) std::cos (angle);
        earMix[(size_t) b] = (float) std::sin (angle);
    }

    // Early on, scattered arrivals are separate events, like the reflections; they only become continuous noise later.
    // Sparse noise of the same power, with a density of ~20 times the mirror images' (each bounce scatters into many paths).
    std::mt19937 rng (seed);
    std::normal_distribution<float> gauss (0.0f, 1.0f);
    std::uniform_real_distribution<float> uniform (0.0f, 1.0f);
    std::array<std::vector<float>, 2> noise, tail;
    for (int ch = 0; ch < 2; ++ch)
    {
        noise[(size_t) ch].resize ((size_t) (length + N));
        tail[(size_t) ch].assign ((size_t) (length + N), 0.0f);
        for (int n = 0; n < length + N; ++n)
        {
            const double t = n / sampleRate + travelTime;
            const auto probability = (float) juce::jlimit (0.002, 1.0, 20.0 * 4.0 * kPi * std::pow (kSpeedOfSound, 3.0) * t * t / volume / sampleRate);
            const float v = gauss (rng);
            noise[(size_t) ch][(size_t) n] = probability >= 1.0f ? v : (uniform (rng) < probability ? v / std::sqrt (probability) : 0.0f);
        }
    }

    std::array<std::vector<float>, 2> frame { std::vector<float> ((size_t) N * 2), std::vector<float> ((size_t) N * 2) };

    for (int start = -N; start < length; start += hop)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = frame[(size_t) ch];
            std::fill (f.begin(), f.end(), 0.0f);
            for (int n = 0; n < N; ++n)
                f[(size_t) n] = start + n >= 0 ? noise[(size_t) ch][(size_t) (start + n)] * window[(size_t) n] : 0.0f;
            fft.performRealOnlyForwardTransform (f.data(), true);
        }

        const double t = std::max (0.0, (start + N / 2) / sampleRate);
        const double specularLeft = juce::square (specularAmplitude (t));
        const double bounces = (t + travelTime) * bouncesPerSecond;
        for (int b = 0; b <= N / 2; ++b)
        {
            // What the mirror reflections no longer carry at this time and frequency.
            const double scattered = std::sqrt (std::max (0.0, 1.0 - specularLeft * std::exp (logSpecular[(size_t) b] * bounces)));
            const auto gain = (float) (spectralShape[(size_t) b] * std::exp (-decayRate[(size_t) b] * t) * scattered);
            for (int part = 0; part < 2; ++part)
            {
                const size_t index = (size_t) (2 * b + part);
                const float l = frame[0][index], r = frame[1][index];
                frame[0][index] = gain * (earKeep[(size_t) b] * l + earMix[(size_t) b] * r);
                frame[1][index] = gain * (earKeep[(size_t) b] * r + earMix[(size_t) b] * l);
            }
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& f = frame[(size_t) ch];
            fft.performRealOnlyInverseTransform (f.data());
            for (int n = 0; n < N; ++n)
                if (start + n >= 0 && start + n < length)
                    tail[(size_t) ch][(size_t) (start + n)] += f[(size_t) n] * window[(size_t) n] * olaGain;
        }
    }

    const int gateStart = (int) (firstReflection * sampleRate), gateLength = std::max (1, (int) (0.001 * sampleRate));  // Nothing can arrive before the first bounce.
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* out = ir.getWritePointer (ch);
        for (int n = 0; n < length; ++n)
            out[n] += tail[(size_t) ch][(size_t) n] * (float) noiseScale * juce::jlimit (0.0f, 1.0f, (float) (n - gateStart) / (float) gateLength);

        // Short fade at the very end.
        const int fade = std::min (length, (int) (0.05 * sampleRate));
        for (int n = 0; n < fade; ++n)
            out[length - 1 - n] *= (float) n / (float) fade;

        // The signal coming in is already quieter at the ear away from the source (head shadow of the direct sound,
        // 6 dB at the side). The room's sound comes from everywhere and isn't, so that is undone here.
        const double side = std::abs (std::atan2 (sx - lx, ly - sy));
        if ((ch == 0) == (sx > lx))
            ir.applyGain (ch, 0, length, (float) std::pow (10.0, 6.0 * std::sin (side) / 20.0));
    }

    return ir;
}

juce::AudioBuffer<float> removeDirectSound (const juce::AudioBuffer<float>& measured, double sampleRate)
{
    const int numChannels = measured.getNumChannels(), numSamples = measured.getNumSamples();
    const int before = (int) (0.0005 * sampleRate), after = (int) (0.0025 * sampleRate), fade = std::max (1, (int) (0.001 * sampleRate));

    // The direct sound in each channel: the highest peak within 1 ms of the first time it gets to half the channel's maximum.
    std::vector<int> peak ((size_t) numChannels, 0);
    int earliest = numSamples;
    double directEnergy = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* x = measured.getReadPointer (ch);
        float largest = 0.0f;
        for (int n = 0; n < numSamples; ++n)
            largest = std::max (largest, std::abs (x[n]));
        if (largest <= 0.0f)
            continue;

        int first = 0;
        while (std::abs (x[first]) < 0.5f * largest)
            ++first;
        int best = first;
        for (int n = first; n < std::min (numSamples, first + (int) (0.001 * sampleRate)); ++n)
            if (std::abs (x[n]) > std::abs (x[best]))
                best = n;
        peak[(size_t) ch] = best;
        earliest = std::min (earliest, best);

        double energy = 0.0;
        for (int n = std::max (0, best - before); n < std::min (numSamples, best + after); ++n)
            energy += (double) x[n] * x[n];
        directEnergy = std::max (directEnergy, energy);
    }
    if (directEnergy <= 0.0)
        return measured;  // Silent file.

    const int start = std::max (0, earliest - before);
    juce::AudioBuffer<float> wet (numChannels, numSamples - start);
    double wetEnergy = 0.0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* out = wet.getWritePointer (ch);
        const auto* x = measured.getReadPointer (ch);
        const int fadeFrom = peak[(size_t) ch] + after - start;  // In `out`; silence before, then fading in.
        for (int n = 0; n < wet.getNumSamples(); ++n)
        {
            out[n] = x[n + start] * juce::jlimit (0.0f, 1.0f, (float) (n - fadeFrom) / (float) fade);
            wetEnergy += (double) out[n] * out[n];
        }
    }
    wetEnergy /= numChannels;

    // Direct sound = 1, as for the generated rooms. A far-away microphone can give a lot of reverb, but past +12 dB the
    // peak found was probably not the direct sound.
    wet.applyGain ((float) std::sqrt (std::min (1.0 / directEnergy, juce::Decibels::decibelsToGain (12.0) / wetEnergy)));
    return wet;
}

} // namespace reverb
