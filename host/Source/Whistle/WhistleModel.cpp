#include "WhistleModel.h"

#include <algorithm>
#include <cmath>

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

// Amplitude attenuation per metre from viscous and thermal wall losses at frequency hz (tube radius a).
double wallAttenuation (double hz, double a, double wallLoss)
{
    const double omega = 2.0 * kPi * hz;
    return omega / kSpeedOfSound * 1.045 / (a * std::sqrt (kAirDensity * omega / kAirViscosity)) * wallLoss;
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
    for (auto& f : lossRight) f.y1 = 0.0f;
    for (auto& f : lossLeft) f.y1 = 0.0f;
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
    levelState = 0.0f;
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

        // The foot reflection lowpass delays low frequencies by a/c; the open end correction is 0.61 a.
        if (k == out.numSegments - 1)
            length += (0.6133 - 1.0) * footRadius;

        // Wall losses as a one-pole lowpass matching the attenuation at 100 Hz and 4 kHz.
        const double gainLow = std::exp (-wallAttenuation (100.0, a, g.wallLoss) * length);
        const double gainHigh = std::exp (-wallAttenuation (4000.0, a, g.wallLoss) * length);
        const double r = gainHigh / gainLow;
        const double cosw = std::cos (2.0 * kPi * 4000.0 / rate);
        double pole = 0.0;
        if (r < 0.99999)
        {
            const double r2 = r * r, bq = 1.0 - r2 * cosw;
            pole = (bq - std::sqrt (std::max (0.0, bq * bq - (1.0 - r2) * (1.0 - r2)))) / (1.0 - r2);
        }
        out.lossA1[(size_t) k] = (float) pole;
        out.lossB0[(size_t) k] = (float) (gainLow * (1.0 - pole));

        const double lossDelay = pole / (1.0 - pole);  // The filter's own delay at low frequencies.
        out.delay[(size_t) k] = (float) std::max (2.0, length / kSpeedOfSound * rate - lossDelay);
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
        out.holeArea[(size_t) i] = (float) (kPi * b * b);
        out.holeLength[(size_t) i] = (float) g.holeEffectiveLength (i);
        out.holeChimney[(size_t) i] = (float) std::max (g.wallThickness, 0.0003);  // As in Acoustics
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
    out.windowLength = (float) g.windowLength;
    out.windowWidth = (float) g.windowWidth;
    out.edgeOffset = (float) g.edgeOffset;
    out.jetGrowth = (float) std::exp (0.38 * g.windowLength / g.windwayHeight);  // Jet instability growth (alpha_i ~ 0.38/h)
    out.dipoleLength = (float) (4.0 / kPi * std::sqrt (2.0 * g.windwayHeight * g.windowLength));
}

void WhistleModel::setControls (const Controls& controls)
{
    target = controls;
    const int rampSamples = std::max (1, (int) (0.002 * rate));  // One sensor frame period.
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
        lossRight[(size_t) k].b0 = cfg.lossB0[(size_t) k];
        lossRight[(size_t) k].a1 = cfg.lossA1[(size_t) k];
        lossLeft[(size_t) k].b0 = cfg.lossB0[(size_t) k];
        lossLeft[(size_t) k].a1 = cfg.lossA1[(size_t) k];
        arriveRight[(size_t) k] = lossRight[(size_t) k].process (right[(size_t) k].read (cfg.delay[(size_t) k]));  // At junction k+1
        arriveLeft[(size_t) k] = lossLeft[(size_t) k].process (left[(size_t) k].read (cfg.delay[(size_t) k]));     // At junction k
    }

    // 2. Jet. Breath pressure gives the jet speed; the sound in the window deflects the jet, the deflection
    //    grows on the way to the edge and arrives there after the jet's travel time.
    const float pressure = std::max (0.0f, current.mouthPressure);
    const float jetSpeed = std::sqrt (2.0f * pressure / rho);
    const float speed = std::max (jetSpeed, 0.5f);
    const float h = cfg.windwayHeight;
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
    const float jetFlow = cfg.windowWidth * jetHalfWidth * jetSpeed * std::tanh ((deflection - cfg.edgeOffset) / jetHalfWidth);

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
    const float source = jetDrive + vortexLoss + edgeNoise;

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
    //    inertance), its covered part a small closed chimney whose air volume flattens the notes below it (a
    //    compliance). In units of bore area: p = sum * s / (tau s^2 + s + w), with w = A_open c / (t_e (S1 + S2))
    //    and tau = A_covered t / (c (S1 + S2)); bilinear transform per sample.
    const float bilinear = 2.0f * (float) rate;
    for (int jn = 1; jn < n; ++jn)
    {
        const float a = arriveRight[(size_t) jn - 1], b = arriveLeft[(size_t) jn];
        const float s1 = cfg.area[(size_t) jn - 1], s2 = cfg.area[(size_t) jn];
        const float sum = 2.0f * (s1 * a + s2 * b) / (s1 + s2);
        float p = sum;

        const int hole = cfg.holeAt[(size_t) jn];
        if (hole >= 0)
        {
            const float open = juce::jlimit (0.0f, 1.0f, current.openness[(size_t) hole]);
            const float area = cfg.holeArea[(size_t) hole];
            const float w = area * open * c / (cfg.holeLength[(size_t) hole] * (s1 + s2));
            const float tau = area * (1.0f - open) * cfg.holeChimney[(size_t) hole] / (c * (s1 + s2));

            const float k = bilinear, tk2 = tau * k * k;
            const float a0 = tk2 + k + w;
            const float a1 = (2.0f * w - 2.0f * tk2) / a0;
            const float a2 = (tk2 - k + w) / a0;
            const float b0 = k / a0;  // b1 = 0, b2 = -b0

            auto& st = holeState[(size_t) jn];  // x1, x2, y1, y2
            p = b0 * (sum - st[1]) - a1 * st[2] - a2 * st[3];
            st[1] = st[0];
            st[0] = sum;
            st[3] = st[2];
            st[2] = p;

            // Flow leaving the junction; only the open part of the hole radiates (the chimney is closed).
            radiate (jn, open * (s1 * (2.0f * a - p) + s2 * (2.0f * b - p)) / (rho * c));
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
