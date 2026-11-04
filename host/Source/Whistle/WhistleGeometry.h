// The physical description of a fipple flute (whistle, recorder). Both the real-time model (WhistleModel) and
// the tuning calculator (Acoustics) are built from this, so changing the instrument means changing these numbers.
// All lengths are in metres.

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace whistle
{

// Tone holes, one per WARBL finger sensor, in this order: left thumb (on the back), L1-L3, R1-R4.
// An instrument uses some of them (e.g. a whistle L1-R3); the others are plugged (disabled).
constexpr int kNumHoles = 8;
constexpr std::array<const char*, kNumHoles> kHoleNames { "LT", "L1", "L2", "L3", "R1", "R2", "R3", "R4" };
constexpr double kSpeedOfSound = 345.0;  // Air inside a played whistle is a little warmer than the room.
constexpr double kAirDensity = 1.2;
constexpr double kAirViscosity = 1.85e-5;

// WARBL sensor index for each hole (see warbl::holeName()), and for the bell sensor at the foot.
constexpr std::array<int, kNumHoles> kSensorForHole { 8, 7, 6, 5, 4, 3, 2, 1 };
constexpr int kBellSensor = 0;

struct ToneHole
{
    bool enabled = false;   // A disabled hole is plugged: as if it weren't drilled.
    double position = 0.0;  // Centre of the hole, measured from the window edge (labium) towards the foot.
    double diameter = 0.0;
};

// Which fingering chart the tuning table, tuner and checks use (the sound never uses it).
enum class FingeringChart
{
    whistle,          // Six holes, L1-R3
    baroqueRecorder    // Thumb and seven holes, baroque fingering
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
    bool bellClosesFoot = false;    // The bell sensor covers the open foot (recorder technique).
    FingeringChart chart = FingeringChart::whistle;

    // Fipple (mouthpiece). The windway ends in a flat jet that crosses the window and hits the edge (labium).
    double windwayHeight = 0.001;    // Jet thickness at the flue exit (h).
    double windwayLength = 0.018;    // Mouth end to flue exit: the air the breath accelerates, and its friction.
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

// How the jet behaves (see WhistleModel) and the output level: part of an instrument's character.
struct Voicing
{
    float jetStrouhal = 0.02f;   // Frequency the jet amplifies most, as f h / U
    float jetConvection = 0.6f;  // Speed of disturbances along the jet, relative to the jet speed
    float breathNoise = 0.05f;   // Turbulence at the jet and edge (grows with jet speed)
    float outputGainDb = 0.0f;
    float directNoise = 0.0f;    // How much of the edge noise escapes straight from the window
    // The windway's physics, 1 = as computed from its size, 0 = off (jet speed follows the pressure at once,
    // no friction): kept adjustable because WARBL's own mouthpiece already smooths the breath a little.
    float windwayMass = 1.0f;      // The air in the windway has to be accelerated (~1 ms)
    float windwayFriction = 1.0f;  // Boundary layers on the windway walls carry less flow than the jet core
    // Loss from vortices shed where the sound flows round the edge, as a multiple of Verge's 1/2 rho (v / 0.6)^2.
    // It is what limits the low notes' loudness: lower = fuller, louder low notes (with more odd harmonics).
    float vortexLoss = 1.0f;
};

// Starting points, tuned (see WhistleGeometry.cpp).
const std::vector<Geometry>& presets();

// The voicing that goes with each preset (same order as presets()).
Voicing presetVoicing (size_t index);

// Puts the instrument's front holes (in order, keeping each hole's position and size) on WARBL's sensors
// starting at `firstHole` (1 = L1, 2 = L2, ...), e.g. a six-hole whistle on L2-R4 instead of L1-R3. The thumb
// hole is left as it is. Returns false (and changes nothing) if they don't fit.
bool moveFrontHoles (Geometry&, int firstHole);

// The first front hole in use (1 = L1 ...), or 0 if none.
int firstFrontHole (const Geometry&);

} // namespace whistle
