#include "ScalePlan.h"

#include "PitchDetect.h"
#include "WhistleModel.h"

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

// Plays one fingering at one pressure (blown up over 40 ms, like a player) and returns the pitch, or 0.
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
            steps.push_back (best);
            done.insert (c.semitones);
        }
    }
    return steps;
}

} // namespace whistle
