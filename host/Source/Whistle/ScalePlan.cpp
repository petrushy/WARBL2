#include "ScalePlan.h"

#include "PitchDetect.h"
#include "WhistleModel.h"

#include <algorithm>
#include <set>

namespace whistle
{

namespace
{
std::string noteName (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return names[(midi % 12 + 12) % 12] + std::to_string (midi / 12 - 1);
}

// Plays one fingering at one pressure (blown up over 40 ms, like a player) and returns the pitch, or 0 if it
// doesn't sound clearly as a note: periodic, and at least -40 dBFS (~60 dB SPL at the listener), not a whisper
// right at the threshold that may not start at all when played.
double play (const Geometry& g, const Voicing& v, const Openness& open, double pressure)
{
    constexpr double rate = 48000.0;
    WhistleModel model;
    model.setGeometry (g);
    model.prepare (rate);
    model.setVoicing (v);  // With its breath noise, which seeds the note as when the scale is played

    Controls controls;
    for (int i = 0; i < kNumHoles; ++i)
        controls.openness[(size_t) i] = (float) open.holes[(size_t) i];
    controls.footOpenness = (float) open.foot;

    std::vector<float> out ((size_t) (rate * 0.35), 0.0f);
    const int block = (int) (rate * 0.002);
    for (int start = 0; start < (int) out.size(); start += block)
    {
        controls.mouthPressure = (float) (pressure * std::min (1.0, start / (rate * 0.04)));
        model.setControls (controls);
        model.render (out.data() + start, std::min (block, (int) out.size() - start));
    }

    double sum = 0.0;
    const auto tail = (size_t) (0.1 * rate);
    for (size_t i = out.size() - tail; i < out.size(); ++i)
        sum += (double) out[i] * out[i];
    if (sum / (double) tail < 1.0e-8)  // -40 dBFS
        return 0.0;

    double clarity = 0.0;
    const double f = measurePitch (out, rate, clarity);
    return clarity > 0.8 ? f : 0.0;
}
} // namespace

int scaleControlsAt (const std::vector<ScaleStep>& steps, double t, Controls& controls)
{
    const int step = (int) (t / kScaleNoteSeconds);
    if (step < 0 || step >= (int) steps.size())
    {
        controls.mouthPressure = 0.0f;
        return -1;
    }

    const auto& s = steps[(size_t) step];
    const double within = t - step * kScaleNoteSeconds;
    const double sounding = kScaleNoteSeconds - kScaleGap;
    double envelope = 0.0;
    if (within < kScaleAttack)
        envelope = within / kScaleAttack;
    else if (within < sounding - kScaleRelease)
        envelope = 1.0;
    else if (within < sounding)
        envelope = (sounding - within) / kScaleRelease;

    for (int i = 0; i < kNumHoles; ++i)
        controls.openness[(size_t) i] = (float) s.openness.holes[(size_t) i];
    controls.footOpenness = (float) s.openness.foot;
    controls.mouthPressure = (float) (s.pressure * envelope);
    return step;
}

std::vector<ScaleStep> planScale (const Geometry& g, const Voicing& v, double lowestHz, int semitones)
{
    std::vector<ScaleStep> steps;
    std::set<int> done;

    // Chart notes in pitch order; the first fingering listed for a note is used.
    auto chart = chartFor (g.chart);
    std::stable_sort (chart.begin(), chart.end(), [] (const auto& a, const auto& b) { return a.semitones < b.semitones; });

    for (const auto& c : chart)
    {
        if (c.semitones > semitones || done.count (c.semitones) > 0)
            continue;

        const double target = lowestHz * std::pow (2.0, c.semitones / 12.0);
        const auto open = c.openness (g);

        // Try pressures from soft to hard (each register in its usual range) and keep the closest in tune, among
        // those at which the fingering also sounds one step softer (in any register): right at the threshold a
        // note is unreliable, and the breath noise can decide whether it starts at all.
        const double from = c.reg == 1 ? 150.0 : c.reg == 2 ? 600.0 : 1200.0;
        const double to = c.reg == 1 ? 3000.0 : 5000.0;
        ScaleStep best;
        best.cents = 1.0e9f;
        bool softerSounds = play (g, v, open, from / 1.15) > 0.0;
        for (double pressure = from; pressure <= to; pressure *= 1.15)
        {
            const double f = play (g, v, open, pressure);
            const bool sounds = f > 0.0;
            const bool reliable = sounds && softerSounds;
            softerSounds = sounds;
            if (! reliable)
                continue;
            const auto cents = (float) (1200.0 * std::log2 (f / target));
            if (std::abs (cents) < std::abs (best.cents))
            {
                best.cents = cents;
                best.pressure = (float) pressure;
            }
        }

        if (std::abs (best.cents) <= 60.0f)
        {
            best.name = noteName (target);
            best.openness = open;
            best.targetHz = target;
            steps.push_back (best);
            done.insert (c.semitones);
        }
    }

    // Check in context, and give notes that don't sound there more breath.
    for (int pass = 0; pass < 4 && ! steps.empty(); ++pass)
    {
        const auto played = playScale (g, v, steps, 48000.0);
        bool changed = false;
        for (size_t i = 0; i < steps.size(); ++i)
        {
            const double cents = played[i] > 0.0 ? 1200.0 * std::log2 (played[i] / steps[i].targetHz) : 1.0e9;
            if (std::abs (cents) <= 60.0)
            {
                steps[i].cents = (float) cents;
            }
            else if (pass < 3)
            {
                steps[i].pressure *= 1.15f;
                changed = true;
            }
            else
            {
                steps[i].pressure = 0.0f;  // Still not playing: left out below
            }
        }
        if (! changed)
            break;
    }
    steps.erase (std::remove_if (steps.begin(), steps.end(), [] (const ScaleStep& s) { return s.pressure <= 0.0f; }), steps.end());
    return steps;
}

std::vector<double> playScale (const Geometry& g, const Voicing& v, const std::vector<ScaleStep>& steps, double rate,
                               std::vector<float>* audio)
{
    WhistleModel model;
    model.setGeometry (g);
    model.prepare (rate);
    model.setVoicing (v);
    std::vector<float> out ((size_t) (rate * (kScaleNoteSeconds * (double) steps.size() + 0.5)), 0.0f);
    constexpr int block = 64;
    for (int start = 0; start < (int) out.size(); start += block)
    {
        Controls controls;
        scaleControlsAt (steps, start / rate, controls);
        model.setControls (controls);
        model.render (out.data() + start, std::min (block, (int) out.size() - start));
    }

    std::vector<double> pitches;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        // The steady part of each note: from its start to just before its release.
        const auto from = (size_t) (rate * kScaleNoteSeconds * (double) i);
        const auto to = from + (size_t) (rate * (kScaleNoteSeconds - kScaleGap - kScaleRelease));
        const std::vector<float> note (out.begin() + (long) from, out.begin() + (long) to);
        double clarity = 0.0;
        const double f = measurePitch (note, rate, clarity);
        pitches.push_back (clarity > 0.8 ? f : 0.0);
    }
    if (audio != nullptr)
        *audio = std::move (out);
    return pitches;
}

} // namespace whistle
