// Checks the whistle acoustics and model without the app.
//
//   WhistleCheck tune             tune every preset to D (D5, or D4 for low D) and print the hole positions
//   WhistleCheck table            predicted resonances for the scale fingerings of every preset
//   WhistleCheck play [preset]    run the time-domain model at fixed fingerings and pressures, measure what it plays
//   WhistleCheck slide [hole] [Pa]  open one hole (1 = L1 ... 6 = R3) gradually (half-holing) and measure the pitch
//   WhistleCheck chart [preset]   every fingering of a standard whistle chart: predicted and played tuning
//   WhistleCheck head [preset]    how a narrower head section changes the second register's tuning
//   WhistleCheck scale [preset] [file.wav]  the app's test scale: plan, play, measure each note (and save it)
//   WhistleCheck modes [preset]   which register (resonance) each fingering plays as the pressure rises to 8.5 kPa

#include <juce_audio_formats/juce_audio_formats.h>

#include "Whistle/Acoustics.h"
#include "Whistle/PitchDetect.h"
#include "Whistle/ScalePlan.h"
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

    if (mode == "scale")  // scale [preset]: the test scale the app plays (pressure per note, found with the model)
    {
        const auto index = (size_t) (argc > 2 ? presetArg (argv[2]) : 0);
        const auto& g = presets()[index];
        const double lowest = lowestNote (g);
        const auto planStart = juce::Time::getMillisecondCounterHiRes();
        const auto steps = planScale (g, presetVoicing (index), lowest);
        std::printf ("%s: %d notes, planned in %.0f ms\n", g.name.toRawUTF8(), (int) steps.size(), juce::Time::getMillisecondCounterHiRes() - planStart);
        // Play it exactly as the app does (same timing and breath envelope), measure every note, and save a WAV.
        constexpr double rate = 48000.0;
        WhistleModel model;
        model.setGeometry (g);
        model.prepare (rate);
        const auto voicing = presetVoicing (index);
        model.jetStrouhal = voicing.jetStrouhal;
        model.jetConvection = voicing.jetConvection;
        model.breathNoise = voicing.breathNoise;
        std::vector<float> out ((size_t) (rate * (kScaleNoteSeconds * (double) steps.size() + 0.5)), 0.0f);
        const int block = 64;
        for (int start = 0; start < (int) out.size(); start += block)
        {
            Controls controls;
            scaleControlsAt (steps, start / rate, controls);
            model.setControls (controls);
            model.render (out.data() + start, std::min (block, (int) out.size() - start));
        }

        for (size_t i = 0; i < steps.size(); ++i)
        {
            // The steady part of each note: from its start to just before its release.
            const auto from = (size_t) (rate * kScaleNoteSeconds * (double) i);
            const auto to = from + (size_t) (rate * (kScaleNoteSeconds - kScaleGap - kScaleRelease));
            std::vector<float> note (out.begin() + (long) from, out.begin() + (long) to);
            double clarity = 0.0;
            const double f = measurePitch (note, rate, clarity);
            const double target = lowest * std::pow (2.0, std::round (12.0 * std::log2 (f > 0.0 ? f / lowest : 1.0)) / 12.0);
            std::printf ("  %-5s  %5.0f Pa  planned %+4.0f c   played %s\n", steps[i].name.c_str(), steps[i].pressure, steps[i].cents,
                         f > 0.0 && clarity > 0.8 ? (noteName (f) + " " + std::to_string ((int) std::lround (1200.0 * std::log2 (f / target))) + " c").c_str() : "-");
        }

        if (argc > 3)
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
        std::printf ("jet: Strouhal %.3f, Q %.2f, convection %.2f, stages %d\n", strouhal, jetQ, convection, stages);
        play (presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, index)]);
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
