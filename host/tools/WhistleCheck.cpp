// Checks the whistle acoustics and model without the app.
//
//   WhistleCheck tune             tune every preset to D (D5, or D4 for low D) and print the hole positions
//   WhistleCheck table            predicted resonances for the scale fingerings of every preset
//   WhistleCheck play [preset]    run the time-domain model at fixed fingerings and pressures, measure what it plays
//   WhistleCheck slide [hole] [Pa]  open one hole gradually (half-holing) and measure the pitch

#include "Whistle/Acoustics.h"
#include "Whistle/WhistleModel.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace whistle;

namespace
{
std::string noteName (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return names[(midi % 12 + 12) % 12] + std::to_string (midi / 12 - 1);
}

double centsFromNearest (double hz)
{
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    return (midi - std::round (midi)) * 100.0;
}

double lowestNote (const Geometry& g) { return g.boreLength > 0.4 ? 293.66 : 587.33; }

void printTable (const Geometry& g)
{
    std::printf ("\n%s\n  fingering   register 1          register 2        2/1 ratio (cents from octave)\n", g.name.toRawUTF8());
    for (int closed = kNumHoles; closed >= 0; --closed)
    {
        const auto r = resonances (g, scaleFingering (closed), lowestNote (g) * 0.6, lowestNote (g) * 6.0);
        char pattern[kNumHoles + 1] {};
        for (int i = 0; i < kNumHoles; ++i)
            pattern[i] = i < closed ? 'x' : 'o';
        if (r.size() < 2)
        {
            std::printf ("  %s      (fewer than 2 resonances)\n", pattern);
            continue;
        }
        std::printf ("  %s      %7.1f Hz %-4s %+5.0f   %7.1f Hz %-4s   %+5.0f\n", pattern, r[0], noteName (r[0]).c_str(), centsFromNearest (r[0]),
                     r[1], noteName (r[1]).c_str(), 1200.0 * std::log2 (r[1] / r[0]) - 1200.0);
    }
}
} // namespace

// Fundamental frequency of the last part of a signal (normalised autocorrelation), 0 if not periodic.
static double measurePitch (const std::vector<float>& x, double rate, double& clarity)
{
    const size_t window = (size_t) (0.1 * rate);
    const size_t start = x.size() - window - (size_t) (rate / 100.0);
    const int minLag = (int) (rate / 3000.0), maxLag = (int) (rate / 120.0);
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    double energy = 0.0;
    for (size_t i = 0; i < window; ++i)
        energy += (double) x[start + i] * x[start + i];
    if (energy <= 1e-20)
    {
        clarity = 0.0;
        return 0.0;
    }
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double sum = 0.0, e2 = 0.0;
        for (size_t i = 0; i < window; ++i)
        {
            sum += (double) x[start + i] * x[start + i + (size_t) lag];
            e2 += (double) x[start + i + (size_t) lag] * x[start + i + (size_t) lag];
        }
        r[(size_t) lag] = sum / std::sqrt (energy * e2 + 1e-30);
    }
    double best = 0.0;
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        best = std::max (best, r[(size_t) lag]);
    // First peak that is nearly as high as the best one (avoids picking a multiple of the period).
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
        {
            const double denom = r[(size_t) lag - 1] - 2.0 * r[(size_t) lag] + r[(size_t) lag + 1];
            const double offset = denom < 0.0 ? 0.5 * (r[(size_t) lag - 1] - r[(size_t) lag + 1]) / denom : 0.0;
            clarity = r[(size_t) lag];
            return rate / (lag + offset);
        }
    }
    clarity = 0.0;
    return 0.0;
}

static float strouhal = 0.02f, jetQ = 0.7f, convection = 0.6f;
static int stages = 2;

