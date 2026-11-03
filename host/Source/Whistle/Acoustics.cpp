#include "Acoustics.h"

#include <algorithm>
#include <cmath>

namespace whistle
{

namespace
{
using Complex = std::complex<double>;
constexpr double kPi = juce::MathConstants<double>::pi;
const Complex j (0.0, 1.0);

// Wavenumber in a tube of radius a, with viscous and thermal wall losses (Keefe's large-tube approximation).
Complex waveNumber (double omega, double a, double wallLoss)
{
    const double rv = a * std::sqrt (kAirDensity * omega / kAirViscosity);
    const double d = 1.045 / rv * wallLoss;
    return { omega / kSpeedOfSound * (1.0 + d), -omega / kSpeedOfSound * d };
}

// Impedance at the input of a cylinder of length l terminated by zLoad.
Complex throughCylinder (Complex zLoad, double z0, Complex k, double l)
{
    const Complex c = std::cos (k * l), s = std::sin (k * l);
    return z0 * (zLoad * c + j * z0 * s) / (z0 * c + j * zLoad * s);
}

// Shunt admittance of a tone hole that is partly open: the open part behaves like a short tube radiating
// outside (an inertance), the covered part like a closed chimney (a small compliance).
Complex holeAdmittance (const Geometry& g, int hole, double openness, double omega)
{
    const double b = 0.5 * g.holes[(size_t) hole].diameter;
    const double area = kPi * b * b;
    const double z0h = kAirDensity * kSpeedOfSound / area;
    const double k = omega / kSpeedOfSound;
    const double chimney = std::max (g.wallThickness, 0.0003);

    const Complex zOpen = j * omega * kAirDensity * g.holeEffectiveLength (hole) / area
                          + z0h * 0.25 * (k * b) * (k * b);  // Radiation resistance.
    const Complex yClosed = j * std::tan (k * chimney) / z0h;

    return openness / zOpen + (1.0 - openness) * yClosed;
}
} // namespace

Openness Openness::fromPattern (const char* pattern)
{
    Openness open;
    for (int i = 0; i < kNumHoles + 1 && pattern[i] != 0; ++i)
    {
        const char c = pattern[i];
        const double value = c == 'x' ? 0.0 : c == 'h' ? 0.5 : c == 'p' ? 0.25 : 1.0;
        if (i < kNumHoles)
            open.holes[(size_t) i] = value;
        else
            open.foot = value;
    }
    return open;
}

Complex windowImpedance (const Geometry& g, const Openness& open, double hz)
{
    const double omega = 2.0 * kPi * hz;

    // Start at the foot (unflanged pipe radiation, or closed by the bell sensor) and work up to the window.
    const double aFoot = g.radiusAt (g.boreLength);
    const double z0Foot = kAirDensity * kSpeedOfSound / (kPi * aFoot * aFoot);
    const double kaFoot = omega / kSpeedOfSound * aFoot;
    const double footOpen = g.bellClosesFoot ? juce::jlimit (1.0e-4, 1.0, open.foot) : 1.0;
    Complex z = z0Foot * (0.25 * kaFoot * kaFoot + j * 0.6133 * kaFoot) / footOpen;

    // Points where something happens, from the foot up: the enabled holes, then the window at x = 0.
    std::vector<int> order;
    for (int i = 0; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            order.push_back (i);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return g.holes[(size_t) a].position > g.holes[(size_t) b].position; });

    double x = g.boreLength;
    auto travelTo = [&] (double target)
    {
        // Short cylindrical slices follow a tapered bore.
        while (x > target + 1.0e-9)
        {
            const double step = std::min (0.005, x - target);
            const double a = g.radiusAt (x - 0.5 * step);
            const double z0 = kAirDensity * kSpeedOfSound / (kPi * a * a);
            z = throughCylinder (z, z0, waveNumber (omega, a, g.wallLoss), step);
            x -= step;
        }
    };

    for (int hole : order)
    {
        const double position = juce::jlimit (0.0, g.boreLength, g.holes[(size_t) hole].position);
        travelTo (position);
        z = 1.0 / (1.0 / z + holeAdmittance (g, hole, juce::jlimit (0.0, 1.0, open.holes[(size_t) hole]), omega));
    }
    travelTo (0.0);

    // The window: a short opening the air moves through, in series as seen by the jet.
    const Complex zWindow = j * omega * kAirDensity * g.windowEffectiveLength() / g.windowArea();
    return z + zWindow;
}

std::vector<double> resonances (const Geometry& g, const Openness& open, double fMin, double fMax)
{
    // Scan in 2-cent steps, then refine each minimum of |Z| with a parabola through its neighbours (in log |Z|).
    const double step = std::pow (2.0, 2.0 / 1200.0);
    std::vector<double> freqs, mags;
    for (double f = fMin; f <= fMax; f *= step)
    {
        freqs.push_back (f);
        mags.push_back (std::log (std::abs (windowImpedance (g, open, f))));
    }

    std::vector<double> found;
    for (size_t i = 1; i + 1 < mags.size(); ++i)
    {
        if (mags[i] < mags[i - 1] && mags[i] <= mags[i + 1])
        {
            const double denom = mags[i - 1] - 2.0 * mags[i] + mags[i + 1];
            const double offset = denom > 0.0 ? 0.5 * (mags[i - 1] - mags[i + 1]) / denom : 0.0;
            found.push_back (freqs[i] * std::pow (step, offset));
        }
    }
    return found;
}

