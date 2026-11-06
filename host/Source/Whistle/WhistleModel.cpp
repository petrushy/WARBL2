#include "WhistleModel.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace whistle
{

namespace
{
constexpr double kPi = juce::MathConstants<double>::pi;
constexpr float rho = (float) kAirDensity;
constexpr float c = (float) kSpeedOfSound;
constexpr double kMaxSectionLength = 0.03;
constexpr double kMaxHeadSectionLength = 0.008;  // Finer sections where a head section changes the diameter.
constexpr double kListenerDistance = 0.5;  // m in front of the window, for the radiation delays and level.
constexpr float kEdgeNoise = 0.15f;       // Edge pressure fluctuation at 20 m/s, as a fraction of rho U^2 (Breath noise 1)
constexpr float kDirectNoise = 1.0f;      // Share of the edge noise radiated straight from the window (at 1)
constexpr float kMaxSlowdown = 3.0f;  // The slow jet's growth stops rising below a third of the reference speed
constexpr float kKinematicViscosity = (float) (kAirViscosity / kAirDensity);  // m^2/s

// Amplitude attenuation per metre from viscous and thermal wall losses at frequency hz (tube radius a), as in
// Acoustics (Keefe). The same boundary layers slow the sound down: the extra phase per metre equals this.
double wallAttenuation (double hz, double a, double wallLoss)
{
    const double omega = 2.0 * kPi * hz;
    return omega / kSpeedOfSound * 1.045 / (a * std::sqrt (kAirDensity * omega / kAirViscosity)) * wallLoss;
}

// A biquad (b0 b1 b2 a1 a2) for what the wall does to a wave over one section, on top of its delay length / c:
// exp(-(1 + j) alpha(f) length), loss and dispersion together. Both grow as sqrt(f), which a one-pole lowpass
// can't follow (it had a fraction of the loss at the notes' frequencies). Fitted over 150 Hz-6 kHz by
// linearised least squares (Levy), weighted for relative error, with one Sanathanan-Koerner pass to undo
// Levy's bias: within about +-6 % of the loss from 300 Hz to 4 kHz.
std::array<float, 5> fitWallLoss (double length, double a, double wallLoss, double rate)
{
    constexpr int kPoints = 80, n = 5;
    double x[n] = { 1.0, 0.0, 0.0, 0.0, 0.0 };
    for (int pass = 0; pass < 2; ++pass)
    {
        double normal[n][n + 1] {};
        for (int i = 0; i < kPoints; ++i)
        {
            const double hz = 150.0 * std::pow (6000.0 / 150.0, i / (kPoints - 1.0));
            const double alphaL = wallAttenuation (hz, a, wallLoss) * length;
            const std::complex<double> target = std::exp (std::complex<double> (-alphaL, -alphaL));
            const std::complex<double> z1 = std::polar (1.0, -2.0 * kPi * hz / rate), z2 = z1 * z1;
            const double weight = 1.0 / (std::abs (1.0 + x[3] * z1 + x[4] * z2) * std::max (alphaL, 1.0e-12));
            // Residual = row . (b0 b1 b2 a1 a2) - target, as real and imaginary parts.
            const std::complex<double> row[n] = { 1.0, z1, z2, -target * z1, -target * z2 };
            for (int part = 0; part < 2; ++part)
            {
                double r[n];
                const double t = weight * (part == 0 ? target.real() : target.imag());
                for (int k = 0; k < n; ++k)
                    r[k] = weight * (part == 0 ? row[k].real() : row[k].imag());
                for (int p = 0; p < n; ++p)
                {
                    for (int q = 0; q < n; ++q)
                        normal[p][q] += r[p] * r[q];
                    normal[p][n] += r[p] * t;
                }
            }
        }

        // Gauss-Jordan elimination with partial pivoting.
        for (int col = 0; col < n; ++col)
        {
            int pivot = col;
            for (int row = col + 1; row < n; ++row)
                if (std::abs (normal[row][col]) > std::abs (normal[pivot][col]))
                    pivot = row;
            for (int k = 0; k <= n; ++k)
                std::swap (normal[col][k], normal[pivot][k]);
            if (std::abs (normal[col][col]) < 1.0e-300)
                return { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            for (int row = 0; row < n; ++row)
            {
                if (row == col)
                    continue;
                const double factor = normal[row][col] / normal[col][col];
                for (int k = col; k <= n; ++k)
                    normal[row][k] -= factor * normal[col][k];
            }
        }
        for (int k = 0; k < n; ++k)
            x[k] = normal[k][n] / normal[k][k];
    }

    // Stable poles only (inside the stability triangle, with a margin); otherwise just the loss at 1 kHz.
    const bool stable = std::abs (x[4]) < 0.9999 && std::abs (x[3]) < 1.0 + x[4] - 1.0e-6;
    if (! stable || ! std::isfinite (x[0] + x[1] + x[2] + x[3] + x[4]))
        return { (float) std::exp (-wallAttenuation (1000.0, a, wallLoss) * length), 0.0f, 0.0f, 0.0f, 0.0f };
    return { (float) x[0], (float) x[1], (float) x[2], (float) x[3], (float) x[4] };
}
} // namespace

float WhistleModel::DelayLine::read (float delay) const
{
    // Value `delay` samples ago; the newest stored sample is 1 sample old.
    const int whole = (int) delay;
    const int m = whole - 1;
    const float f = delay - (float) m;  // In [1, 2)
    const float h0 = -(f - 1.0f) * (f - 2.0f) * (f - 3.0f) / 6.0f;
    const float h1 = f * (f - 2.0f) * (f - 3.0f) * 0.5f;
    const float h2 = -f * (f - 1.0f) * (f - 3.0f) * 0.5f;
    const float h3 = f * (f - 1.0f) * (f - 2.0f) / 6.0f;
    auto at = [&] (int age) { return buffer[(size_t) ((write - age) & (kLineSize - 1))]; };
    return h0 * at (m) + h1 * at (m + 1) + h2 * at (m + 2) + h3 * at (m + 3);
}

WhistleModel::WhistleModel()
{
    geometry = presets().front();
}

void WhistleModel::prepare (double hostSampleRate)
{
    hostRate = hostSampleRate;
    // The model needs ~96 kHz for accurate short delays (the bore's sections, the jet): at 44.1/48 kHz it runs at
    // twice the host rate and decimates; at 88.2 kHz and above it runs at the host rate.
    oversampling = hostSampleRate < 80000.0 ? 2 : 1;
    rate = oversampling * hostSampleRate;
    slowCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * rate)));
    lockCoeff = (float) (1.0 - std::exp (-1.0 / (0.005 * rate)));

    // Decimation lowpass (Blackman-windowed sinc) at ~0.45 of the host rate.
    const double cutoff = 0.225;  // Of the internal rate
    double sum = 0.0;
    for (int i = 0; i < kDecimatorTaps; ++i)
    {
        const double x = i - (kDecimatorTaps - 1) * 0.5;
        const double sinc = std::abs (x) < 1e-9 ? 2.0 * cutoff : std::sin (2.0 * kPi * cutoff * x) / (kPi * x);
        const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * i / (kDecimatorTaps - 1)) + 0.08 * std::cos (4.0 * kPi * i / (kDecimatorTaps - 1));
        decimatorCoeffs[(size_t) i] = (float) (sinc * w);
        sum += sinc * w;
    }
    for (auto& v : decimatorCoeffs)
        v = (float) (v / sum);

    buildConfig (geometry, config);
    hasPending = false;
    resetState();
    current = target = {};
    rampRemaining = 0;
}

