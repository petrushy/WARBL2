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
    // By default the model gets the pressure actually blown (1:1) up to the sensor's ceiling: its 12-bit reading
    // tops out at 4095 counts, ~3050 above the usual ambient reading of ~1040, at ~1.66 Pa per count.
    float inputMin = 10.0f;     // Pa measured. Below this: no breath (also rejects sensor noise).
    float inputMax = 5000.0f;   // Pa measured that gives the full output.
    float curve = 64.0f;
    float outputMax = 5000.0f;  // Pa sent to the model at full input.

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

struct Hole
{
    float openBelow = 0.05f;  // Coverage (0 = open, 1 = covered at calibration) below which the hole is fully open.
    float closedAt = 1.0f;    // Coverage at which the hole is fully closed. Lower = more sensitive.
    float curve = 64.0f;      // Power curve: exponent; S-curve: steepness (both on WARBL's 0-127 scale, 64 = straight)
    bool sCurve = true;       // S-curve (flat at both ends) instead of a power curve
    float centre = 0.5f;      // S-curve: where between "open below" and "closed at" the steep part is

    // 0 = open, 1 = closed.
    float closedness (float coverage) const
    {
        const float x = (coverage - openBelow) / std::max (0.01f, closedAt - openBelow);
        return sCurve ? applySCurve (x, curveExponent (curve), centre) : applyCurve (x, curve);
    }
};

} // namespace response