const std::vector<ChartFingering>& chartFor (FingeringChart chart)
{
    constexpr int bore = ChartFingering::kTunesBore, none = ChartFingering::kTunesNothing;

    // Patterns in hole order: LT L1 L2 L3 R1 R2 R3 R4. Hole indices: LT 0, L1 1, L2 2, L3 3, R1 4, R2 5, R3 6, R4 7.
    static const std::vector<ChartFingering> whistle {
        { "D", "-xxxxxx-", 0, 1, bore },       { "E", "-xxxxxo-", 2, 1, 6 },          { "F#", "-xxxxoo-", 4, 1, 5 },
        { "G", "-xxxooo-", 5, 1, 4 },          { "A", "-xxoooo-", 7, 1, 3 },          { "B", "-xooooo-", 9, 1, 2 },
        { "C nat", "-oxxooo-", 10, 1, none, 1 }, { "C#", "-oooooo-", 11, 1, 1 },
        { "high D", "-oxxxxx-", 12, 2 },       { "high D", "-xxxxxx-", 12, 2 },       { "high E", "-xxxxxo-", 14, 2 },
        { "high F#", "-xxxxoo-", 16, 2 },      { "high G", "-xxxooo-", 17, 2 },       { "high A", "-xxoooo-", 19, 2 },
        { "high B", "-xooooo-", 21, 2 },
    };

    // Baroque recorder fingering in C, with its forked F, F# and Bb. Each hole's position is set by a note
    // that uses it (R2 by the forked F, the thumb hole by high D: thumb open, L2 covered). With cross-fingering
    // tuning, the F# fork sets R1's size. The Bb fork and the second register (thumb pinched, leaving a small
    // crack open) are left to the geometry: L2's size alone can't put Bb in tune.
    static const std::vector<ChartFingering> recorder {
        { "C", "xxxxxxxx", 0, 1, bore },       { "D", "xxxxxxxo", 2, 1, 7 },          { "E", "xxxxxxoo", 4, 1, 6 },
        { "F", "xxxxxoxx", 5, 1, 5 },          { "F#", "xxxxoxxo", 6, 1, none, 4 },   { "G", "xxxxoooo", 7, 1, 4 },
        { "A", "xxxooooo", 9, 1, 3 },          { "Bb", "xxoxxooo", 10, 1 },  { "B", "xxoooooo", 11, 1, 2 },
        { "high C", "xoxooooo", 12, 1, 1 },    { "high D", "ooxooooo", 14, 1, 0 },
        { "high E", "pxxxxxoo", 16, 2 },       { "high F", "pxxxxoxo", 17, 2 },       { "high G", "pxxxoooo", 19, 2 },
        { "high A", "pxxooooo", 21, 2 },
    };

    return chart == FingeringChart::baroqueRecorder ? recorder : whistle;
}

double predictedCents (const Geometry& g, const ChartFingering& c, double lowestHz)
{
    const double target = lowestHz * std::pow (2.0, c.semitones / 12.0);
    double nearest = 0.0;
    for (double f : resonances (g, c.openness(), target * 0.7, target * 1.4))
        if (nearest == 0.0 || std::abs (std::log2 (f / target)) < std::abs (std::log2 (nearest / target)))
            nearest = f;
    return nearest > 0.0 ? 1200.0 * std::log2 (nearest / target) : 0.0;
}