void WhistleModel::resetState()
{
    for (auto& d : right) d = {};
    for (auto& d : left) d = {};
    for (auto& f : lossRight) f = {};
    for (auto& f : lossLeft) f = {};
    for (auto& st : holeState)
        st.fill (0.0f);
    jetLine.fill (0.0f);
    noiseLow0 = noiseLow = noiseHigh = 0.0f;
    noiseFilterCountdown = 0;
    for (auto& st : jetState)
        st.fill (0.0f);
    jetFilterCountdown = 0;
    jetBoostLow = 0.0f;
    for (auto& r : radiation)
        r.fill (0.0f);
    for (auto& d : decimatorBuffer)
        d.fill (0.0f);
    lastRadiatedFlow = dcX1 = dcY1 = {};
    topPressure = lastSource = footX1 = footY1 = flowIntoBore = lastJetFlow = 0.0f;
    windwaySpeed = slowJetSpeed = jetLock = 0.0f;
    levelState = 0.0f;
}

void WhistleModel::setVoicing (const Voicing& v)
{
    jetStrouhal = v.jetStrouhal;
    jetConvection = v.jetConvection;
    breathNoise = v.breathNoise;
    outputGainDb = v.outputGainDb;
    directNoise = v.directNoise;
    windwayMass = v.windwayMass;
    windwayFriction = v.windwayFriction;
    vortexLoss = v.vortexLoss;
    attackChiff = v.attackChiff;
    fingerGapDamping = v.fingerGapDamping;
    jetGain = v.jetGain;
    slowJetGrowth = v.slowJetGrowth;
    jetStages = juce::jlimit (1, 2, v.jetStages);
}

Voicing WhistleModel::getVoicing() const
{
    Voicing v;
    v.jetStrouhal = jetStrouhal.load();
    v.jetConvection = jetConvection.load();
    v.breathNoise = breathNoise.load();
    v.outputGainDb = outputGainDb.load();
    v.directNoise = directNoise.load();
    v.windwayMass = windwayMass.load();
    v.windwayFriction = windwayFriction.load();
    v.vortexLoss = vortexLoss.load();
    v.attackChiff = attackChiff.load();
    v.fingerGapDamping = fingerGapDamping.load();
    v.jetGain = jetGain.load();
    v.slowJetGrowth = slowJetGrowth.load();
    v.jetStages = jetStages.load();
    return v;
}

void WhistleModel::setGeometry (const Geometry& g)
{
    Config built;
    buildConfig (g, built);
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    geometry = g;
    pending = built;
    hasPending = true;
}

