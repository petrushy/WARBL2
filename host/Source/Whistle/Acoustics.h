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
// lowest note (all holes covered) is `lowestHz`. Keeps hole diameters. Returns false if it couldn't converge.
bool tuneToScale (Geometry&, double lowestHz);

} // namespace whistle