static void play (const Geometry& g)
{
    constexpr double rate = 48000.0;
    const double pressures[] = { 100, 200, 400, 700, 1000, 1500, 2200 };
    std::printf ("\n%s: pitch played by the model (cents from the predicted register 1 / 2), level dBFS\n", g.name.toRawUTF8());
    std::printf ("  fingering   predicted        ");
    for (double p : pressures)
        std::printf ("%7.0f Pa      ", p);
    std::printf ("\n");

    for (int closed = kNumHoles; closed >= 0; --closed)
    {
        const auto predicted = resonances (g, scaleFingering (closed), lowestNote (g) * 0.6, lowestNote (g) * 6.0);
        char pattern[kNumHoles + 1] {};
        for (int i = 0; i < kNumHoles; ++i)
            pattern[i] = i < closed ? 'x' : 'o';
        std::printf ("  %s      %6.0f/%6.0f  ", pattern, predicted.size() > 0 ? predicted[0] : 0.0, predicted.size() > 1 ? predicted[1] : 0.0);

        for (double pressure : pressures)
        {
            WhistleModel model;
            model.setGeometry (g);
            model.prepare (rate);
            model.jetStrouhal = strouhal;
            model.jetQ = jetQ;
            model.jetConvection = convection;
            model.jetStages = stages;
            Controls controls;
            const auto open = scaleFingering (closed);
            for (int i = 0; i < kNumHoles; ++i)
                controls.openness[(size_t) i] = (float) open[(size_t) i];
            // Blow up to the pressure over 40 ms, like a player, then hold.
            std::vector<float> out ((size_t) (rate * 0.6), 0.0f);
            const int block = (int) (rate * 0.002);
            for (int start = 0; start < (int) out.size(); start += block)
            {
                controls.mouthPressure = (float) (pressure * std::min (1.0, start / (rate * 0.04)));
                model.setControls (controls);
                model.render (out.data() + start, std::min (block, (int) out.size() - start));
            }

            double clarity = 0.0;
            const double f = measurePitch (out, rate, clarity);
            double rms = 0.0;
            for (size_t i = out.size() / 2; i < out.size(); ++i)
                rms += (double) out[i] * out[i];
            rms = std::sqrt (rms / (double) (out.size() / 2));
            const double db = 20.0 * std::log10 (rms + 1e-12);

            if (f <= 0.0 || clarity < 0.8 || predicted.size() < 2)
                std::printf ("    -  %5.0f     ", db);
            else
            {
                const int reg = std::abs (std::log2 (f / predicted[1])) < std::abs (std::log2 (f / predicted[0])) ? 2 : 1;
                std::printf ("%2d:%+5.0f %4.0f    ", reg, 1200.0 * std::log2 (f / predicted[(size_t) reg - 1]), db);
            }
        }
        std::printf ("\n");
    }
}

// Slide one hole from covered to open (the fingering above it covered, below it open) at a fixed pressure.
static void slide (const Geometry& g, int hole, double pressure)
{
    constexpr double rate = 48000.0;
    std::printf ("\n%s: hole %d from covered to open at %.0f Pa\n  openness   pitch\n", g.name.toRawUTF8(), hole + 1, pressure);
    for (int step = 0; step <= 10; ++step)
    {
        WhistleModel model;
        model.setGeometry (g);
        model.prepare (rate);
        Controls controls;
        for (int i = 0; i < kNumHoles; ++i)
            controls.openness[(size_t) i] = i < hole ? 0.0f : 1.0f;
        controls.openness[(size_t) hole] = step / 10.0f;

        std::vector<float> out ((size_t) (rate * 0.6), 0.0f);
        const int block = (int) (rate * 0.002);
        for (int start = 0; start < (int) out.size(); start += block)
        {
            controls.mouthPressure = (float) (pressure * std::min (1.0, start / (rate * 0.04)));
            model.setControls (controls);
            model.render (out.data() + start, std::min (block, (int) out.size() - start));
        }
        double clarity = 0.0;
        const double f = measurePitch (out, rate, clarity);
        std::printf ("  %4.1f       %s\n", step / 10.0, f > 0.0 && clarity > 0.8 ? (juce::String (f, 1) + " Hz  " + noteName (f) + juce::String (juce::roundToInt (centsFromNearest (f))).paddedLeft (' ', 5)).toRawUTF8() : "-");
    }
}

int main (int argc, char* argv[])
{
    const juce::String mode = argc > 1 ? argv[1] : "table";

    if (mode == "slide")  // slide <hole 1-6> <pressure>
    {
        slide (presets().front(), (argc > 2 ? juce::String (argv[2]).getIntValue() : 3) - 1, argc > 3 ? juce::String (argv[3]).getDoubleValue() : 700.0);
        return 0;
    }

    if (mode == "play")
    {
        const int index = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
        for (int i = 3; i < argc; ++i)  // st=, q=, conv=
        {
            const juce::String arg (argv[i]);
            const float value = arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            if (arg.startsWith ("st=")) strouhal = value;
            if (arg.startsWith ("q=")) jetQ = value;
            if (arg.startsWith ("conv=")) convection = value;
            if (arg.startsWith ("stages=")) stages = (int) value;
        }
        std::printf ("jet: Strouhal %.3f, Q %.2f, convection %.2f, stages %d\n", strouhal, jetQ, convection, stages);
        play (presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, index)]);
        return 0;
    }

    for (auto g : presets())
    {
        if (mode == "tune")
        {
            const bool ok = tuneToScale (g, lowestNote (g));
            std::printf ("%s: %s\n  bore %.4f  holes", g.name.toRawUTF8(), ok ? "tuned" : "DID NOT CONVERGE", g.boreLength);
            for (const auto& h : g.holes)
                std::printf ("  %.4f", h.position);
            std::printf ("\n");
        }
        printTable (g);
    }
    return 0;
}
