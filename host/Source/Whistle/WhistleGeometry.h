// The physical description of a whistle (fipple flute). Both the real-time model (WhistleModel) and the
// tuning calculator (Acoustics) are built from this, so changing the instrument means changing these numbers.
// All lengths are in metres.

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace whistle
{

constexpr int kNumHoles = 6;  // Ordered from the top: L1, L2, L3, R1, R2, R3.
constexpr double kSpeedOfSound = 345.0;  // Air inside a played whistle is a little warmer than the room.
constexpr double kAirDensity = 1.2;
constexpr double kAirViscosity = 1.85e-5;

// WARBL tone hole sensor index for each whistle hole (see warbl::holeName()).
constexpr std::array<int, kNumHoles> kSensorForHole { 7, 6, 5, 4, 3, 2 };

struct ToneHole
{
    double position = 0.0;  // Centre of the hole, measured from the window edge (labium) towards the foot.
    double diameter = 0.0;
};

struct Geometry
{
    juce::String name;

    // Bore: from the window to the open foot. The diameter changes linearly from top to foot.
    double boreLength = 0.27;
    double boreDiameterTop = 0.012;
    double boreDiameterFoot = 0.012;

    // Optional head section (e.g. a narrower bore around the fipple, as in Goldie whistles): from the window
    // down to headLength the diameter goes linearly from headDiameter to the bore's own diameter there.
    // headLength = 0 means no head section.
    double headLength = 0.0;
    double headDiameter = 0.012;

    std::array<ToneHole, kNumHoles> holes {};
    double wallThickness = 0.0005;  // Sets the height of the tone hole chimneys.

    // Fipple (mouthpiece). The windway ends in a flat jet that crosses the window and hits the edge (labium).
    double windwayHeight = 0.001;    // Jet thickness at the flue exit (h).
    double windowLength = 0.005;     // Flue exit to the edge (W): sets how long the jet takes to cross.
    double windowWidth = 0.008;      // Width of the jet and window.
    double edgeOffset = 0.0001;      // Edge position relative to the jet centre line (y0); positive = into the pipe.

    double wallLoss = 1.0;  // Viscous and thermal losses at the bore wall: 1 = smooth metal, more for rough walls.

    double radiusAt (double x) const
    {
        const auto body = [this] (double at)
        {
            const double t = juce::jlimit (0.0, 1.0, at / boreLength);
            return 0.5 * (boreDiameterTop + t * (boreDiameterFoot - boreDiameterTop));
        };
        if (headLength > 0.0 && x < headLength)
        {
            const double t = juce::jlimit (0.0, 1.0, x / headLength);
            return 0.5 * headDiameter + t * (body (headLength) - 0.5 * headDiameter);
        }
        return body (x);
    }

    double windowArea() const { return windowLength * windowWidth; }

    // Effective acoustic length of the window opening (its "plug" of air plus end corrections on both sides).
    double windowEffectiveLength() const
    {
        const double equivalentRadius = std::sqrt (windowArea() / juce::MathConstants<double>::pi);
        return 0.001 + 1.7 * equivalentRadius;
    }

    // Effective length of an open tone hole: chimney plus inner and outer end corrections (Keefe 1990).
    double holeEffectiveLength (int hole) const
    {
        const double b = 0.5 * holes[(size_t) hole].diameter;
        const double a = radiusAt (holes[(size_t) hole].position);
        return std::max (wallThickness, 0.0003) + b * (1.4 - 0.58 * (b / a) * (b / a));
    }
};

struct Material
{
    juce::String name;
    double wallThickness;
    double wallLoss;
};

// Material mostly matters through wall thickness (tone hole chimney height) and surface roughness.
const std::vector<Material>& materials();

// Starting points; hole positions are tuned for an equal-tempered D major scale.
const std::vector<Geometry>& presets();

} // namespace whistle
