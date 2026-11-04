// Checks the whistle acoustics and model without the app.
//
//   WhistleCheck tune             tune every preset to D (D5, or D4 for low D) and print the hole positions
//   WhistleCheck table            predicted resonances for the scale fingerings of every preset
//   WhistleCheck play [preset]    run the time-domain model at fixed fingerings and pressures, measure what it plays
//   WhistleCheck slide [hole] [Pa]  open one hole (1 = L1 ... 6 = R3) gradually (half-holing) and measure the pitch
//   WhistleCheck chart [preset]   every fingering of a standard whistle chart: predicted and played tuning
//   WhistleCheck head [preset]    how a narrower head section changes the second register's tuning
//   WhistleCheck scale [preset] [file.wav]  the app's test scale: plan, play, measure each note (and save it)
//   WhistleCheck noise [preset] [noise=] [direct=] [wav=]  breath noise vs. tone, airy notes below the threshold
//   WhistleCheck modes [preset]   which register (resonance) each fingering plays as the pressure rises to 8.5 kPa

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "Whistle/Acoustics.h"
#include "Whistle/JetStability.h"
#include "Whistle/PitchDetect.h"
#include "Whistle/ScalePlan.h"
#include "Whistle/WhistleModel.h"

#include <cmath>
#include <functional>
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

// The presets' lowest notes: recorders C5, whistles D5 (low whistles D4).
double lowestNote (const Geometry& g)
{
    if (g.chart == FingeringChart::baroqueRecorder)
        return 523.25;
    return g.boreLength > 0.4 ? 293.66 : 587.33;
}

// The chart's low-register fingerings.
std::vector<ChartFingering> lowRegister (const Geometry& g)
{
    std::vector<ChartFingering> out;
    for (const auto& c : chartFor (g.chart))
        if (c.reg == 1)
            out.push_back (c);
    return out;
}

void printTable (const Geometry& g)
{
    std::printf ("\n%s\n  fingering   register 1          register 2        2/1 ratio (cents from octave)\n", g.name.toRawUTF8());
    for (const auto& c : lowRegister (g))
    {
        const auto r = resonances (g, c.openness (g), lowestNote (g) * 0.6, lowestNote (g) * 6.0);
        if (r.size() < 2)
        {
            std::printf ("  %s    (fewer than 2 resonances)\n", c.holePattern (g).c_str());
            continue;
        }
        std::printf ("  %s    %7.1f Hz %-4s %+5.0f   %7.1f Hz %-4s   %+5.0f\n", c.holePattern (g).c_str(), r[0], noteName (r[0]).c_str(), centsFromNearest (r[0]),
                     r[1], noteName (r[1]).c_str(), 1200.0 * std::log2 (r[1] / r[0]) - 1200.0);
    }
}

Controls controlsFor (const Openness& open)
{
    Controls controls;
    for (int i = 0; i < kNumHoles; ++i)
        controls.openness[(size_t) i] = (float) open.holes[(size_t) i];
    controls.footOpenness = (float) open.foot;
    return controls;
}
} // namespace

// Plays a breath gesture through one fingering: the pressure is sent every 2 ms, like WARBL's frames.
// Returns the sound at `rate`.
static std::vector<float> playGesture (const Geometry& g, const Voicing& v, const Openness& open,
                                       const std::function<double (double)>& pressureAt, double seconds, double rate)
{
    WhistleModel model;
    model.setGeometry (g);
    model.prepare (rate);
    model.setVoicing (v);
    auto controls = controlsFor (open);
    std::vector<float> out ((size_t) (seconds * rate), 0.0f);
    const int block = (int) (rate * 0.002);
    for (int start = 0; start < (int) out.size(); start += block)
    {
        controls.mouthPressure = (float) pressureAt (start / rate);
        model.setControls (controls);
        model.render (out.data() + start, std::min (block, (int) out.size() - start));
    }
    return out;
}

// RMS level (dBFS) of out[from, to).
static double levelDb (const std::vector<float>& out, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double) out[i] * out[i];
    return 10.0 * std::log10 (sum / (double) std::max<size_t> (1, to - from) + 1.0e-20);
}

// wav=<prefix>: saves out as <prefix>-<name>.wav (spaces in name become underscores).
static void saveWavArg (int argc, char* argv[], const juce::String& name, const std::vector<float>& out, double rate)
{
    for (int i = 3; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (! arg.startsWith ("wav="))
            continue;
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false) + "-"
                                                                                  + name.replaceCharacter (' ', '_') + ".wav");
        juce::AudioBuffer<float> buffer (1, (int) out.size());
        buffer.copyFrom (0, 0, out.data(), (int) out.size());
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
}

// A preset by number or by part of its name (e.g. "recorder", "narrow head").
static int presetArg (const char* arg)
{
    const juce::String text (arg);
    if (text.containsOnly ("0123456789"))
        return juce::jlimit (0, (int) presets().size() - 1, text.getIntValue());
    for (size_t i = 0; i < presets().size(); ++i)
        if (presets()[i].name.containsIgnoreCase (text))
            return (int) i;
    std::printf ("No preset matches \"%s\"\n", arg);
    std::exit (1);
}

using Complex = std::complex<double>;
static float strouhal = 0.02f, jetQ = 0.7f, convection = 0.6f;
static int stages = 2;
static float windwayMass = 1.0f, windwayFriction = 1.0f;

// noise=<0-1>: breath noise.
static void windwayNoiseArg (int argc, char* argv[], Voicing& v)
{
    for (int i = 3; i < argc; ++i)
        if (juce::String (argv[i]).startsWith ("noise="))
            v.breathNoise = juce::String (argv[i]).fromFirstOccurrenceOf ("=", false, false).getFloatValue();
}