void WhistleModel::buildConfig (const Geometry& g, Config& out) const
{
    out = {};

    // Junction positions: window (0), each hole, foot (L); long stretches are split into short sections.
    std::vector<std::pair<double, int>> points { { 0.0, -1 } };
    const bool hasHead = g.headLength > 0.001 && g.headLength < g.boreLength - 0.01;
    if (hasHead)
        points.push_back ({ g.headLength, -1 });
    for (int i = 0; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            points.push_back ({ juce::jlimit (0.005, g.boreLength - 0.005, g.holes[(size_t) i].position), i });
    std::sort (points.begin(), points.end());
    points.push_back ({ g.boreLength, -1 });

    // Split into sections; on a very long bore, use longer sections so they fit in kMaxSegments.
    std::vector<std::pair<double, int>> junctions;
    for (double sectionScale = 1.0;; sectionScale *= 1.25)
    {
        junctions.clear();
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            junctions.push_back (points[i]);
            const double span = points[i + 1].first - points[i].first;
            const bool inHead = hasHead && points[i].first < g.headLength;
            const int pieces = (int) std::ceil (span / ((inHead ? kMaxHeadSectionLength : kMaxSectionLength) * sectionScale));
            for (int p = 1; p < pieces; ++p)
                junctions.push_back ({ points[i].first + span * p / pieces, -1 });
        }
        junctions.push_back (points.back());
        if ((int) junctions.size() - 1 <= kMaxSegments)
            break;
    }

    out.numSegments = std::min ((int) junctions.size() - 1, kMaxSegments);
    out.holeAt.fill (-1);

    const double footRadius = g.radiusAt (g.boreLength);
    for (int k = 0; k < out.numSegments; ++k)
    {
        const double x0 = junctions[(size_t) k].first, x1 = junctions[(size_t) k + 1].first;
        double length = x1 - x0;
        const double a = g.radiusAt (0.5 * (x0 + x1));

        // Open end correction 0.61 a (as in Acoustics). The foot's reflection lowpass already delays low
        // frequencies by a / c, there and back: that is a / 2 of extra length, so the section adds the rest.
        if (k == out.numSegments - 1)
            length += (0.6133 - 0.5) * footRadius;

        out.loss[(size_t) k] = fitWallLoss (length, a, g.wallLoss, rate);
        out.delay[(size_t) k] = (float) std::max (2.0, length / kSpeedOfSound * rate);
        out.area[(size_t) k] = (float) (kPi * a * a);
    }

    for (int j = 1; j < out.numSegments; ++j)
        out.holeAt[(size_t) j] = junctions[(size_t) j].second;
    for (int j = 0; j <= out.numSegments; ++j)
        out.junctionX[(size_t) j] = (float) junctions[(size_t) j].first;

    // Sound reaches a listener in front of the window later from points further down the tube.
    for (int j = 0; j <= out.numSegments; ++j)
    {
        const double x = junctions[(size_t) j].first;
        const double path = std::sqrt (kListenerDistance * kListenerDistance + x * x) - kListenerDistance;
        out.radiationDelay[(size_t) j] = (float) (path / kSpeedOfSound * rate);
    }

    for (int i = 0; i < kNumHoles; ++i)
    {
        const double b = 0.5 * g.holes[(size_t) i].diameter;
        const double area = kPi * b * b;
        const double outer = 0.5 * b;  // With rho c / A in parallel, this gives the radiation resistance rho w^2 / (4 pi c)
        out.holeInnerMass[(size_t) i] = kAirDensity * std::max (g.holeEffectiveLength (i) - outer, 0.0001) / area;
        out.holeOuterMass[(size_t) i] = kAirDensity * outer / area;
        out.holeRadiationResistance[(size_t) i] = kAirDensity * kSpeedOfSound / area;
        out.holeCompliance[(size_t) i] = area * std::max (g.wallThickness, 0.0003) / (kAirDensity * kSpeedOfSound * kSpeedOfSound);  // As in Acoustics
        // The finger as in the physical hole response (SensorResponse.h): the gap adds a chimney length
        // b^2 ln(R / b) / (2 d), R the fingertip's radius; air flows through it radially (Poiseuille between plates:
        // 12 mu (R - b) / (2 pi b d^3)).
        const double fingertip = std::max (0.008, 1.5 * b);
        out.holeGapHeight[(size_t) i] = b * b * std::log (fingertip / b) / (2.0 * g.holeEffectiveLength (i));
        out.holeGapViscous[(size_t) i] = 12.0 * kAirViscosity * (fingertip - b) / (2.0 * kPi * b);
    }

    // Window end: the air in the window (area Aw, effective length tw) is a mass the jet's pressure has to
    // move, in series with the bore. Open-end-like at low frequencies, nearly closed at high ones.
    out.windowArea = (float) g.windowArea();
    out.boreImpedance = (float) (kAirDensity * kSpeedOfSound / out.area[0]);
    const double inertance = kAirDensity * g.windowEffectiveLength() / g.windowArea();
    out.windowMass = (float) (inertance * 2.0 * rate / out.boreImpedance);

    // Open foot: reflection -1 at low frequencies, falling above ka ~ 1.
    const double kf = std::tan (kSpeedOfSound / footRadius / (2.0 * rate));
    out.footB = (float) (kf / (1.0 + kf));
    out.footA = (float) ((1.0 - kf) / (1.0 + kf));
    out.bellClosesFoot = g.bellClosesFoot;

    out.windwayHeight = (float) g.windwayHeight;
    out.windwayLength = (float) g.windwayLength;
    out.windowLength = (float) g.windowLength;
    out.windowWidth = (float) g.windowWidth;
    out.edgeOffset = (float) g.edgeOffset;
    out.jetGrowthExponent = (float) (0.38 * g.windowLength / g.windwayHeight);  // Jet instability growth (alpha_i ~ 0.38/h)
    out.jetGrowth = std::exp (out.jetGrowthExponent);
    out.referenceJetSpeed = (float) (presetLowestNote (g) * g.windwayHeight / 0.02);
    out.dipoleLength = (float) (4.0 / kPi * std::sqrt (2.0 * g.windwayHeight * g.windowLength));
}

