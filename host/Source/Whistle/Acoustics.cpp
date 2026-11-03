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

Complex windowImpedance (const Geometry& g, const Openness& open, double hz)
{
    const double omega = 2.0 * kPi * hz;

    // Start at the open foot (unflanged pipe radiation) and work up to the window.
    const double aFoot = g.radiusAt (g.boreLength);
    const double z0Foot = kAirDensity * kSpeedOfSound / (kPi * aFoot * aFoot);
    const double kaFoot = omega / kSpeedOfSound * aFoot;
    Complex z = z0Foot * (0.25 * kaFoot * kaFoot + j * 0.6133 * kaFoot);

    // Points where something happens, from the foot up: holes, then the window at x = 0.
    std::array<int, kNumHoles> order {};
    for (int i = 0; i < kNumHoles; ++i)
        order[(size_t) i] = i;
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
        z = 1.0 / (1.0 / z + holeAdmittance (g, hole, juce::jlimit (0.0, 1.0, open[(size_t) hole]), omega));
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

Openness scaleFingering (int closed)
{
    Openness open {};
    for (int i = 0; i < kNumHoles; ++i)
        open[(size_t) i] = i < closed ? 0.0 : 1.0;
    return open;
}

bool tuneToScale (Geometry& g, double lowestHz)
{
    static const int majorScale[kNumHoles + 1] = { 0, 2, 4, 5, 7, 9, 11 };  // Semitones above the lowest note.

    auto firstResonance = [&] (const Openness& open, double guessHz)
    {
        const auto found = resonances (g, open, guessHz * 0.7, guessHz * 1.4);
        return found.empty() ? 0.0 : found.front();
    };

    // Solve f(value) = target for one length by the secant method; pitch falls as the length grows.
    auto solve = [&] (double& value, double lo, double hi, const Openness& open, double target)
    {
        // Resonances come from a 2-cent scan refined by interpolation, so aim for 0.5 cents and accept 2.
        double x0 = value, f0 = firstResonance (open, target);
        double x1 = juce::jlimit (lo, hi, value * (f0 > 0.0 ? f0 / target : 1.0));
        double bestValue = value, bestError = 1.0e9;
        for (int iteration = 0; iteration < 30; ++iteration)
        {
            value = x1;
            const double f1 = firstResonance (open, target);
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
    };

    auto targetFor = [&] (int closed) { return lowestHz * std::pow (2.0, majorScale[kNumHoles - closed] / 12.0); };

    // Moving a hole slightly changes the notes below it (its closed chimney is part of their bore), so repeat.
    for (int pass = 0; pass < 6; ++pass)
    {
        // Lowest note: bore length, all holes covered. Then each hole from the bottom up, with every hole below it open.
        if (! solve (g.boreLength, 0.05, 1.0, scaleFingering (kNumHoles), lowestHz))
            return false;

        for (int hole = kNumHoles - 1; hole >= 0; --hole)
        {
            const double below = hole + 1 < kNumHoles ? g.holes[(size_t) hole + 1].position : g.boreLength;
            const double lo = 0.01, hi = below - g.holes[(size_t) hole].diameter;
            if (! solve (g.holes[(size_t) hole].position, lo, hi, scaleFingering (hole), targetFor (hole)))
                return false;
        }

        double worst = 0.0;
        for (int closed = 0; closed <= kNumHoles; ++closed)
        {
            const double f = firstResonance (scaleFingering (closed), targetFor (closed));
            worst = std::max (worst, std::abs (1200.0 * std::log2 (f / targetFor (closed))));
        }
        if (worst < 1.5)
            return true;
    }
    return false;
}

} // namespace whistle
