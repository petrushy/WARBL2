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
    rate = 2.0 * hostSampleRate;

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
    radiation.fill (0.0f);
    decimatorBuffer.fill (0.0f);
    topPressure = lastSource = footX1 = footY1 = flowIntoBore = lastJetFlow = lastRadiatedFlow = dcX1 = dcY1 = 0.0f;
    windwaySpeed = 0.0f;
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
    out.jetGrowth = (float) std::exp (0.38 * g.windowLength / g.windwayHeight);  // Jet instability growth (alpha_i ~ 0.38/h)
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
    rampRemaining = rampSamples;
}

float WhistleModel::tick()
{
    if (rampRemaining > 0)
    {
        for (int i = 0; i < kNumHoles; ++i)
            current.openness[(size_t) i] += opennessStep[(size_t) i];
        current.footOpenness += footStep;
        current.mouthPressure += pressureStep;
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
    const float steadyEdgeNoise = amount * kEdgeNoise * rho * jetSpeed * jetSpeed * std::sqrt (jetSpeed / 20.0f) * band;  // Pa
    const float turbulence = amount * 0.15f * band;  // In jet half-widths: a slight unsteadiness, not roughness

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
    const float deflection = cfg.jetGrowth * h * filtered / speed + turbulence * jetHalfWidth * std::min (1.0f, jetSpeed / 20.0f);
    const float jetFlow = cfg.windowWidth * jetHalfWidth * jetSpeed * flowShare * std::tanh ((deflection - cfg.edgeOffset) / jetHalfWidth);

    // The edge turbulence comes in bursts as the jet sweeps across the edge (pulsed noise, locked to the tone),
    // which the ear hears as breathiness in the note rather than as a separate hiss. Below the playing threshold
    // the jet hardly moves off the edge, so the airy noise stays steady.
    const float acrossEdge = 1.0f / std::cosh ((deflection - cfg.edgeOffset) / jetHalfWidth);
    const float pulsing = noisePulsing.load (std::memory_order_relaxed);
    const float edgeNoise = steadyEdgeNoise * (1.0f - pulsing + pulsing * acrossEdge * acrossEdge);
    const float jetFlowRate = (jetFlow - lastJetFlow) * (float) rate;
    lastJetFlow = jetFlow;

    // Pressure the jet produces across the window (jet drive), minus losses from vortices shed at the edge.
    const float jetDrive = rho * cfg.dipoleLength / cfg.windowArea * jetFlowRate;
    const float vortexLoss = -0.5f * rho * (windowVelocity / 0.6f) * std::abs (windowVelocity / 0.6f);
    const float source = jetDrive + vortexLoss + edgeNoise + testSource;

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

    float radiatedTotal = 0.0f;
    auto radiate = [&] (int junction, float flow)
    {
        const float d = cfg.radiationDelay[(size_t) junction];
        const int i = (int) d;
        const float f = d - (float) i;
        radiation[(size_t) ((radiationRead + i) & (kRadiationSize - 1))] += flow * (1.0f - f);
        radiation[(size_t) ((radiationRead + i + 1) & (kRadiationSize - 1))] += flow * f;
    };
    // Out of the window: the air the bore pushes out, plus the edge noise that escapes directly.
    radiate (0, -flowIntoBore + directNoise.load (std::memory_order_relaxed) * kDirectNoise * edgeNoise * cfg.windowArea / (rho * c));

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

            // Open branch Y_o = open (r + s mr) / (s (mi mr s + (mi + mr) r)), covered part s C.
            // p / sum = G (mi mr s^2 + (mi + mr) r s) / D, open-branch flow / sum = G open (r + s mr) / D, with
            // D = C mi mr s^3 + (G mi mr + C (mi + mr) r) s^2 + (G (mi + mr) r + open mr) s + open r.
            double num[4], flowNum[4], den[4];
            bilinear (0.0, gj * (mi + mr) * r, gj * mi * mr, 0.0, num);
            bilinear (gj * open * r, gj * open * mr, 0.0, 0.0, flowNum);
            bilinear (open * r, gj * (mi + mr) * r + open * mr, gj * mi * mr + compliance * (mi + mr) * r, compliance * mi * mr, den);

            auto& st = holeState[(size_t) jn];  // sum x1 x2 x3, p y1 y2 y3, flow f1 f2 f3
            const double x0 = sum;
            const double pNew = (num[0] * x0 + num[1] * st[0] + num[2] * st[1] + num[3] * st[2]
                                 - den[1] * st[3] - den[2] * st[4] - den[3] * st[5]) / den[0];
            const double flow = (flowNum[0] * x0 + flowNum[1] * st[0] + flowNum[2] * st[1] + flowNum[3] * st[2]
                                 - den[1] * st[6] - den[2] * st[7] - den[3] * st[8]) / den[0];
            st = { x0, st[0], st[1], pNew, st[3], st[4], flow, st[6], st[7] };
            p = (float) pNew;

            radiate (jn, (float) flow);
        }

        sendRight[(size_t) jn] = p - b;
        sendLeft[(size_t) jn - 1] = p - a;
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
    radiate (n, cfg.area[(size_t) n - 1] / (rho * c) * (arrivingFoot - reflectedFoot));

    // 6. Send the new waves into the sections.
    for (int k = 0; k < n; ++k)
    {
        right[(size_t) k].push (sendRight[(size_t) k]);
        left[(size_t) k].push (sendLeft[(size_t) k]);
    }

    // Radiated sound: pressure at the listener is proportional to the rate of change of the radiated flow.
    radiatedTotal = radiation[(size_t) radiationRead];
    radiation[(size_t) radiationRead] = 0.0f;
    radiationRead = (radiationRead + 1) & (kRadiationSize - 1);

    float out = rho / (4.0f * (float) kPi * (float) kListenerDistance) * (radiatedTotal - lastRadiatedFlow) * (float) rate;
    lastRadiatedFlow = radiatedTotal;

    jetVelocity.store (jetSpeed, std::memory_order_relaxed);
    return out;
}

void WhistleModel::render (float* out, int numSamples)
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
        for (int phase = 0; phase < 2; ++phase)
        {
            // DC blocker on the internal-rate signal, then into the decimation filter.
            const float x = tick();
            const float y = x - dcX1 + 0.9995f * dcY1;
            dcX1 = x;
            dcY1 = y;
            decimatorBuffer[(size_t) decimatorWrite] = y;
            decimatorWrite = (decimatorWrite + 1) % kDecimatorTaps;
        }

        float sample = 0.0f;
        for (int t = 0; t < kDecimatorTaps; ++t)
            sample += decimatorCoeffs[(size_t) t] * decimatorBuffer[(size_t) ((decimatorWrite + t) % kDecimatorTaps)];

        sample *= gain;
        if (! std::isfinite (sample))  // Extreme settings can make the simulation unstable: start again from rest.
        {
            resetState();
            sample = 0.0f;
        }
        levelState += 0.001f * (sample * sample - levelState);
        out[i] += sample;
    }

    level.store (std::sqrt (levelState), std::memory_order_relaxed);
}

} // namespace whistle
