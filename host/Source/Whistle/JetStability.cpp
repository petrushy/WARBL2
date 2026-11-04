#include "JetStability.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace whistle
{

namespace
{
using Complex = std::complex<double>;
const Complex nan (std::numeric_limits<double>::quiet_NaN(), 0.0);

// The profile (U0 = 1, h = 1) and its second derivative at y.
void profile (double y, double t, double& u, double& u2)
{
    const double norm = 2.0 * std::tanh (0.25 / t);
    const double a = (0.5 - y) / (2.0 * t), b = (0.5 + y) / (2.0 * t);
    const double ta = std::tanh (a), tb = std::tanh (b);
    u = (ta + tb) / norm;
    // d2/dy2 tanh((c -+ y) / 2t) = -2 sech^2 tanh / (4 t^2)
    u2 = (-2.0 * (1.0 - ta * ta) * ta - 2.0 * (1.0 - tb * tb) * tb) / (4.0 * t * t) / norm;
}

// Shooting residual for Rayleigh's equation phi'' = (alpha^2 + U'' / (U - c)) phi, c = omega / alpha. Outside
// the jet, where the flow has stopped, the solution must decay as exp(-alpha y); integrated inwards from there
// (where that solution is the one that grows, so it stays accurate) to the centre line, where the sinuous mode
// has phi'(0) = 0 (phi even). Returns phi'(0) / phi(0).
Complex residual (Complex alpha, double omega, double t)
{
    const Complex c = omega / alpha;
    const double edge = 0.5 + 20.0 * t + 2.0;
    const double dy = std::min (t / 12.0, 0.004);
    const int steps = (int) std::ceil (edge / dy);
    const double h = -edge / steps;  // Inwards

    auto second = [&] (double y, Complex phi)
    {
        double u, u2;
        profile (y, t, u, u2);
        return (alpha * alpha + u2 / (u - c)) * phi;
    };

    Complex phi = 1.0, dphi = -alpha;
    for (int i = 0; i < steps; ++i)
    {
        const double y = edge + i * h;
        const Complex k1 = second (y, phi);
        const Complex p2 = phi + 0.5 * h * dphi, d2 = dphi + 0.5 * h * k1;
        const Complex k2 = second (y + 0.5 * h, p2);
        const Complex p3 = phi + 0.5 * h * d2, d3 = dphi + 0.5 * h * k2;
        const Complex k3 = second (y + 0.5 * h, p3);
        const Complex p4 = phi + h * d3, d4 = dphi + h * k3;
        const Complex k4 = second (y + h, p4);
        phi += h / 6.0 * (dphi + 2.0 * d2 + 2.0 * d3 + d4);
        dphi += h / 6.0 * (k1 + 2.0 * k2 + 2.0 * k3 + k4);

        // Keep the numbers finite (only the ratio matters).
        const double size = std::abs (phi) + std::abs (dphi);
        if (size > 1.0e100)
        {
            phi /= size;
            dphi /= size;
        }
    }
    return dphi / phi;
}
} // namespace

std::complex<double> vortexSheetJetWavenumber (double omega)
{
    // Long waves: alpha^3 ~ -2 omega^2, on the branch that grows downstream (Re > 0, Im < 0).
    Complex k = std::pow (2.0 * omega * omega, 1.0 / 3.0) * std::polar (1.0, -std::acos (-1.0) / 3.0);
    for (int i = 0; i < 100; ++i)
    {
        const Complex th = std::tanh (0.5 * k), d = omega - k;
        const Complex f = d * d * th + omega * omega;
        const Complex df = -2.0 * d * th + d * d * 0.5 * (1.0 - th * th);
        const Complex step = f / df;
        k -= step;
        if (std::abs (step) < 1.0e-13)
            break;
    }
    return k;
}

std::complex<double> jetResponse (double omega, double windowOverH, double thetaOverH)
{
    const Complex alpha = jetWavenumber (omega, thetaOverH);
    if (! std::isfinite (alpha.real()))
        return nan;
    const Complex i (0.0, 1.0);
    const Complex eta = i / omega * (1.0 - std::exp (i * alpha * windowOverH));
    return std::conj (eta);
}

std::complex<double> jetWavenumber (double omega, double thetaOverH, std::complex<double> guess)
{
    const double t = std::clamp (thetaOverH, 0.002, 0.07);
    Complex k0 = guess == Complex (0.0, 0.0) ? vortexSheetJetWavenumber (omega) : guess;
    Complex k1 = k0 * Complex (1.01, 0.01);
    Complex r0 = residual (k0, omega, t), r1 = residual (k1, omega, t);
    for (int i = 0; i < 60; ++i)
    {
        if (! std::isfinite (r1.real()) || ! std::isfinite (r1.imag()) || r1 == r0)
            return nan;
        const Complex k2 = k1 - r1 * (k1 - k0) / (r1 - r0);
        k0 = k1;
        r0 = r1;
        k1 = k2;
        if (k1.real() <= 0.0)
            return nan;
        r1 = residual (k1, omega, t);
        if (std::abs (k1 - k0) < 1.0e-9 * std::abs (k1))
            return k1.imag() < 0.0 ? k1 : nan;  // A downstream-growing wave only
    }
    return nan;
}

} // namespace whistle
