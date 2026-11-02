// Checks the generated room impulse responses objectively and renders them for listening.
//
// For each room: reverberation time per octave band (T30 from Schroeder backward integration) against the
// target, and direct-to-reverberant ratio against statistical room acoustics. Writes to <output dir>:
//   ir_<room>.wav       the impulse response
//   phrase_<room>.wav   the test phrase, dry + reverb at the app's default level
//
//   ReverbCheck [output dir]

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "ImpulseResponse.h"
#include "TestPhrase.h"

#include <cmath>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr float kDefaultWetDb = -6.0f;  // As RoomReverb::wetDb.

// Reverberation time from the -5 to -35 dB part of the Schroeder decay curve, extrapolated to 60 dB.
double measureT30 (const float* ir, int length, float centreHz)
{
    std::vector<float> band (ir, ir + length);
    for (int pass = 0; pass < 2; ++pass)  // 4th order band-pass, about an octave wide.
    {
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (kSampleRate, centreHz, 1.414f);
        juce::dsp::IIR::Filter<float> filter (coefficients);
        for (auto& s : band)
            s = filter.processSample (s);
    }

    std::vector<double> decay ((size_t) length);
    double sum = 0.0;
    for (int n = length - 1; n >= 0; --n)
    {
        sum += (double) band[(size_t) n] * band[(size_t) n];
        decay[(size_t) n] = sum;
    }
    if (sum <= 0.0)
        return 0.0;

    // Least-squares line through the decay curve between -5 and -35 dB.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (int n = 0; n < length; ++n)
    {
        const double db = 10.0 * std::log10 (decay[(size_t) n] / decay[0] + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = n / kSampleRate;
            sx += t; sy += db; sxx += t * t; sxy += t * db;
            ++count;
        }
    }
    if (count < 10)
        return 0.0;
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);  // dB per second (negative)
    return -60.0 / slope;
}

// Plain FFT convolution of a mono signal with one IR channel.
std::vector<float> convolve (const std::vector<float>& signal, const float* ir, int irLength)
{
    const size_t outLength = signal.size() + (size_t) irLength - 1;
    int order = 1;
    while ((size_t) (1 << order) < outLength)
        ++order;
    const size_t N = (size_t) 1 << order;

    juce::dsp::FFT fft (order);
    std::vector<float> a (N * 2, 0.0f), b (N * 2, 0.0f);
    std::copy (signal.begin(), signal.end(), a.begin());
    std::copy (ir, ir + irLength, b.begin());
    fft.performRealOnlyForwardTransform (a.data());
    fft.performRealOnlyForwardTransform (b.data());
    for (size_t k = 0; k <= N / 2; ++k)
    {
        const float re = a[2 * k] * b[2 * k] - a[2 * k + 1] * b[2 * k + 1];
        const float im = a[2 * k] * b[2 * k + 1] + a[2 * k + 1] * b[2 * k];
        a[2 * k] = re;
        a[2 * k + 1] = im;
    }
    fft.performRealOnlyInverseTransform (a.data());
    a.resize (outLength);
    return a;
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer)
{
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (kSampleRate).withNumChannels (2).withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}
} // namespace

