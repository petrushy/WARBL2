// How sensor readings become the instrument model's controls: an input range, a curve and an output range,
// as in the WARBL Config Tool. Curves use WARBL's 0-127 scale: 64 is linear, lower is logarithmic (more
// response at the start), higher is exponential (more response at the end).

#pragma once

#include <algorithm>
#include <cmath>

namespace response
{

// WARBL's curveValToExponent(): 0..127 -> exponent 0.25..1..4.
inline float curveExponent (float value)
{
    const float v = value - 64.0f;
    return v >= 0.0f ? 3.0f * v / 63.0f + 1.0f : 0.75f * (64.0f + v) / 64.0f + 0.25f;
}

inline float applyCurve (float x, float curve)
{
    return std::pow (std::clamp (x, 0.0f, 1.0f), curveExponent (curve));
}

struct Breath
{
    // Defaults chosen by playing WARBL (2026-11-04): about twice the blown pressure, more so when blowing
    // softly (the curve), so normal WARBL breath reaches the whistle's second register; full output at 2.5 kPa
    // measured. (The sensor's 12-bit reading tops out ~3050 counts above ambient, at ~1.66 Pa per count.)
    float inputMin = 10.0f;     // Pa measured. Below this: no breath (also rejects sensor noise).
    float inputMax = 2500.0f;   // Pa measured that gives the full output.
    float curve = 34.0f;
    float outputMax = 4930.0f;  // Pa sent to the model at full input.

    float map (float measuredPa) const
    {
        if (measuredPa <= inputMin)
            return 0.0f;
        const float x = (measuredPa - inputMin) / std::max (1.0f, inputMax - inputMin);
        return outputMax * applyCurve (x, curve);
    }
};

// S-curve through (0,0), (centre, 0.5) and (1,1): flat at both ends, steep in the middle. steepness 1 is a
// straight line (with centre 0.5), more is a sharper S, less than 1 an inverted S.
inline float applySCurve (float x, float steepness, float centre)
{
    x = std::clamp (x, 0.0f, 1.0f);
    const float c = std::clamp (centre, 0.05f, 0.95f);
    const float shifted = std::pow (x, std::log (0.5f) / std::log (c));  // Moves `centre` to 0.5
    const float a = std::pow (shifted, steepness), b = std::pow (1.0f - shifted, steepness);
    return a + b > 0.0f ? a / (a + b) : 0.0f;
}

// A finger hovering over a tone hole, from WARBL's infrared reading. The reflected light falls off with distance
// roughly as (d0 / (d + d0))^2, so the coverage x (0..1 between "open below" and "closed at") gives the finger's
// height d = d0 (1 / sqrt(x) - 1). Air leaving the hole squeezes out through the ring between the finger and the
// rim (area 2 pi b d, flowing out to the fingertip's edge R): an extra air mass, the same as a longer chimney by
// b^2 ln(R / b) / (2 d). The hole's openness is its own effective length over the total, so a hovering finger
// still flattens the note, and a big hole needs the finger higher than a small one.
struct FingerHole
{
    float radius = 0.0f;           // m (0: not known, use the curve)
    float effectiveLength = 0.0f;  // m, open hole with no finger (chimney and end corrections)
};

inline float fingerOpenness (float x, float rangeMm, const FingerHole& hole)
{
    if (x >= 1.0f)
        return 0.0f;
    if (x <= 0.0f)
        return 1.0f;
    constexpr float seal = 0.0001f;  // m: a finger this close seals the hole (skin gives a little)
    const float height = rangeMm * 0.001f * (1.0f / std::sqrt (x) - 1.0f) - seal;
    if (height <= 0.0f)
        return 0.0f;
    const float b = hole.radius;
    const float fingertip = std::max (0.008f, 1.5f * b);  // Radius of the finger pad over the hole
    const float gap = b * b * std::log (fingertip / b) / (2.0f * height);
    return hole.effectiveLength / (hole.effectiveLength + gap);
}

struct Hole
{
    float openBelow = 0.05f;  // Coverage (0 = open, 1 = covered at calibration) below which the hole is fully open.
    float closedAt = 1.0f;    // Coverage at which the hole is fully closed. Lower = more sensitive.
    float curve = 82.0f;      // Power curve: exponent; S-curve: steepness (both on WARBL's 0-127 scale, 64 = straight)
    bool sCurve = true;       // S-curve (flat at both ends) instead of a power curve
    float centre = 0.51f;     // S-curve: where between "open below" and "closed at" the steep part is
    // (Steepness and centre chosen by playing WARBL, 2026-11-04.)
    bool fingerHeight = false;  // Physical: from the finger's height over each hole (see fingerOpenness), not a curve
    float fingerRange = 2.5f;   // mm: d0, the infrared sensor's distance scale

    // 0 = open, 1 = closed. The physical shape needs the hole's size; without it the curve is used.
    float closedness (float coverage, const FingerHole& hole = {}) const
    {
        const float x = (coverage - openBelow) / std::max (0.01f, closedAt - openBelow);
        if (fingerHeight && hole.radius > 0.0f)
            return 1.0f - fingerOpenness (x, fingerRange, hole);
        return sCurve ? applySCurve (x, curveExponent (curve), centre) : applyCurve (x, curve);
    }
};

} // namespace response