void WhistleModel::setControls (const Controls& controls, double rampSeconds)
{
    target = controls;
    const int rampSamples = std::max (1, (int) (rampSeconds * rate));
    for (int i = 0; i < kNumHoles; ++i)
        opennessStep[(size_t) i] = (target.openness[(size_t) i] - current.openness[(size_t) i]) / rampSamples;
    footStep = (target.footOpenness - current.footOpenness) / rampSamples;
    pressureStep = (target.mouthPressure - current.mouthPressure) / rampSamples;
    edgeStep = (target.edgeShift - current.edgeShift) / rampSamples;
    rampRemaining = rampSamples;
}

void WhistleModel::tick (float& windowOut, float& bodyOut)
{
    if (rampRemaining > 0)
    {
        for (int i = 0; i < kNumHoles; ++i)
            current.openness[(size_t) i] += opennessStep[(size_t) i];
        current.footOpenness += footStep;
        current.mouthPressure += pressureStep;
        current.edgeShift += edgeStep;
        if (--rampRemaining == 0)
            current = target;
    }

    const auto& cfg = config;
    const int n = cfg.numSegments;
    std::array<float, kMaxSegments> arriveRight {}, arriveLeft {}, sendRight {}, sendLeft {};

    // 1. Waves arriving at the ends of each section.
    for (int k = 0; k < n; ++k)
    {
        const auto& loss = cfg.loss[(size_t) k];
        arriveRight[(size_t) k] = lossRight[(size_t) k].process (right[(size_t) k].read (cfg.delay[(size_t) k]), loss);  // At junction k+1
        arriveLeft[(size_t) k] = lossLeft[(size_t) k].process (left[(size_t) k].read (cfg.delay[(size_t) k]), loss);     // At junction k
    }

    // 2. Jet. Breath pressure gives the jet speed; the sound in the window deflects the jet, the deflection
    //    grows on the way to the edge and arrives there after the jet's travel time.
    //
    //    The breath accelerates the air in the windway (mass rho L per unit area) against the pressure it takes
    //    to leave at that speed (Bernoulli): rho L dU/dt = p - rho U^2 / 2. Time constant L / U: ~1 ms when
    //    blowing softly, ~0.3 ms when blowing hard. Solved implicitly, which is stable for any length; with
    //    no mass the speed follows the pressure at once.
    const float pressure = std::max (0.0f, current.mouthPressure);
    const float massLength = windwayMass.load (std::memory_order_relaxed) * cfg.windwayLength;  // m
    if (massLength > 1.0e-6f)
    {
        // U' + a U'^2 = b, with a = dt / 2L, b = U + p dt / (rho L); root written to avoid cancellation.
        const float dt = 1.0f / (float) rate;
        const float a = dt / (2.0f * massLength);
        const float b = windwaySpeed + pressure * dt / (rho * massLength);
        windwaySpeed = 2.0f * b / (1.0f + std::sqrt (1.0f + 4.0f * a * b));
    }
    else
    {
        windwaySpeed = std::sqrt (2.0f * pressure / rho);
    }
    const float jetSpeed = windwaySpeed;
    const float speed = std::max (jetSpeed, 0.5f);
    const float h = cfg.windwayHeight;

    // Friction: boundary layers grow along the windway walls (laminar, Blasius: displacement thickness
    // 1.72 sqrt(nu L / U) on each wall) and carry less flow than the jet's core, at most down to fully
    // developed flow (Poiseuille, 2/3 of the core speed on average). Blowing softly loses the most.
    float flowShare = 1.0f;
    const float friction = windwayFriction.load (std::memory_order_relaxed);
    if (friction > 0.0f)
    {
        const float displacement = std::min (h / 6.0f, 1.72f * std::sqrt (kKinematicViscosity * cfg.windwayLength / speed));
        flowShare = 1.0f - friction * 2.0f * displacement / h;
    }
    const float windowVelocity = flowIntoBore / cfg.windowArea;  // Into the pipe

    jetLine[(size_t) jetWrite] = windowVelocity;
    jetWrite = (jetWrite + 1) & (kJetLineSize - 1);
    const float convection = jetConvection.load (std::memory_order_relaxed);
    const float travel = juce::jlimit (2.0f, (float) kJetLineSize - 4.0f, cfg.windowLength / (convection * speed) * (float) rate);
    const int whole = (int) travel;
    const float frac = travel - (float) whole;
    const float delayed = (1.0f - frac) * jetLine[(size_t) ((jetWrite - whole) & (kJetLineSize - 1))]
                          + frac * jetLine[(size_t) ((jetWrite - whole - 1) & (kJetLineSize - 1))];

    // Turbulence. One noise signal, in a band centred on a frequency that rises with the jet speed (f ~ U / h:
    // blowing harder sounds brighter), drives two things:
    //  - edge noise: a pressure fluctuation of order rho U^2 where the jet hits the edge. It enters the bore at the
    //    window like the tone does, so the bore's resonances colour it (below the playing threshold it is an airy
    //    whoosh at the fingered note); part of it also escapes straight out of the window (see radiate below).
    //  - a small wobble of the jet's deflection, which makes the tone itself slightly unsteady.
    if ((noiseFilterCountdown--) <= 0)
    {
        noiseFilterCountdown = 8;
        const float centre = juce::jlimit (300.0f, 0.3f * (float) rate, noiseStrouhal.load (std::memory_order_relaxed) * speed / h);
        noiseLowA = 1.0f - std::exp (-2.0f * (float) kPi * 1.5f * centre / (float) rate);  // Lowpass above the centre (2 poles)
        noiseHighA = 1.0f - std::exp (-2.0f * (float) kPi * 0.5f * centre / (float) rate);  // Highpass an octave below
        noiseNorm = 1.0f / std::sqrt (noiseLowA / (2.0f - noiseLowA));  // Keeps the band's level independent of its centre
    }
    noiseLow0 += noiseLowA * (gauss (rng) - noiseLow0);
    noiseLow += noiseLowA * (noiseLow0 - noiseLow);
    noiseHigh += noiseHighA * (noiseLow - noiseHigh);
    const float band = (noiseLow - noiseHigh) * noiseNorm * 1.4f;  // Second pole: ~1.4x to keep the level

    const float amount = breathNoise.load (std::memory_order_relaxed);
    // Edge noise: of order rho U^2, growing a little faster (U^2.5): the edge-dipole law (U^3) let the hiss
    // overtake the tone at the high pressures of the top notes.

    // Attack chiff: a jet that has just started (its speed well above its ~20 ms average, as after a tongued
    // attack, not in a slow swell) rolls up a starting vortex and is turbulent, and until the sound in the pipe
    // steers it across the edge (the oscillation has built up) the edge hears mostly that turbulence. Only a fast
    // jet becomes turbulent: none below a Reynolds number U h / nu of 1000 (a 1 mm windway: ~200 Pa), fully above
    // 3000 (~1100 Pa), so gentle attacks start clean and hard ones chiff.
    slowJetSpeed += slowCoeff * (jetSpeed - slowJetSpeed);
    const float starting = jetSpeed > 1.0f ? std::max (0.0f, 1.0f - slowJetSpeed / jetSpeed) : 0.0f;
    const float reynolds = jetSpeed * h / kKinematicViscosity;
    const float turbulent = juce::jlimit (0.0f, 1.0f, (reynolds - 1000.0f) / 2000.0f);
    const float chiff = 1.0f + attackChiff.load (std::memory_order_relaxed) * turbulent * starting * (1.0f - jetLock);

    const float steadyEdgeNoise = chiff * amount * kEdgeNoise * rho * jetSpeed * jetSpeed * std::sqrt (jetSpeed / 20.0f) * band;  // Pa
    const float turbulence = chiff * amount * 0.15f * band;  // In jet half-widths: a slight unsteadiness, not roughness

    // A jet amplifies disturbances most around one frequency, set by its speed and thickness (Strouhal number
    // f h / U), and less above and below. Blowing harder moves that band up, which is what lets the higher
    // register take over. Band-pass (unity gain, no phase shift at the centre), retuned every few samples.
    if ((jetFilterCountdown--) <= 0)
    {
        jetFilterCountdown = 8;
        const double centre = juce::jlimit (50.0, 0.4 * rate, (double) jetStrouhal.load (std::memory_order_relaxed) * speed / h);
        const double w0 = 2.0 * kPi * centre / rate;
        const double alpha = std::sin (w0) / (2.0 * jetQ.load (std::memory_order_relaxed));
        const double a0 = 1.0 + alpha;
        jetB0 = (float) (alpha / a0);
        jetA1 = (float) (-2.0 * std::cos (w0) / a0);
        jetA2 = (float) ((1.0 - alpha) / a0);

        // Growth exponent alpha_i W at this speed: a slower jet fits more wavelengths of the disturbance into the
        // window and amplifies it more (limited to 3x slower than the reference, below which the note hardly sounds).
        const float slow = slowJetGrowth.load (std::memory_order_relaxed);
        const float exponent = slow > 0.0f ? cfg.jetGrowthExponent * std::pow (std::min (kMaxSlowdown, cfg.referenceJetSpeed / speed), slow)
                                           : cfg.jetGrowthExponent;
        // A fast jet grows less at every frequency; a slow one more, but only for long waves (high resonances
        // lie above its band, where it doesn't amplify: boosting them too makes soft breath squeak).
        jetAmplification = jetGain.load (std::memory_order_relaxed) * std::exp (std::min (exponent, cfg.jetGrowthExponent));
        jetSlowBoost = std::exp (std::max (0.0f, exponent - cfg.jetGrowthExponent)) - 1.0f;
        jetBoostA = (float) (1.0 - std::exp (-w0));  // One-pole lowpass at the band centre
    }
    float filtered = delayed;
    for (int stage = 0; stage < jetStages.load (std::memory_order_relaxed); ++stage)
    {
        auto& st = jetState[(size_t) stage];
        const float y = jetB0 * (filtered - st[1]) - jetA1 * st[2] - jetA2 * st[3];
        st[1] = st[0];
        st[0] = filtered;
        st[3] = st[2];
        st[2] = y;
        filtered = y;
    }

    const float jetHalfWidth = 0.4f * h;
    // The slow jet's extra growth is for long waves (at and below the band centre), not for high resonances.
    jetBoostLow += jetBoostA * (filtered - jetBoostLow);
    const float deflection = jetAmplification * h * (filtered + jetSlowBoost * jetBoostLow) / speed + turbulence * jetHalfWidth * std::min (1.0f, jetSpeed / 20.0f);
    const float edge = cfg.edgeOffset + current.edgeShift;  // Where the jet's centre line meets the edge
    const float jetFlow = cfg.windowWidth * jetHalfWidth * jetSpeed * flowShare * std::tanh ((deflection - edge) / jetHalfWidth);

    // The edge turbulence comes in bursts as the jet sweeps across the edge (pulsed noise, locked to the tone),
    // which the ear hears as breathiness in the note rather than as a separate hiss. Below the playing threshold
    // the jet hardly moves off the edge, so the airy noise stays steady.
    const float acrossEdge = 1.0f / std::cosh ((deflection - edge) / jetHalfWidth);
    jetLock += lockCoeff * (std::min (1.0f, std::abs (deflection - edge) / jetHalfWidth) - jetLock);
    const float pulsing = noisePulsing.load (std::memory_order_relaxed);
    const float edgeNoise = steadyEdgeNoise * (1.0f - pulsing + pulsing * acrossEdge * acrossEdge);
    const float jetFlowRate = (jetFlow - lastJetFlow) * (float) rate;
    lastJetFlow = jetFlow;

    // Pressure the jet produces across the window (jet drive), minus losses from vortices shed at the edge.
    const float jetDrive = rho * cfg.dipoleLength / cfg.windowArea * jetFlowRate;
    const float vortexDrop = -vortexLoss.load (std::memory_order_relaxed) * 0.5f * rho * (windowVelocity / 0.6f) * std::abs (windowVelocity / 0.6f);
    const float source = jetDrive + vortexDrop + edgeNoise + testSource;
    if (jetLog != nullptr)
        jetLog->insert (jetLog->end(), { deflection - edge, jetFlow, jetDrive, vortexDrop });

    // 3. Window end. The jet's pressure pushes the air in the window (mass M) into the bore:
    //    p_top = source - M dU/dt, with p_top = p+ + p- and U = (p+ - p-) / Z0, solved for the outgoing p+
    //    with the trapezoidal rule.
    const float arrivingTop = arriveLeft[0];
    const float m = cfg.windowMass, z0 = cfg.boreImpedance;
    const float outgoing = (source + lastSource - topPressure - (1.0f - m) * arrivingTop + m * z0 * flowIntoBore) / (1.0f + m);
    sendRight[0] = outgoing;
    topPressure = outgoing + arrivingTop;
    lastSource = source;
    flowIntoBore = (outgoing - arrivingTop) / z0;
    if (windowFlowLog != nullptr)
        windowFlowLog->push_back (flowIntoBore);

    auto radiate = [&] (int junction, float flow)  // Junction 0 is the window; the others the holes and the foot.
    {
        auto& buffer = radiation[junction == 0 ? 0 : 1];
        const float d = cfg.radiationDelay[(size_t) junction];
        const int i = (int) d;
        const float f = d - (float) i;
        buffer[(size_t) ((radiationRead + i) & (kRadiationSize - 1))] += flow * (1.0f - f);
        buffer[(size_t) ((radiationRead + i + 1) & (kRadiationSize - 1))] += flow * f;
    };
    // Out of the window: the air the bore pushes out, plus the edge noise that escapes directly.
    radiate (0, testWindowRadiation * -flowIntoBore + directNoise.load (std::memory_order_relaxed) * kDirectNoise * edgeNoise * cfg.windowArea / (rho * c));

    // 4. Junctions inside the bore. Pressure there is the area-weighted sum of the arriving waves, filtered by
    //    the hole's shunt: its open part is a short tube to the outside that lets low frequencies escape (an
    //    inertance, and radiating: the outer part of its air mass in parallel with the resistance rho c / A, which
    //    takes energy out as Acoustics' radiation resistance does), its covered part a small closed chimney whose
    //    air volume flattens the notes below it (a compliance). With G = (S1 + S2) / (rho c) the bore's
    //    admittance at the junction and Y the hole's, p = sum G / (G + Y): third order in s, bilinear transform
    //    per sample (openness changes). The open branch's own flow is what radiates.
    const double bilinearK = 2.0 * rate;
    auto bilinear = [bilinearK] (double c0, double c1, double c2, double c3, double* out)  // c0 + c1 s + c2 s^2 + c3 s^3
    {
        const double k = bilinearK, a0 = c0, a1 = c1 * k, a2 = c2 * k * k, a3 = c3 * k * k * k;
        out[0] = a0 + a1 + a2 + a3;
        out[1] = 3.0 * a0 + a1 - a2 - 3.0 * a3;
        out[2] = 3.0 * a0 - a1 - a2 + 3.0 * a3;
        out[3] = a0 - a1 + a2 - a3;
    };
    std::array<float, kMaxSegments + 1> junctionPressure {};
    junctionPressure[0] = topPressure;
    for (int jn = 1; jn < n; ++jn)
    {
        const float a = arriveRight[(size_t) jn - 1], b = arriveLeft[(size_t) jn];
        const float s1 = cfg.area[(size_t) jn - 1], s2 = cfg.area[(size_t) jn];
        const float sum = 2.0f * (s1 * a + s2 * b) / (s1 + s2);
        float p = sum;

        const int hole = cfg.holeAt[(size_t) jn];
        if (hole >= 0)
        {
            const double open = juce::jlimit (0.0, 1.0, (double) current.openness[(size_t) hole]);
            const double mi = cfg.holeInnerMass[(size_t) hole], mr = cfg.holeOuterMass[(size_t) hole];
            const double r = cfg.holeRadiationResistance[(size_t) hole];
            // A fully open hole keeps a trace of compliance: avoids a pole-zero pair exactly at the Nyquist frequency.
            const double compliance = cfg.holeCompliance[(size_t) hole] * std::max (1.0 - open, 0.02);
            const double gj = (s1 + s2) / (rho * c);

            // A partly open hole lets air out through the thin gap under the finger: a viscous resistance rv in series
            // with the open part, huge when the gap is a fraction of a millimetre (it damps the resonance of the hole's
            // air mass with the trapped chimney air, which otherwise "plops" as a finger lifts), negligible once the
            // hole is more than about a quarter open. Scaled by the openness like the rest of the open branch (orv).
            double orv = 0.0;
            const double gapDamping = fingerGapDamping.load (std::memory_order_relaxed);
            if (open > 0.0 && open < 1.0 && gapDamping > 0.0)
            {
                const double gap = cfg.holeGapHeight[(size_t) hole] / (1.0 / open - 1.0);
                orv = std::min (1.0e15, gapDamping * open * cfg.holeGapViscous[(size_t) hole] / (gap * gap * gap));
            }

            // Open branch Y_o = open (r + s mr) / (mi mr s^2 + ((mi + mr) r + orv mr) s + orv r), covered part s C.
            // p / sum = G Do / D, open-branch flow / sum = G open (r + s mr) / D, with Do the open branch's
            // denominator above and D = (G + s C) Do + open (r + s mr).
            const double d0 = orv * r, d1 = (mi + mr) * r + orv * mr, d2 = mi * mr;
            double num[4], flowNum[4], den[4];
            bilinear (gj * d0, gj * d1, gj * d2, 0.0, num);
            bilinear (gj * open * r, gj * open * mr, 0.0, 0.0, flowNum);
            bilinear (gj * d0 + open * r, gj * d1 + compliance * d0 + open * mr, gj * d2 + compliance * d1, compliance * d2, den);

            auto& st = holeState[(size_t) jn];  // sum x1 x2 x3, p y1 y2 y3, flow f1 f2 f3
            const double x0 = sum;
            const double pNew = (num[0] * x0 + num[1] * st[0] + num[2] * st[1] + num[3] * st[2]
                                 - den[1] * st[3] - den[2] * st[4] - den[3] * st[5]) / den[0];
            const double flow = (flowNum[0] * x0 + flowNum[1] * st[0] + flowNum[2] * st[1] + flowNum[3] * st[2]
                                 - den[1] * st[6] - den[2] * st[7] - den[3] * st[8]) / den[0];
            st = { x0, st[0], st[1], pNew, st[3], st[4], flow, st[6], st[7] };
            p = (float) pNew;

            radiate (jn, testBodyRadiation * (float) flow);
        }

        sendRight[(size_t) jn] = p - b;
        sendLeft[(size_t) jn - 1] = p - a;
        junctionPressure[(size_t) jn] = p;
    }

    // 5. Foot: open (radiating, reflection -1 at low frequencies) or, if the bell sensor may close it, a blend
    //    towards a closed end (reflection +1, nothing radiated).
    const float arrivingFoot = arriveRight[(size_t) n - 1];
    const float lowpassed = cfg.footB * (arrivingFoot + footX1) + cfg.footA * footY1;
    footX1 = arrivingFoot;
    footY1 = lowpassed;
    const float footOpen = cfg.bellClosesFoot ? juce::jlimit (0.0f, 1.0f, current.footOpenness) : 1.0f;
    const float reflectedFoot = -footOpen * lowpassed + (1.0f - footOpen) * arrivingFoot;
    sendLeft[(size_t) n - 1] = reflectedFoot;
    junctionPressure[(size_t) n] = arrivingFoot + reflectedFoot;
    radiate (n, testBodyRadiation * cfg.area[(size_t) n - 1] / (rho * c) * (arrivingFoot - reflectedFoot));

    // 6. Send the new waves into the sections.
    for (int k = 0; k < n; ++k)
    {
        right[(size_t) k].push (sendRight[(size_t) k]);
        left[(size_t) k].push (sendLeft[(size_t) k]);
    }

    // Radiated sound: pressure at the listener is proportional to the rate of change of the radiated flow.
    float out[2];
    for (size_t part = 0; part < 2; ++part)  // Window, then holes and foot
    {
        const float radiated = radiation[part][(size_t) radiationRead];
        radiation[part][(size_t) radiationRead] = 0.0f;
        out[part] = rho / (4.0f * (float) kPi * (float) kListenerDistance) * (radiated - lastRadiatedFlow[part]) * (float) rate;
        lastRadiatedFlow[part] = radiated;
    }
    radiationRead = (radiationRead + 1) & (kRadiationSize - 1);
    windowOut = out[0];
    bodyOut = out[1];

    if (scope.enabled.load (std::memory_order_relaxed))
    {
        const int w = scope.written.load (std::memory_order_relaxed);
        const auto i = (size_t) (w & (Scope::size - 1));
        scope.deflection[i].store (deflection, std::memory_order_relaxed);
        scope.jetFlow[i].store (jetFlow, std::memory_order_relaxed);
        scope.windowFlow[i].store (flowIntoBore, std::memory_order_relaxed);
        scope.drive[i].store (jetDrive, std::memory_order_relaxed);
        scope.vortex[i].store (vortexDrop, std::memory_order_relaxed);
        scope.sound[i].store (out[0] + out[1], std::memory_order_relaxed);
        scope.written.store (w + 1, std::memory_order_release);

        constexpr float smoothing = 0.002f;  // ~5 ms at 96 kHz
        for (int j = 0; j <= n; ++j)
        {
            const float p = junctionPressure[(size_t) j];
            junctionPower[(size_t) j] += smoothing * (p * p - junctionPower[(size_t) j]);
            junctionCorrelation[(size_t) j] += smoothing * (p * topPressure - junctionCorrelation[(size_t) j]);
        }
        if ((scopeCountdown--) <= 0)
        {
            scopeCountdown = 64;
            scope.rate.store ((float) rate, std::memory_order_relaxed);
            scope.mouthPressure.store (pressure, std::memory_order_relaxed);
            scope.jetSpeed.store (jetSpeed, std::memory_order_relaxed);
            scope.flowShare.store (flowShare, std::memory_order_relaxed);
            scope.travelSamples.store (travel, std::memory_order_relaxed);
            scope.growthExponent.store (std::log (jetAmplification / std::max (0.01f, jetGain.load (std::memory_order_relaxed))),
                                        std::memory_order_relaxed);
            scope.bandCentre.store (jetStrouhal.load (std::memory_order_relaxed) * speed / h, std::memory_order_relaxed);
            scope.slowBoost.store (jetSlowBoost, std::memory_order_relaxed);
            scope.edge.store (edge, std::memory_order_relaxed);
            const int count = std::min (n + 1, Scope::maxJunctions);
            for (int j = 0; j < count; ++j)
            {
                scope.junctionX[(size_t) j].store (cfg.junctionX[(size_t) j], std::memory_order_relaxed);
                scope.junctionRms[(size_t) j].store (std::sqrt (junctionPower[(size_t) j]), std::memory_order_relaxed);
                scope.junctionSign[(size_t) j].store (junctionCorrelation[(size_t) j] >= 0.0f ? 1.0f : -1.0f, std::memory_order_relaxed);
            }
            scope.junctions.store (count, std::memory_order_relaxed);
        }
    }

    jetVelocity.store (jetSpeed, std::memory_order_relaxed);
}

