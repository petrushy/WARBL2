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

struct Hole
{
    float openBelow = 0.05f;  // Coverage (0 = open, 1 = covered at calibration) below which the hole is fully open.
    float closedAt = 1.0f;    // Coverage at which the hole is fully closed. Lower = more sensitive.
    float curve = 64.0f;

    // 0 = open, 1 = closed.
    float closedness (float coverage) const
    {
        const float x = (coverage - openBelow) / std::max (0.01f, closedAt - openBelow);
        return applyCurve (x, curve);
    }
};

} // namespace response
