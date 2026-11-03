// Pitch of the end of a signal, by normalised autocorrelation. Used to check what the model plays.

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace whistle
{

// Fundamental frequency of the last ~0.1 s of x (Hz), or 0 if not clearly periodic; clarity is the peak
// correlation (1 = perfectly periodic).
inline double measurePitch (const std::vector<float>& x, double rate, double& clarity)
{
    clarity = 0.0;
    const size_t window = (size_t) (0.1 * rate);
    const int minLag = (int) (rate / 3000.0), maxLag = (int) (rate / 120.0);
    if (x.size() < window + (size_t) maxLag + 2)
        return 0.0;
    const size_t start = x.size() - window - (size_t) maxLag - 2;

    double energy = 0.0;
    for (size_t i = 0; i < window; ++i)
        energy += (double) x[start + i] * x[start + i];
    if (energy <= 1e-20)
        return 0.0;

    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag + 1; ++lag)
    {
        double sum = 0.0, e2 = 0.0;
        for (size_t i = 0; i < window; ++i)
        {
            sum += (double) x[start + i] * x[start + i + (size_t) lag];
            e2 += (double) x[start + i + (size_t) lag] * x[start + i + (size_t) lag];
        }
        r[(size_t) lag] = sum / std::sqrt (energy * e2 + 1e-30);
    }

    double best = 0.0;
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
        best = std::max (best, r[(size_t) lag]);

    // First peak that is nearly as high as the best one (avoids picking a multiple of the period).
    for (int lag = minLag + 1; lag <= maxLag; ++lag)
    {
        if (r[(size_t) lag] >= 0.9 * best && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1])
        {
            const double denom = r[(size_t) lag - 1] - 2.0 * r[(size_t) lag] + r[(size_t) lag + 1];
            const double offset = denom < 0.0 ? 0.5 * (r[(size_t) lag - 1] - r[(size_t) lag + 1]) / denom : 0.0;
            clarity = r[(size_t) lag];
            return rate / (lag + offset);
        }
    }
    return 0.0;
}

} // namespace whistle