void WhistleModel::render (float* out, int numSamples)
{
    render (out, out, numSamples);
}

void WhistleModel::render (float* windowOut, float* bodyOut, int numSamples)
{
    if (hasPending.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (pendingLock);
        if (lock.isLocked())
        {
            const bool sameShape = pending.numSegments == config.numSegments;
            config = pending;
            hasPending = false;
            if (! sameShape)  // Different sections: start the bore from rest.
            {
                for (auto& d : right) d = {};
                for (auto& d : left) d = {};
                for (auto& st : holeState)
                    st.fill (0.0f);
            }
        }
    }

    // 1.0 in the output is 2 Pa at the listener (100 dB SPL).
    const float gain = std::pow (10.0f, outputGainDb.load() / 20.0f) * 0.5f;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int phase = 0; phase < oversampling; ++phase)
        {
            // DC blocker on the internal-rate signals, then into the decimation filters.
            float x[2];
            tick (x[0], x[1]);
            for (size_t source = 0; source < 2; ++source)
            {
                const float y = x[source] - dcX1[source] + 0.9995f * dcY1[source];
                dcX1[source] = x[source];
                dcY1[source] = y;
                decimatorBuffer[source][(size_t) decimatorWrite] = y;
            }
            decimatorWrite = (decimatorWrite + 1) % kDecimatorTaps;
        }

        float sample[2] = { 0.0f, 0.0f };
        if (oversampling == 1)
        {
            // No decimation: the newest samples (the filter's delay doesn't matter, so take them at once).
            const auto newest = (size_t) ((decimatorWrite + kDecimatorTaps - 1) % kDecimatorTaps);
            sample[0] = decimatorBuffer[0][newest];
            sample[1] = decimatorBuffer[1][newest];
        }
        else
        {
            for (int t = 0; t < kDecimatorTaps; ++t)
            {
                const auto at = (size_t) ((decimatorWrite + t) % kDecimatorTaps);
                sample[0] += decimatorCoeffs[(size_t) t] * decimatorBuffer[0][at];
                sample[1] += decimatorCoeffs[(size_t) t] * decimatorBuffer[1][at];
            }
        }

        sample[0] *= gain;
        sample[1] *= gain;
        if (! std::isfinite (sample[0] + sample[1]))  // Extreme settings can make the simulation unstable: start again from rest.
        {
            resetState();
            sample[0] = sample[1] = 0.0f;
        }
        const float sum = sample[0] + sample[1];
        levelState += 0.001f * (sum * sum - levelState);
        windowOut[i] += sample[0];
        bodyOut[i] += sample[1];
    }

    level.store (std::sqrt (levelState), std::memory_order_relaxed);
}

} // namespace whistle