// mass=<0-1> friction=<0-1> windway=<length mm>: try the windway's physics.
static void windwayArgs (int argc, char* argv[], int from, Geometry& g)
{
    for (int i = from; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const double value = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        if (arg.startsWith ("mass=")) windwayMass = (float) value;
        if (arg.startsWith ("friction=")) windwayFriction = (float) value;
        if (arg.startsWith ("windway=")) g.windwayLength = value * 0.001;
    }
}

static void play (const Geometry& g)
{
    constexpr double rate = 48000.0;
    const double pressures[] = { 100, 200, 400, 700, 1000, 1500, 2200 };
    std::printf ("\n%s: pitch played by the model (cents from the predicted register 1 / 2), level dBFS\n", g.name.toRawUTF8());
    std::printf ("  fingering   predicted        ");
    for (double p : pressures)
        std::printf ("%7.0f Pa      ", p);
    std::printf ("\n");

    for (const auto& c : lowRegister (g))
    {
        const auto predicted = resonances (g, c.openness (g), lowestNote (g) * 0.6, lowestNote (g) * 6.0);
        std::printf ("  %s    %6.0f/%6.0f  ", c.holePattern (g).c_str(), predicted.size() > 0 ? predicted[0] : 0.0, predicted.size() > 1 ? predicted[1] : 0.0);

        for (double pressure : pressures)
        {
            WhistleModel model;
            model.setGeometry (g);
            model.prepare (rate);
            model.jetStrouhal = strouhal;
            model.jetQ = jetQ;
            model.jetConvection = convection;
            model.jetStages = stages;
            model.windwayMass = windwayMass;
            model.windwayFriction = windwayFriction;
            auto controls = controlsFor (c.openness (g));
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

// Slide one hole from covered to open (the front holes above it covered, below it open) at a fixed pressure.
static void slide (const Geometry& g, int hole, double pressure)
{
    constexpr double rate = 48000.0;
    std::printf ("\n%s: hole %s from covered to open at %.0f Pa\n  openness   pitch\n", g.name.toRawUTF8(), kHoleNames[(size_t) hole], pressure);
    for (int step = 0; step <= 10; ++step)
    {
        WhistleModel model;
        model.setGeometry (g);
        model.prepare (rate);
        Controls controls;
        for (int i = 0; i < kNumHoles; ++i)
            controls.openness[(size_t) i] = i < hole ? 0.0f : 1.0f;  // Includes the thumb (index 0) covered.
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

// Run the model at one fingering and pressure; returns the played frequency (0 if no clear pitch).
static double playOnce (const Geometry& g, const Openness& open, double pressure)
{
    constexpr double rate = 48000.0;
    WhistleModel model;
    model.setGeometry (g);
    model.prepare (rate);
    model.jetStrouhal = strouhal;
    model.jetQ = jetQ;
    model.jetConvection = convection;
    model.jetStages = stages;
    model.windwayMass = windwayMass;
    model.windwayFriction = windwayFriction;

    auto controls = controlsFor (open);

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
    return clarity > 0.8 ? f : 0.0;
}

// For each chart fingering: the resonance the geometry predicts in that note's register, and the pressures at
// which the model plays that note, with its pitch at the lowest and highest of those pressures.
static void chart (const Geometry& g)
{
    const double lowest = lowestNote (g);
    std::printf ("\n%s\n  note      fingering  predicted (geometry)   model plays it at    pitch at lowest / highest\n", g.name.toRawUTF8());

    for (const auto& c : chartFor (g.chart))
    {
        const auto open = c.openness (g);
        const double target = lowest * std::pow (2.0, c.semitones / 12.0);
        const double cents = predictedCents (g, c, lowest);

        std::vector<std::pair<double, double>> hits;  // Pressure, cents from target
        for (double pressure = 200.0; pressure <= 5000.0; pressure += 100.0)
        {
            const double f = playOnce (g, open, pressure);
            if (f > 0.0 && std::abs (1200.0 * std::log2 (f / target)) < 150.0)
                hits.push_back ({ pressure, 1200.0 * std::log2 (f / target) });
        }

        std::printf ("  %-8s  %s     %+5.0f c               ", c.name, c.holePattern (g).c_str(), cents);
        if (hits.empty())
            std::printf ("never\n");
        else
            std::printf ("%4.0f-%4.0f Pa        %+5.0f / %+5.0f c\n", hits.front().first, hits.back().first, hits.front().second, hits.back().second);
    }
}

// Effect of a narrower head section on tuning: for each head shape the whistle is retuned (holes and bore),
// then the second register's octave and the vented high D are reported per note.
static void head (const Geometry& base)
{
    const double lowest = lowestNote (base);
    std::printf ("\n%s: head section effect (whistle retuned for each), second register vs. octave in cents\n", base.name.toRawUTF8());
    std::printf ("  head            ");
    for (const auto& c : chartFor (base.chart))
        if (c.reg == 2)
            std::printf ("%-9s", c.name + 5);
    std::printf ("  bore length\n");

    const double diameters[] = { 0.012, 0.0105, 0.009, 0.0075 };
    const double lengths[] = { 0.03, 0.06, 0.10 };
    for (double length : lengths)
    {
        for (double diameter : diameters)
        {
            auto g = base;
            g.headLength = diameter < base.boreDiameterTop ? length : 0.0;
            g.headDiameter = diameter;
            if (diameter >= base.boreDiameterTop && length > lengths[0])
                continue;  // No head: show once.
            const bool ok = tuneToScale (g, lowest, true);
            if (g.headLength == 0.0)
                std::printf ("  none            ");
            else
                std::printf ("  %3.0f mm, %4.1f mm", length * 1000.0, diameter * 1000.0);
            for (const auto& c : chartFor (g.chart))
                if (c.reg == 2)
                    std::printf ("%+5.0f    ", predictedCents (g, c, lowest));
            std::printf ("  %.1f mm%s\n", g.boreLength * 1000.0, ok ? "" : "  (tuning didn't converge)");
        }
    }
}

int main (int argc, char* argv[])
{
    const juce::String mode = argc > 1 ? argv[1] : "table";

    if (mode == "chart" || mode == "chartc")  // chart [preset]; chartc retunes with the cross-fingered C natural first
    {
        const int index = argc > 2 ? presetArg (argv[2]) : 0;
        auto g = presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, index)];
        strouhal = presetVoicing ((size_t) index).jetStrouhal;
        convection = presetVoicing ((size_t) index).jetConvection;
        for (int i = 3; i < argc; ++i)  // headlength=<mm> headdiameter=<mm>: add a head section and retune; st=, conv=: jet
        {
            const juce::String arg (argv[i]);
            const double value = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            if (arg.startsWith ("headlength=")) g.headLength = value * 0.001;
            if (arg.startsWith ("headdiameter=")) g.headDiameter = value * 0.001;
            if (arg.startsWith ("st=")) strouhal = (float) value;
            if (arg.startsWith ("conv=")) convection = (float) value;
            if (arg.startsWith ("h=")) g.windwayHeight = value * 0.001;
        }
        windwayArgs (argc, argv, 3, g);
        if (g.headLength > 0.0)
        {
            const bool ok = tuneToScale (g, lowestNote (g), true);
            std::printf ("Head section %.0f mm long, %.1f mm at the window; retuned: %s\n", g.headLength * 1000.0, g.headDiameter * 1000.0, ok ? "ok" : "closest possible");
        }
        if (mode == "chartc")
        {
            const bool ok = tuneToScale (g, lowestNote (g), true);
            std::printf ("Retuned with C natural: %s, L1 diameter %.2f mm at %.1f mm\n", ok ? "ok" : "closest possible",
                         g.holes[0].diameter * 1000.0, g.holes[0].position * 1000.0);
        }
        chart (g);
        return 0;
    }

    if (mode == "jet")  // jet [preset]: jet stability from the windway profile, against the model's current jet
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        const auto& g = presets()[index];
        const auto v = presetVoicing (index);
        const double h = g.windwayHeight, w = g.windowLength;
        constexpr double nu = kAirViscosity / kAirDensity;

        std::printf ("Check: thin shear layers (theta/h 0.003) against Rayleigh's vortex-sheet jet. alpha h:\n");
        for (double om : { 0.05, 0.1, 0.2, 0.4, 0.8 })
        {
            const auto vs = vortexSheetJetWavenumber (om), ry = jetWavenumber (om, 0.003);
            std::printf ("  omega h/U %.2f: vortex sheet %.4f%+.4fi   Rayleigh %.4f%+.4fi\n", om, vs.real(), vs.imag(), ry.real(), ry.imag());
        }

        std::printf ("\n%s: windway %.1f mm long, %.2f mm high; window %.1f mm (W/h %.1f).\n"
                     "Per frequency: phase speed c/U and growth over the window (dB) from the profile theory, then\n"
                     "the model's current jet (delay at %.2f U, band-pass at Strouhal %.3f, growth e^(0.38 W/h)).\n",
                     g.name.toRawUTF8(), g.windwayLength * 1000.0, h * 1000.0, w * 1000.0, w / h, v.jetConvection, v.jetStrouhal);
        for (double pressure : { 300.0, 1000.0, 3000.0 })
        {
            const double u = std::sqrt (2.0 * pressure / kAirDensity);
            const double theta = std::min (0.067 * h, 0.664 * std::sqrt (nu * g.windwayLength / u));  // Blasius, at most Poiseuille
            std::printf ("\n  %.0f Pa: U %.0f m/s, boundary layers theta/h %.3f\n     f (Hz)  omega h/U    theory c/U  growth     model c/U  gain\n",
                         pressure, u, theta / h);
            Complex guess (0.0, 0.0);
            for (double f : { 300.0, 600.0, 1200.0, 2400.0, 4800.0 })
            {
                const double om = 2.0 * juce::MathConstants<double>::pi * f * h / u;
                const auto k = jetWavenumber (om, theta / h, guess);
                // The model: band-pass(es) (unity at the centre) and a pure delay, times the fixed growth.
                const double centre = 2.0 * juce::MathConstants<double>::pi * v.jetStrouhal;  // omega h/U at the centre
                const std::complex<double> s (0.0, om / centre);
                const auto bp = std::pow (s / (1.0 / 0.7) / (s * s + s / 0.7 + 1.0) * (1.0 / 0.7) * 0.7, 2.0);  // two stages, Q 0.7
                const double modelGain = 20.0 * std::log10 (std::abs (bp) * std::exp (0.38 * w / h));
                const double phaseDelay = om * w / h / v.jetConvection - std::arg (bp);  // radians
                const double modelSpeed = om * w / h / phaseDelay;
                if (std::isfinite (k.real()))
                {
                    guess = k;
                    std::printf ("    %6.0f   %6.3f      %6.2f   %+6.1f dB    %6.2f   %+6.1f dB\n", f, om, om / k.real(),
                                 -k.imag() * w / h * 20.0 / std::log (10.0), modelSpeed, modelGain);
                }
                else
                    std::printf ("    %6.0f   %6.3f      (no growing wave)      %6.2f   %+6.1f dB\n", f, om, modelSpeed, modelGain);
            }
        }
        return 0;
    }

    if (mode == "breath")  // breath [preset] [wav=<prefix>]: register thresholds in slow and fast pressure sweeps, up and back down
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        auto g = presets()[index];
        windwayArgs (argc, argv, 3, g);
        auto voicing = presetVoicing (index);
        windwayNoiseArg (argc, argv, voicing);
        voicing.windwayMass = windwayMass;
        voicing.windwayFriction = windwayFriction;
        std::printf ("%s: breath swept from 100 to 5000 Pa and back (exponentially), slowly (4 s each way) and fast (1 s).\n"
                     "Which register sounds over which pressures, in the order heard (1 = low register, - = no clear note,\n"
                     "below -40 dBFS or not on a resonance). Going up and coming down differ: flutes have hysteresis. A brief 1\n"
                     "between registers 2 and 3 is the crossover: both sound at once, and together they repeat at the low note.\n",
                     g.name.toRawUTF8());

        constexpr double rate = 48000.0, lo = 100.0, hi = 5000.0;
        for (const auto& c : lowRegister (g))
        {
            const auto open = c.openness (g);
            const auto res = resonances (g, open, lowestNote (g) * 0.6, 8000.0);
            std::printf ("\n  %-6s %s  resonances", c.name, c.holePattern (g).c_str());
            for (size_t r = 0; r < std::min<size_t> (3, res.size()); ++r)
                std::printf (" %.0f", res[r]);
            std::printf (" Hz\n");

            for (double sweep : { 4.0, 1.0 })
            {
                // Up, hold the top for 0.2 s, back down, then a short silence.
                auto pressureAt = [sweep] (double t)
                {
                    if (t < sweep) return lo * std::pow (hi / lo, t / sweep);
                    if (t < sweep + 0.2) return hi;
                    if (t < 2.0 * sweep + 0.2) return hi * std::pow (lo / hi, (t - sweep - 0.2) / sweep);
                    return 0.0;
                };
                const double seconds = 2.0 * sweep + 0.4;
                const auto out = playGesture (g, voicing, open, pressureAt, seconds, rate);
                if (sweep > 2.0)
                    saveWavArg (argc, argv, c.name, out, rate);

                // Analyse 100 ms windows every 10 ms; each is labelled with the pressure at its centre.
                struct Segment { int reg; double from, to; int windows; };
                std::vector<Segment> segments;
                const auto window = (size_t) (0.1 * rate);
                for (double t = 0.12; t < 2.0 * sweep + 0.2; t += 0.01)
                {
                    const auto end = (size_t) (t * rate);
                    int reg = 0;
                    if (levelDb (out, end - window / 2, end) > -40.0)
                    {
                        const std::vector<float> chunk (out.begin() + (long) (end - window - (size_t) (rate / 120.0) - 4), out.begin() + (long) end);
                        double clarity = 0.0;
                        const double f = measurePitch (chunk, rate, clarity);
                        for (size_t r = 0; f > 0.0 && clarity > 0.8 && r < res.size(); ++r)
                            if (std::abs (1200.0 * std::log2 (f / res[r])) < 80.0)
                                reg = (int) r + 1;
                    }
                    const double p = pressureAt (t - 0.05);
                    if (segments.empty() || segments.back().reg != reg)
                        segments.push_back ({ reg, p, p, 1 });
                    else
                    {
                        segments.back().to = p;
                        ++segments.back().windows;
                    }
                }
                // Drop blips shorter than 30 ms, then merge neighbours that became equal.
                std::vector<Segment> clean;
                for (const auto& sgm : segments)
                {
                    if (sgm.windows < 3 && ! clean.empty())
                    {
                        clean.back().to = sgm.to;
                        continue;
                    }
                    if (! clean.empty() && clean.back().reg == sgm.reg)
                        clean.back().to = sgm.to;
                    else
                        clean.push_back (sgm);
                }
                std::printf ("    %s:", sweep > 2.0 ? "slow" : "fast");
                for (const auto& sgm : clean)
                    std::printf ("  %s %.0f-%.0f", sgm.reg == 0 ? "-" : std::to_string (sgm.reg).c_str(), sgm.from, sgm.to);
                std::printf ("\n");
            }
        }
        return 0;
    }

    if (mode == "tongue")  // tongue [preset] [p=<Pa>]: repeated tonguing with 10, 20 and 40 ms gaps
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        auto g = presets()[index];
        windwayArgs (argc, argv, 3, g);
        auto voicing = presetVoicing (index);
        windwayNoiseArg (argc, argv, voicing);
        voicing.windwayMass = windwayMass;
        voicing.windwayFriction = windwayFriction;
        double pressure = 700.0;
        for (int i = 3; i < argc; ++i)
            if (juce::String (argv[i]).startsWith ("p="))
                pressure = juce::String (argv[i]).fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        std::printf ("%s at %.0f Pa: a note, then four more tongued after a gap with no breath (hard tongue: 2 ms\n"
                     "steps). Per gap length: how far the sound falls in the gap, and how long each re-attack takes to\n"
                     "reach 90 %% of the steady level (2 ms windows). The first note is a fresh start.\n\n", g.name.toRawUTF8(), pressure);
        std::printf ("  fingering   fresh   gap 10 ms: dip / re-attack   gap 20 ms           gap 40 ms\n");

        constexpr double rate = 48000.0, noteLength = 0.25;
        for (const auto& c : lowRegister (g))
        {
            std::printf ("  %s  ", c.holePattern (g).c_str());
            bool first = true;
            for (double gap : { 0.010, 0.020, 0.040 })
            {
                const double period = noteLength + gap;
                auto pressureAt = [=] (double t) { return std::fmod (t, period) < noteLength && t < 5.0 * period ? pressure : 0.0; };
                const auto out = playGesture (g, voicing, c.openness (g), pressureAt, 5.0 * period + 0.05, rate);

                const auto window = (size_t) (0.002 * rate);
                const auto noteEnd = (size_t) (noteLength * rate);
                const double steady = levelDb (out, noteEnd - (size_t) (0.05 * rate), noteEnd);
                auto attack = [&] (double start)
                {
                    for (auto i = (size_t) (start * rate); i + window < out.size(); i += window)
                        if (levelDb (out, i, i + window) >= steady + 20.0 * std::log10 (0.9))
                            return ((double) i + (double) window) / rate - start;
                    return 9.999;
                };
                if (first)
                {
                    std::printf ("%5.0f ms   ", attack (0.0) * 1000.0);
                    first = false;
                }
                double dip = 0.0, reattack = 0.0;
                for (int k = 1; k < 5; ++k)
                {
                    const double start = k * period;
                    dip += levelDb (out, (size_t) ((start - 0.004) * rate), (size_t) (start * rate)) - steady;
                    reattack += attack (start);
                }
                std::printf ("%+5.0f dB / %3.0f ms     ", dip / 4.0, reattack / 4.0 * 1000.0);
            }
            std::printf ("\n");
        }
        return 0;
    }

    if (mode == "passive")  // passive [preset]: the time-domain bore against the frequency-domain calculation, jet off
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        const auto& g = presets()[index];
        std::printf ("%s: passive resonances (jet off). An impulse at the window; the model's admittance U/p there\n"
                     "against Acoustics' 1/Z. Per resonance: frequency (Acoustics), the model's offset in cents,\n"
                     "Q (Acoustics / model) and the model's peak height relative to Acoustics (dB).\n\n", g.name.toRawUTF8());

        constexpr double hostRate = 48000.0, rate = 2.0 * hostRate;  // The model runs at twice the host rate
        constexpr int order = 17;
        constexpr size_t size = (size_t) 1 << order;

        // Peak of a sampled magnitude curve near index i: refined frequency, peak value, and Q from the half-power width.
        struct Peak { double hz, height, q; };
        auto describe = [] (const std::vector<double>& freq, const std::vector<double>& mag, size_t i)
        {
            const double l = std::log (mag[i - 1]), m = std::log (mag[i]), r = std::log (mag[i + 1]);
            const double denom = l - 2.0 * m + r;
            const double offset = denom < 0.0 ? 0.5 * (l - r) / denom : 0.0;
            const double hz = freq[i] + offset * (freq[i + 1] - freq[i - 1]) * 0.5;
            const double half = mag[i] / std::sqrt (2.0);
            size_t lo = i, hi = i;
            while (lo > 0 && mag[lo] > half) --lo;
            while (hi + 1 < mag.size() && mag[hi] > half) ++hi;
            return Peak { hz, mag[i], hz / std::max (1.0e-9, freq[hi] - freq[lo]) };
        };

        juce::dsp::FFT fft (order);
        for (const auto& c : chartFor (g.chart))
        {
            const auto open = c.openness (g);
            WhistleModel model;
            model.setGeometry (g);
            model.prepare (hostRate);
            model.setControls (controlsFor (open));  // No breath: the jet does nothing
            std::vector<float> settle ((size_t) (0.01 * hostRate), 0.0f);
            model.render (settle.data(), (int) settle.size());

            std::vector<float> flow, sink (1, 0.0f);
            flow.reserve (size + 4);
            model.windowFlowLog = &flow;
            model.testSource = 1.0f;  // 1 Pa for one host sample = two internal samples
            model.render (sink.data(), 1);
            model.testSource = 0.0f;
            while (flow.size() < size)
                model.render (sink.data(), 1);

            std::vector<std::complex<float>> in (size), out (size);
            for (size_t i = 0; i < size; ++i)
                in[i] = flow[i];
            fft.perform (in.data(), out.data(), false);

            std::vector<double> freq, modelMag;
            for (size_t i = 0; i < size / 2; ++i)
            {
                const double hz = (double) i * rate / (double) size;
                const auto source = 1.0 + std::exp (std::complex<double> (0.0, -2.0 * juce::MathConstants<double>::pi * hz / rate));  // The 2-sample pulse
                freq.push_back (hz);
                modelMag.push_back (std::abs (std::complex<double> (out[i]) / source) + 1.0e-30);
            }

            std::printf ("  %-8s %s", c.name, c.holePattern (g).c_str());
            const auto expected = resonances (g, open, lowestNote (g) * 0.7, 5000.0);
            for (size_t r = 0; r < std::min<size_t> (3, expected.size()); ++r)
            {
                // Acoustics' peak, sampled finely around its resonance.
                std::vector<double> af, am;
                for (double cents = -150.0; cents <= 150.0; cents += 0.5)
                {
                    af.push_back (expected[r] * std::pow (2.0, cents / 1200.0));
                    am.push_back (1.0 / std::abs (windowImpedance (g, open, af.back())));
                }
                const auto a = describe (af, am, (size_t) (std::max_element (am.begin() + 1, am.end() - 1) - am.begin()));

                // The model's highest admittance peak within 150 cents.
                size_t best = 0;
                for (size_t i = 1; i + 1 < freq.size(); ++i)
                    if (freq[i] > a.hz * 0.917 && freq[i] < a.hz * 1.09 && (best == 0 || modelMag[i] > modelMag[best]))
                        best = i;
                const auto m = describe (freq, modelMag, best);
                std::printf ("   %6.0f Hz %+4.0f c Q %3.0f/%3.0f %+5.1f dB", a.hz, 1200.0 * std::log2 (m.hz / a.hz), a.q, m.q,
                             20.0 * std::log10 (m.height / a.height));
            }
            std::printf ("\n");
        }
        return 0;
    }

    if (mode == "attack")  // attack [preset] [mass= friction= windway=]: how fast notes build up, tongued
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        auto g = presets()[index];
        windwayArgs (argc, argv, 3, g);
        auto voicing = presetVoicing (index);  // With its breath noise, which seeds the note as in the app
        windwayNoiseArg (argc, argv, voicing);
        voicing.windwayMass = windwayMass;
        voicing.windwayFriction = windwayFriction;
        std::printf ("%s, windway %.0f mm, mass %.2f, friction %.2f, breath noise %.2f\n", g.name.toRawUTF8(), g.windwayLength * 1000.0, windwayMass,
                     windwayFriction, voicing.breathNoise);
        std::printf ("  Breath steps to the pressure in 2 ms (a hard tongued attack). Times from the step to reach\n"
                     "  10 %% and 90 %% of the note's steady level (RMS in 2 ms windows), and the steady level.\n");
        std::printf ("  fingering   pressure    10 %%     90 %%    level\n");

        constexpr double rate = 48000.0;
        const auto window = (size_t) (rate * 0.002);
        for (const auto& c : lowRegister (g))
        {
            for (double pressure : { 300.0, 1500.0 })
            {
                WhistleModel model;
                model.setGeometry (g);
                model.prepare (rate);
                model.setVoicing (voicing);
                auto controls = controlsFor (c.openness (g));
                controls.mouthPressure = (float) pressure;  // Reached in one 2 ms ramp, as from one sensor frame
                model.setControls (controls);
                std::vector<float> out ((size_t) (rate * 0.3), 0.0f);
                model.render (out.data(), (int) out.size());

                std::vector<double> rms;
                for (size_t start = 0; start + window <= out.size(); start += window)
                {
                    double sum = 0.0;
                    for (size_t i = start; i < start + window; ++i)
                        sum += (double) out[i] * out[i];
                    rms.push_back (std::sqrt (sum / (double) window));
                }
                double steady = 0.0;  // Average of the last 50 ms
                for (size_t i = rms.size() - 25; i < rms.size(); ++i)
                    steady += rms[i] / 25.0;
                auto reach = [&] (double share)
                {
                    for (size_t i = 0; i < rms.size(); ++i)
                        if (rms[i] >= share * steady)
                            return (double) (i + 1) * 2.0;  // ms, end of the window
                    return 0.0;
                };
                std::printf ("  %s  %6.0f Pa  %5.0f ms %5.0f ms  %6.1f dB\n", c.holePattern (g).c_str(), pressure, reach (0.1), reach (0.9),
                             20.0 * std::log10 (steady + 1e-12));
            }
        }
        return 0;
    }

    if (mode == "noise")  // noise [preset] [noise=0-1] [direct=0-1] [wav=file]: breath noise vs. tone, and a crescendo
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        const auto& g = presets()[index];
        auto voicing = presetVoicing (index);
        float direct = 0.0f, noiseBand = 0.08f, pulse = 0.8f;
        juce::String wavPath;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String arg (argv[i]);
            const auto value = arg.fromFirstOccurrenceOf ("=", false, false);
            if (arg.startsWith ("noise=")) voicing.breathNoise = value.getFloatValue();
            if (arg.startsWith ("direct=")) direct = value.getFloatValue();
            if (arg.startsWith ("band=")) noiseBand = value.getFloatValue();
            if (arg.startsWith ("pulse=")) pulse = value.getFloatValue();
            if (arg.startsWith ("wav=")) wavPath = value;
        }

        constexpr double rate = 48000.0;
        auto makeModel = [&]
        {
            auto model = std::make_unique<WhistleModel>();
            model->setGeometry (g);
            model->prepare (rate);
            model->jetStrouhal = voicing.jetStrouhal;
            model->jetConvection = voicing.jetConvection;
            model->breathNoise = voicing.breathNoise;
            model->directNoise = direct;
            model->noiseStrouhal = noiseBand;
            model->noisePulsing = pulse;
            return model;
        };

        constexpr int fftOrder = 14, fftSize = 1 << fftOrder;
        juce::dsp::FFT fft (fftOrder);
        std::printf ("%s, breath noise %.2f, direct %.2f\n", g.name.toRawUTF8(), voicing.breathNoise, direct);

        for (int semitones : { 0, 5 })
        {
            const ChartFingering* fingering = nullptr;
            for (const auto& c : chartFor (g.chart))
                if (c.semitones == semitones && c.reg == 1 && fingering == nullptr)
                    fingering = &c;
            const double target = lowestNote (g) * std::pow (2.0, semitones / 12.0);
            std::printf ("  %s (%s, %.0f Hz):\n", fingering->name, fingering->holePattern (g).c_str(), target);

            for (double pressure : { 60.0, 120.0, 200.0, 300.0, 500.0, 800.0, 1200.0, 2000.0 })
            {
                auto model = makeModel();
                auto controls = controlsFor (fingering->openness (g));
                std::vector<float> out ((size_t) (rate * 0.8), 0.0f);
                for (int at = 0; at < (int) out.size(); at += 96)
                {
                    controls.mouthPressure = (float) (pressure * std::min (1.0, at / (rate * 0.04)));
                    model->setControls (controls);
                    model->render (out.data() + at, std::min (96, (int) out.size() - at));
                }

                // Spectrum of the last ~0.34 s.
                std::vector<float> data ((size_t) fftSize * 2, 0.0f);
                double rms = 0.0;
                for (int i = 0; i < fftSize; ++i)
                {
                    const float x = out[out.size() - (size_t) fftSize + (size_t) i];
                    rms += (double) x * x;
                    data[(size_t) i] = x * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (fftSize - 1)));
                }
                fft.performFrequencyOnlyForwardTransform (data.data(), true);
                const double binHz = rate / fftSize;
                auto db = [] (double v) { return 20.0 * std::log10 (v + 1e-12); };

                // Strongest spectral peak (the tone, or the strongest resonance of an airy note), and how far the
                // noise between the peak's harmonics (1-8 kHz, median) lies below it.
                int peak = (int) (200.0 / binHz);
                for (int b = peak; b < (int) (5000.0 / binHz); ++b)
                    if (data[(size_t) b] > data[(size_t) peak])
                        peak = b;
                const double f = peak * binHz;
                std::vector<double> between;
                for (int b = (int) (1000.0 / binHz); b < (int) (8000.0 / binHz); ++b)
                {
                    const double harmonic = b * binHz / f;
                    if (std::abs (harmonic - std::round (harmonic)) > 0.25)
                        between.push_back (data[(size_t) b]);
                }
                std::nth_element (between.begin(), between.begin() + (long) between.size() / 2, between.end());
                const double snr = db (data[(size_t) peak]) - db (between[between.size() / 2]);
                std::printf ("    %5.0f Pa  level %6.1f dBFS  peak %6.0f Hz %-4s  %5.1f dB above the noise between its harmonics%s\n", pressure,
                             10.0 * std::log10 (rms / fftSize + 1e-20), f, noteName (f).c_str(), snr, snr < 25.0 ? "  (airy, no clear tone)" : "");
            }
        }

        if (wavPath.isNotEmpty())
        {
            // Crescendo on the second fingering: from silence through the airy whoosh into the tone, then held.
            const ChartFingering* fingering = nullptr;
            for (const auto& c : chartFor (g.chart))
                if (c.semitones == 5 && fingering == nullptr)
                    fingering = &c;
            auto model = makeModel();
            auto controls = controlsFor (fingering->openness (g));
            std::vector<float> out ((size_t) (rate * 6.0), 0.0f);
            for (int at = 0; at < (int) out.size(); at += 96)
            {
                controls.mouthPressure = (float) (1200.0 * std::min (1.0, at / (rate * 5.0)));
                model->setControls (controls);
                model->render (out.data() + at, std::min (96, (int) out.size() - at));
            }
            juce::AudioBuffer<float> buffer (1, (int) out.size());
            buffer.copyFrom (0, 0, out.data(), (int) out.size());
            const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (wavPath);
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            std::printf ("Saved %s (0 to 1200 Pa over 5 s on %s)\n", file.getFullPathName().toRawUTF8(), fingering->name);
        }
        return 0;
    }

    if (mode == "scale")  // scale [preset]: the test scale the app plays (pressure per note, found with the model)
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        auto g = presets()[index];
        auto scaleVoicing = presetVoicing (index);
        for (int i = 4; i < argc; ++i)  // edge=<mm> noise=<0-1>: try voicing changes
        {
            const juce::String arg (argv[i]);
            const double value = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
            if (arg.startsWith ("edge=")) g.edgeOffset = value * 0.001;
            if (arg.startsWith ("noise=")) scaleVoicing.breathNoise = (float) value;
        }
        windwayArgs (argc, argv, 3, g);
        scaleVoicing.windwayMass = windwayMass;
        scaleVoicing.windwayFriction = windwayFriction;
        const double lowest = lowestNote (g);
        const auto planStart = juce::Time::getMillisecondCounterHiRes();
        const auto steps = planScale (g, scaleVoicing, lowest);
        std::printf ("%s: %d notes, planned in %.0f ms\n", g.name.toRawUTF8(), (int) steps.size(), juce::Time::getMillisecondCounterHiRes() - planStart);
        // Play it exactly as the app does (same timing and breath envelope), measure every note, and save a WAV.
        constexpr double rate = 48000.0;
        std::vector<float> out;
        const auto played = playScale (g, scaleVoicing, steps, rate, &out);
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const double f = played[i];
            const double target = lowest * std::pow (2.0, std::round (12.0 * std::log2 (f > 0.0 ? f / lowest : 1.0)) / 12.0);
            std::printf ("  %-5s  %5.0f Pa  planned %+4.0f c   played %s\n", steps[i].name.c_str(), steps[i].pressure, steps[i].cents,
                         f > 0.0 ? (noteName (f) + " " + std::to_string ((int) std::lround (1200.0 * std::log2 (f / target))) + " c").c_str() : "-");
        }

        if (argc > 3 && ! juce::String (argv[3]).containsChar ('='))  // A WAV file name, not an option
        {
            juce::AudioBuffer<float> buffer (1, (int) out.size());
            buffer.copyFrom (0, 0, out.data(), (int) out.size());
            const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
            std::printf ("Saved %s\n", file.getFullPathName().toRawUTF8());
        }
        return 0;
    }

    if (mode == "notes")  // notes <preset> <pinch 0-1> [st=] [conv=]: the recorder's top notes with a given thumb opening
    {
        const auto presetIndex = (size_t) presetArg (argv[2]);
        auto g = presets()[presetIndex];
        strouhal = presetVoicing (presetIndex).jetStrouhal;
        const double pinch = juce::String (argv[3]).getDoubleValue();
        for (int i = 4; i < argc; ++i)
        {
            const juce::String arg (argv[i]);
            const float value = arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            if (arg.startsWith ("st=")) strouhal = value;
            if (arg.startsWith ("conv=")) convection = value;
        }
        std::printf ("thumb %.0f %% open, jet Strouhal %.3f, convection %.2f\n", pinch * 100.0, strouhal, convection);
        for (const auto& c : chartFor (g.chart))
        {
            if (c.semitones < 21)
                continue;
            auto open = c.openness (g);
            open.holes[0] = pinch;
            const double target = lowestNote (g) * std::pow (2.0, c.semitones / 12.0);
            const auto r = resonances (g, open, target * 0.8, target * 1.25);
            std::printf ("  %-8s resonance %+4.0f c   played:", c.name, r.empty() ? 0.0 : 1200.0 * std::log2 (r.front() / target));
            for (double p = 2000.0; p <= 5000.0; p += 500.0)
            {
                const double f = playOnce (g, open, p);
                if (f <= 0.0)
                    std::printf ("     -  ");
                else
                    std::printf (" %+6.0f ", 1200.0 * std::log2 (f / target));
            }
            std::printf ("   (2-5 kPa, cents from the note)\n");
        }
        return 0;
    }

    if (mode == "modes")  // modes <preset>: which resonance (register) each chart fingering plays as pressure rises
    {
        const auto g = presets()[(size_t) (argc > 2 ? presetArg (argv[2]) : 0)];
        const double pressures[] = { 300, 600, 1000, 1500, 2200, 3000, 4000, 5500, 7000, 8500 };
        std::printf ("\n%s: resonance played (1 = low register, 2 = octave, 3 = twelfth, 4 = double octave) and note\n  fingering  ", g.name.toRawUTF8());
        for (double p : pressures)
            std::printf ("%7.0f Pa  ", p);
        std::printf ("\n");
        for (const auto& c : chartFor (g.chart))
        {
            if (c.reg != 1)
                continue;
            const auto r = resonances (g, c.openness (g), 150.0, 8000.0);
            std::printf ("  %s  ", c.holePattern (g).c_str());
            for (double p : pressures)
            {
                const double f = playOnce (g, c.openness (g), p);
                int resonance = 0;  // 0 = not within 100 cents of any resonance
                for (size_t i = 0; i < r.size(); ++i)
                    if (f > 0.0 && std::abs (1200.0 * std::log2 (f / r[i])) < 100.0)
                        resonance = (int) i + 1;
                if (f <= 0.0)
                    std::printf ("    -       ");
                else
                    std::printf (" %d %-6s   ", resonance, noteName (f).c_str());
            }
            std::printf ("\n");
        }
        return 0;
    }

    if (mode == "sweep")  // sweep <preset> <hole> <pattern>: resonances while moving one hole
    {
        auto g = presets()[(size_t) presetArg (argv[2])];
        tuneToScale (g, lowestNote (g));
        const int hole = juce::String (argv[3]).getIntValue();
        const auto open = Openness::fromPattern (argv[4]);
        for (double x = 0.10; x <= g.boreLength - 0.01; x += 0.005)
        {
            g.holes[(size_t) hole].position = x;
            const auto r = resonances (g, open, 300.0, 3000.0);
            std::printf ("  %5.1f mm:", x * 1000.0);
            for (size_t i = 0; i < std::min<size_t> (3, r.size()); ++i)
                std::printf ("  %7.1f %-4s", r[i], noteName (r[i]).c_str());
            std::printf ("\n");
        }
        return 0;
    }

    if (mode == "head")  // head [preset]
    {
        const int index = argc > 2 ? presetArg (argv[2]) : 0;
        head (presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, index)]);
        return 0;
    }

    if (mode == "slide")  // slide <hole 1-6> <pressure>
    {
        slide (presets().front(), juce::jlimit (1, kNumHoles - 1, argc > 2 ? juce::String (argv[2]).getIntValue() : 3), argc > 3 ? juce::String (argv[3]).getDoubleValue() : 700.0);
        return 0;
    }

    if (mode == "play")
    {
        const int index = argc > 2 ? presetArg (argv[2]) : 0;
        for (int i = 3; i < argc; ++i)  // st=, q=, conv=
        {
            const juce::String arg (argv[i]);
            const float value = arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            if (arg.startsWith ("st=")) strouhal = value;
            if (arg.startsWith ("q=")) jetQ = value;
            if (arg.startsWith ("conv=")) convection = value;
            if (arg.startsWith ("stages=")) stages = (int) value;
        }
        auto g = presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, index)];
        windwayArgs (argc, argv, 3, g);
        std::printf ("jet: Strouhal %.3f, Q %.2f, convection %.2f, stages %d; windway mass %.2f, friction %.2f\n", strouhal, jetQ, convection,
                     stages, windwayMass, windwayFriction);
        play (g);
        return 0;
    }

    int presetIndex = -1;
    for (auto g : presets())
    {
        ++presetIndex;
        if (mode == "code" && argc > 2 && presetArg (argv[2]) != presetIndex)
            continue;  // code <index>: one preset only

        if (mode == "code")  // The presets' holes as code, for WhistleGeometry.cpp (after tuning them with tunec).
        {
            tuneToScale (g, lowestNote (g), true);
            std::printf ("  %s: length %.5f\n    { {", g.name.toRawUTF8(), g.boreLength);
            for (const auto& h : g.holes)
                std::printf (" { %s, %.5f, %.5f },", h.enabled ? "true" : "false", h.position, h.diameter);
            std::printf (" } }\n");
            continue;
        }

        if (mode == "tune" || mode == "tunec")  // tunec: also tune cross-fingerings with hole sizes
        {
            const bool ok = tuneToScale (g, lowestNote (g), mode == "tunec");
            std::printf ("%s: %s\n  bore %.4f  holes (position/diameter)", g.name.toRawUTF8(), ok ? "tuned" : "DID NOT CONVERGE", g.boreLength);
            for (const auto& h : g.holes)
                std::printf ("  %.4f/%.4f", h.position, h.diameter);
            std::printf ("\n  ");
            for (const auto& c : chartFor (g.chart))
                std::printf ("%s %+.0f  ", c.name, predictedCents (g, c, lowestNote (g)));
            std::printf ("\n");
        }
        printTable (g);
    }
    return 0;
}
