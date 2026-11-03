// Frequency-domain acoustics of a fipple flute geometry (transfer-matrix method): the impedance the jet sees at
// the window, and from it the resonances that a given set of hole openings gives. Used for the tuning table
// and for placing holes; the sound itself comes from WhistleModel, a time-domain simulation of the same
// geometry. Nothing here decides which note is played.

#pragma once

#include "WhistleGeometry.h"

#include <complex>
#include <string>

namespace whistle
{

// How open each hole and the foot are: 0 = closed, 1 = fully open. Disabled (plugged) holes are ignored.
struct Openness
{
    std::array<double, kNumHoles> holes {};
    double foot = 1.0;

    static Openness allClosed() { return {}; }

    // From a pattern in hole order (LT L1 L2 L3 R1 R2 R3 R4, then optionally the bell): x = covered,
    // o = open, h = half covered, p = pinched (a small crack open, 25 %), - = not used (open).
    static Openness fromPattern (const char* pattern);
};

// Impedance seen at the window: the bore with its holes plus the window opening itself (Pa s / m^3).
std::complex<double> windowImpedance (const Geometry&, const Openness&, double hz);

// Resonance frequencies (impedance minima at the window) between fMin and fMax, lowest first.
// For a flute-like instrument these are where it can sound: the first is the low register,
// the second the overblown register.
std::vector<double> resonances (const Geometry&, const Openness&, double fMin = 150.0, double fMax = 5000.0);

// The holes a chart's fingers refer to, in order. The recorder chart uses all eight holes by name (LT first);
// the whistle chart's six fingers are the instrument's enabled front holes from the top, so the same whistle
// fingerings work whether the whistle is on WARBL's L1-R3 or L2-R4 sensors.
std::vector<int> chartHoles (const Geometry&);

// A fingering chart entry: the note a fingering should give, and which part of the instrument the tuner
// adjusts to get it. Patterns and tuned holes are in chart fingers (see chartHoles()).
struct ChartFingering
{
    static constexpr int kTunesBore = -2;
    static constexpr int kTunesNothing = -1;

    const char* name;     // In the chart's own key (D for whistles, C for recorders).
    const char* pattern;  // One character per chart finger: x covered, o open, h half, p pinched (25 %).
    int semitones;        // Above the lowest note.
    int reg;              // 1 = low register, 2 = second octave, 3 = third octave.
    int tunes = kTunesNothing;          // Finger whose hole's position is set by this note, or the bore length.
    int tunesDiameter = kTunesNothing;  // Optional: finger whose hole's diameter is set by this note (cross-fingerings).

    Openness openness (const Geometry&) const;

    // The hole a finger maps to on this instrument, or -1.
    static int holeFor (const Geometry&, int finger);

    // The pattern in hole order (LT L1 L2 L3 R1 R2 R3 R4), '-' for holes the chart doesn't use.
    std::string holePattern (const Geometry&) const;
};

const std::vector<ChartFingering>& chartFor (FingeringChart);

// Cents from the target note to the nearest resonance the geometry gives for this fingering.
double predictedCents (const Geometry&, const ChartFingering&, double lowestHz);

// Moves the bore length and hole positions so the instrument's chart notes that tune something are in tune
// (equal temperament, lowest note `lowestHz`). Keeps hole diameters, except that with `tuneCrossFingerings`
// a chart note marked tunesDiameter also sets that hole's size (e.g. the whistle's cross-fingered C natural
// sets L1's diameter). Returns false if it couldn't converge.
bool tuneToScale (Geometry&, double lowestHz, bool tuneCrossFingerings = false);

} // namespace whistle