namespace
{
double firstResonance (const Geometry& g, const Openness& open, double guessHz)
{
    const auto found = resonances (g, open, guessHz * 0.7, guessHz * 1.4);
    return found.empty() ? 0.0 : found.front();
}

// Solves f(value) = target for one length by the secant method (pitch falls as the length grows).
// Resonances come from a 2-cent scan refined by interpolation, so aim for 0.5 cents and accept 2.
bool solveLength (Geometry& g, double& value, double lo, double hi, const Openness& open, double target)
{
    double x0 = value, f0 = firstResonance (g, open, target);
    double x1 = juce::jlimit (lo, hi, value * (f0 > 0.0 ? f0 / target : 1.0));
    double bestValue = value, bestError = 1.0e9;
    for (int iteration = 0; iteration < 30; ++iteration)
    {
        value = x1;
        const double f1 = firstResonance (g, open, target);
        if (f1 <= 0.0 || f0 <= 0.0)
            break;
        const double error = std::log2 (f1 / target) * 1200.0;
        if (std::abs (error) < std::abs (bestError))
        {
            bestError = error;
            bestValue = value;
        }
        if (std::abs (error) < 0.5)
            return true;
        const double slope = (std::log2 (f1) - std::log2 (f0)) / (x1 - x0);
        const double next = std::abs (slope) > 1.0e-12 ? x1 - std::log2 (f1 / target) / slope : x1 * (f1 / target);
        x0 = x1;
        f0 = f1;
        x1 = juce::jlimit (lo, hi, next);
    }
    value = bestValue;
    return std::abs (bestError) < 2.0;
}

// Tunes the bore and hole positions (diameters fixed), in chart order: the lowest note first, then holes
// from the bottom up. Moving a hole slightly changes the notes below it (its closed chimney is part of
// their bore), so the pass is repeated.
bool tunePositions (Geometry& g, double lowestHz)
{
    const auto& chart = chartFor (g.chart);
    auto targetFor = [&] (const ChartFingering& c) { return lowestHz * std::pow (2.0, c.semitones / 12.0); };

    // Closely spaced holes (e.g. a recorder's) need more passes.
    for (int pass = 0; pass < 20; ++pass)
    {
        double below = g.boreLength;  // Position of the hole tuned just before (lower down).
        for (const auto& c : chart)
        {
            if (c.tunes == ChartFingering::kTunesBore)
            {
                if (! solveLength (g, g.boreLength, 0.05, 2.0, c.openness(), targetFor (c)))
                    return false;
                below = g.boreLength;
            }
            else if (c.tunes >= 0 && g.holes[(size_t) c.tunes].enabled)
            {
                auto& hole = g.holes[(size_t) c.tunes];
                const double hi = c.tunes == 0 ? g.boreLength - hole.diameter  // The thumb hole is on the back.
                                               : below - hole.diameter;
                if (! solveLength (g, hole.position, 0.01, hi, c.openness(), targetFor (c)))
                    return false;
                if (c.tunes != 0)
                    below = hole.position;
            }
        }

        double worst = 0.0;
        for (const auto& c : chart)
            if (c.tunes != ChartFingering::kTunesNothing && (c.tunes < 0 || g.holes[(size_t) c.tunes].enabled))
                worst = std::max (worst, std::abs (predictedCents (g, c, lowestHz)));
        if (worst < 1.5)
            return true;
    }
    return false;
}
} // namespace

namespace
{
// Sets one hole's diameter (positions retuned for each try) so a cross-fingering is in tune.
// E.g. a smaller top hole vents less, so the covered holes below it pull the cross-fingered note further down.
bool tuneDiameter (Geometry& g, double lowestHz, const ChartFingering& cross)
{
    auto& diameter = g.holes[(size_t) cross.tunesDiameter].diameter;
    const double original = diameter;
    const Geometry originalGeometry = g;
    auto crossError = [&] (double d)
    {
        diameter = d;
        if (! tunePositions (g, lowestHz))
            return 1.0e9;
        return predictedCents (g, cross, lowestHz);
    };

    // Scan diameters for a sign change of the error, then bisect.
    double lo = 0.0, hi = 0.0, errorLo = 0.0;
    double previousDiameter = 0.0, previousError = 1.0e9;
    for (double d = std::max (0.0015, 0.2 * original); d <= original * 1.6; d += 0.1 * original)
    {
        const double error = crossError (d);
        if (error < 1.0e8 && previousError < 1.0e8 && (error > 0.0) != (previousError > 0.0))
        {
            lo = previousDiameter;
            errorLo = previousError;
            hi = d;
            break;
        }
        previousDiameter = d;
        previousError = error;
    }

    if (hi > 0.0)
    {
        for (int iteration = 0; iteration < 25 && hi - lo > 0.00002; ++iteration)
        {
            const double mid = 0.5 * (lo + hi);
            const double errorMid = crossError (mid);
            if (errorMid > 1.0e8)
                break;
            if ((errorMid > 0.0) == (errorLo > 0.0))
            {
                lo = mid;
                errorLo = errorMid;
            }
            else
            {
                hi = mid;
            }
        }
        if (std::abs (crossError (0.5 * (lo + hi))) < 5.0)
            return true;
    }

    // The hole's size alone can't put the cross-fingering in tune: keep it and tune the rest.
    g = originalGeometry;
    tunePositions (g, lowestHz);
    return false;
}
} // namespace

bool tuneToScale (Geometry& g, double lowestHz, bool tuneCrossFingerings)
{
    std::vector<const ChartFingering*> crosses;
    if (tuneCrossFingerings)
        for (const auto& c : chartFor (g.chart))
            if (c.tunesDiameter >= 0 && g.holes[(size_t) c.tunesDiameter].enabled)
                crosses.push_back (&c);

    if (crosses.empty())
        return tunePositions (g, lowestHz);

    // Several cross-fingerings, each with its own hole size: take turns until they all hold.
    bool ok = true;
    for (int round = 0; round < (crosses.size() > 1 ? 3 : 1); ++round)
    {
        ok = true;
        for (const auto* cross : crosses)
            ok = tuneDiameter (g, lowestHz, *cross) && ok;
    }
    for (const auto* cross : crosses)
        ok = ok && std::abs (predictedCents (g, *cross, lowestHz)) < 5.0;
    return ok;
}

} // namespace whistle
