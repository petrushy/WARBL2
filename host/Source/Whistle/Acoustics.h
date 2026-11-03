// Frequency-domain acoustics of a whistle geometry (transfer-matrix method): the impedance the jet sees at
// the window, and from it the resonances that a given set of hole openings gives. Used for the tuning table
// and for placing holes; the sound itself comes from WhistleModel, a time-domain simulation of the same
// geometry. Nothing here decides which note is played.

#pragma once

#include "WhistleGeometry.h"

#include <complex>

namespace whistle
{

// openness: 0 = closed, 1 = fully open, for each hole.
using Openness = std::array<double, kNumHoles>;

// Impedance seen at the window: the bore with its holes plus the window opening itself (Pa s / m^3).
std::complex<double> windowImpedance (const Geometry&, const Openness&, double hz);

// Resonance frequencies (impedance minima at the window) between fMin and fMax, lowest first.
// For a flute-like instrument these are where it can sound: the first is the low register,
// the second the overblown register.
std::vector<double> resonances (const Geometry&, const Openness&, double fMin = 150.0, double fMax = 5000.0);

// Openness for the plain scale fingering with the top `closed` holes covered (6 = all covered).
Openness scaleFingering (int closed);

// Moves the bore length and hole positions so the scale fingerings give an equal-tempered major scale whose
// lowest note (all holes covered) is `lowestHz`. Keeps hole diameters, except that with `tuneCrossFingering`
// the top hole's (L1) diameter is also chosen so the cross-fingered flat seventh (oxxooo, C natural on a D
// whistle) is in tune. Returns false if it couldn't converge.
bool tuneToScale (Geometry&, double lowestHz, bool tuneCrossFingering = false);

// The cross-fingering for the flat seventh: top hole open, the next two covered, the rest open.
Openness crossFingeredSeventh();

// The fingerings of a standard whistle chart, with the note each should give.
struct ChartFingering
{
    const char* name;      // For a D whistle; the same fingerings give the same scale degrees in other keys.
    const char* pattern;   // Top hole first, x = covered, o = open.
    int semitones;         // Above the lowest note.
    int reg;               // 1 = low register, 2 = second (overblown).
    Openness openness() const;
};

const std::vector<ChartFingering>& standardChart();

// Cents from the target note to the nearest resonance the geometry gives for this fingering.
double predictedCents (const Geometry&, const ChartFingering&, double lowestHz);

} // namespace whistle
