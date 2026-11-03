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
constexpr double kListenerDistance = 0.5;  // m in front of the window, for the radiation delays and level.

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
    holeX1.fill (0.0f);
    holeY1.fill (0.0f);
    jetLine.fill (0.0f);
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
    for (int i = 0; i < kNumHoles; ++i)
        points.push_back ({ juce::jlimit (0.005, g.boreLength - 0.005, g.holes[(size_t) i].position), i });
    std::sort (points.begin(), points.end());
    points.push_back ({ g.boreLength, -1 });

    std::vector<std::pair<double, int>> junctions;
    for (size_t i = 0; i + 1 < points.size(); ++i)
    {
        junctions.push_back (points[i]);
        const double span = points[i + 1].first - points[i].first;
        const int pieces = (int) std::ceil (span / kMaxSectionLength);
        for (int p = 1; p < pieces; ++p)
            junctions.push_back ({ points[i].first + span * p / pieces, -1 });
    }
    junctions.push_back (points.back());

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
    pressureStep = (target.mouthPressure - current.mouthPressure) / rampSamples;
    rampRemaining = rampSamples;
}

float WhistleModel::tick()
{
    if (rampRemaining > 0)
    {
        for (int i = 0; i < kNumHoles; ++i)
            current.openness[(size_t) i] += opennessStep[(size_t) i];
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

    // Turbulent fluctuations of the jet (band-limited noise), growing with jet speed.
    noiseState1 += 0.25f * (gauss (rng) - noiseState1);
    noiseState2 += 0.05f * (noiseState1 - noiseState2);
    const float turbulence = breathNoise.load (std::memory_order_relaxed) * (noiseState1 - noiseState2) * 2.0f;

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
    const float jetFlowRate = (jetFlow - lastJetFlow) * (float) rate;
    lastJetFlow = jetFlow;

    // Pressure the jet produces across the window (jet drive), minus losses from vortices shed at the edge.
    const float jetDrive = rho * cfg.dipoleLength / cfg.windowArea * jetFlowRate;
    const float vortexLoss = -0.5f * rho * (windowVelocity / 0.6f) * std::abs (windowVelocity / 0.6f);
    const float source = jetDrive + vortexLoss;

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
    radiate (0, -flowIntoBore);

    // 4. Junctions inside the bore. Pressure there is the area-weighted sum of the arriving waves; an open hole
    //    lets low frequencies escape (a highpass whose corner rises with the open area).
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
            const float corner = std::max (6.0f, cfg.holeArea[(size_t) hole] * open * c / (cfg.holeLength[(size_t) hole] * (s1 + s2)));
            const float k = std::tan (corner / (2.0f * (float) rate));
            const float a1 = (1.0f - k) / (1.0f + k);
            p = (sum - holeX1[(size_t) jn]) / (1.0f + k) + a1 * holeY1[(size_t) jn];
            holeX1[(size_t) jn] = sum;
            holeY1[(size_t) jn] = p;

            radiate (jn, (s1 * (2.0f * a - p) + s2 * (2.0f * b - p)) / (rho * c));  // Flow out of the hole
        }

        sendRight[(size_t) jn] = p - b;
        sendLeft[(size_t) jn - 1] = p - a;
    }

    // 5. Open foot.
    const float arrivingFoot = arriveRight[(size_t) n - 1];
    const float lowpassed = cfg.footB * (arrivingFoot + footX1) + cfg.footA * footY1;
    footX1 = arrivingFoot;
    footY1 = lowpassed;
    sendLeft[(size_t) n - 1] = -lowpassed;
    radiate (n, cfg.area[(size_t) n - 1] / (rho * c) * (arrivingFoot + lowpassed));

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
                holeX1.fill (0.0f);
                holeY1.fill (0.0f);
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