int main (int argc, char* argv[])
{
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    // Test phrase (mono), rendered once.
    TestPhrase phrase;
    phrase.prepare (kSampleRate);
    phrase.start();
    std::vector<float> dry ((size_t) (kSampleRate * 6.0), 0.0f);
    phrase.render (dry.data(), (int) dry.size());

    // The IR generator relies on an FFT forward + inverse round trip giving back the input unchanged.
    {
        juce::dsp::FFT fft (10);
        juce::Random random (1);
        std::vector<float> x (1024), f (2048);
        double in = 0.0, out = 0.0;
        for (auto& v : x)
            v = random.nextFloat() * 2.0f - 1.0f;
        std::fill (f.begin(), f.end(), 0.0f);
        std::copy (x.begin(), x.end(), f.begin());
        fft.performRealOnlyForwardTransform (f.data(), true);
        fft.performRealOnlyInverseTransform (f.data());
        for (size_t i = 0; i < x.size(); ++i)
        {
            in += (double) x[i] * x[i];
            out += (double) f[i] * f[i];
        }
        std::printf ("FFT round trip energy gain %.4f (should be 1)\n", out / in);
    }

    bool ok = true;
    for (const auto& room : reverb::presets())
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto ir = reverb::generate (room, kSampleRate);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;

        std::printf ("\n%s  (%.0f x %.0f x %.0f m, listener at %.1f m, IR %.1f s, generated in %.0f ms)\n",
                     room.name.toRawUTF8(), room.width, room.depth, room.height, room.distance,
                     ir.getNumSamples() / kSampleRate, ms);

        std::printf ("  band     target   measured L/R\n");
        for (float hz : { 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f })
        {
            const double target = reverb::rt60At (room, hz);
            const double left = measureT30 (ir.getReadPointer (0), ir.getNumSamples(), hz);
            const double right = measureT30 (ir.getReadPointer (1), ir.getNumSamples(), hz);
            const bool good = std::abs (left - target) < 0.15 * target && std::abs (right - target) < 0.15 * target;
            ok &= good;
            std::printf ("  %5.0f Hz  %5.2f s   %5.2f / %5.2f s %s\n", hz, target, left, right, good ? "" : "  <-- off by > 15 %");
        }

        // Direct-to-reverberant ratio in the 1 kHz octave band (the theory is per frequency; highs carry less
        // reverberant energy because they decay faster). The direct sound is a unit impulse by construction.
        auto bandEnergy = [] (const float* signal, int length)
        {
            std::vector<float> band (signal, signal + length);
            for (int pass = 0; pass < 2; ++pass)
            {
                juce::dsp::IIR::Filter<float> filter (juce::dsp::IIR::Coefficients<float>::makeBandPass (kSampleRate, 1000.0f, 1.414f));
                for (auto& v : band)
                    v = filter.processSample (v);
            }
            double e = 0.0;
            for (auto v : band)
                e += (double) v * v;
            return e;
        };
        std::vector<float> impulse ((size_t) kSampleRate, 0.0f);
        impulse[0] = 1.0f;
        const double directEnergy = bandEnergy (impulse.data(), (int) impulse.size());
        const double reverbEnergy = 0.5 * (bandEnergy (ir.getReadPointer (0), ir.getNumSamples()) + bandEnergy (ir.getReadPointer (1), ir.getNumSamples()));
        const double volume = (double) room.width * room.depth * room.height;
        const double criticalDistance = 0.057 * std::sqrt (volume / reverb::rt60At (room, 1000.0f));
        const double theory = 20.0 * std::log10 (criticalDistance / room.distance);
        const double measured = 10.0 * std::log10 (directEnergy / reverbEnergy);
        const bool drGood = std::abs (measured - theory) < 2.0;
        ok &= drGood;
        std::printf ("  direct/reverberant at 1 kHz  %5.1f dB  (theory %5.1f dB, critical distance %.1f m)%s\n", measured, theory,
                     criticalDistance, drGood ? "" : "  <-- off by > 2 dB");

        // Interaural correlation of the tail (lower = wider).
        double lr = 0, ll = 0, rr = 0;
        for (int n = (int) (0.1 * kSampleRate); n < ir.getNumSamples(); ++n)
        {
            const double l = ir.getSample (0, n), r = ir.getSample (1, n);
            lr += l * r; ll += l * l; rr += r * r;
        }
        std::printf ("  left/right correlation of tail  %.2f\n", lr / std::sqrt (ll * rr + 1e-30));

        // Files.
        const auto name = room.name.replaceCharacter (' ', '_').toLowerCase();
        writeWav (outDir.getChildFile ("ir_" + name + ".wav"), ir);

        const float wet = juce::Decibels::decibelsToGain (kDefaultWetDb);
        const auto left = convolve (dry, ir.getReadPointer (0), ir.getNumSamples());
        const auto right = convolve (dry, ir.getReadPointer (1), ir.getNumSamples());
        juce::AudioBuffer<float> mix (2, (int) left.size());
        float peak = 0.0f;
        for (int n = 0; n < mix.getNumSamples(); ++n)
        {
            const float d = n < (int) dry.size() ? dry[(size_t) n] : 0.0f;
            mix.setSample (0, n, d + wet * left[(size_t) n]);
            mix.setSample (1, n, d + wet * right[(size_t) n]);
            peak = std::max ({ peak, std::abs (mix.getSample (0, n)), std::abs (mix.getSample (1, n)) });
        }
        if (peak > 0.99f)
            mix.applyGain (0.99f / peak);
        writeWav (outDir.getChildFile ("phrase_" + name + ".wav"), mix);
    }

    std::printf ("\n%s\nFiles in %s\n", ok ? "All rooms within tolerance" : "SOME ROOMS OUT OF TOLERANCE", outDir.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
