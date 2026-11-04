// Real-time physical model of a whistle, simulated in the time domain:
//
//  - Bore: sound waves travelling up and down the tube (digital waveguides), split into short sections.
//    At every tone hole the waves meet a junction whose behaviour depends on how open the hole is (from its
//    WARBL sensor), so pitch, half-holing and slides come from the simulated air column, not from a fingering
//    lookup. Wall losses, the radiating open foot and the window opening at the top are included.
//  - Jet: breath pressure accelerates the air in the windway, up to the jet speed (Bernoulli); the walls'
//    boundary layers carry less flow than the core. The jet leaves the windway, is deflected by the
//    sound in the window and takes a pressure-dependent time to reach the edge, where it is split in and out
//    of the pipe (jet-drive model, Verge/Fabre/Hirschberg). That delay is what makes the whistle overblow to
//    the next register as you blow harder. Turbulence adds breath noise.
//
// Runs internally at twice the host sample rate for accurate short delays. Audio thread: render() and
// setControls(); setGeometry() from any other thread.

#pragma once

#include "WhistleGeometry.h"

#include <atomic>
#include <random>
#include <vector>

namespace whistle
{

struct Controls
{
    std::array<float, kNumHoles> openness {};  // 0 = covered, 1 = open (disabled holes are ignored)
    float footOpenness = 1.0f;                  // Bell sensor; used only if the geometry lets it close the foot
    float mouthPressure = 0.0f;                 // Pa above ambient
};

class WhistleModel
{
public:
    WhistleModel();

    void prepare (double hostSampleRate);
    void setGeometry (const Geometry&);

    // New target controls; reached with a short ramp (by default the sensor frame period) to avoid steps.
    void setControls (const Controls&, double rampSeconds = 0.002);

    // Adds the radiated sound (mono) to out.
    void render (float* out, int numSamples);
    // The same as two sources, so they can be placed apart: what comes out of the window, and what comes out of
    // the tone holes and the foot. Their sum is what render() gives; window and body may be the same buffer.
    void render (float* window, float* body, int numSamples);

    std::atomic<float> noiseStrouhal { 0.08f };  // Turbulence band centre, as f h / U (0.08: ~2-3 kHz when playing)
    std::atomic<float> breathNoise { 0.05f };  // Turbulence at the jet and edge, 0..1 (its level grows with jet speed)
    std::atomic<float> directNoise { 0.0f };   // How much of the edge noise escapes straight from the window, 0..1
    std::atomic<float> noisePulsing { 0.8f };  // How much the edge noise comes in bursts as the jet crosses the edge, 0..1
    std::atomic<float> outputGainDb { 0.0f };
    // Jet behaviour (see tick()): the frequency it amplifies most (Strouhal number f h / U), how broad that
    // band is, and how fast disturbances travel along it relative to the jet speed.
    std::atomic<float> jetStrouhal { 0.02f };
    std::atomic<float> jetQ { 0.7f };
    std::atomic<float> jetConvection { 0.6f };
    std::atomic<int> jetStages { 2 };  // Band-pass stages: more = sharper.
    // Windway physics, 0..1 (see Voicing).
    std::atomic<float> windwayMass { 1.0f };
    std::atomic<float> windwayFriction { 1.0f };
    std::atomic<float> vortexLoss { 1.0f };  // See Voicing

    // Sets all of the above that a Voicing holds, or reads them back.
    void setVoicing (const Voicing&);
    Voicing getVoicing() const;

    // Offline measurements only (WhistleCheck passive), never on the audio thread: an extra pressure source at
    // the window (Pa), and a log of the flow into the bore (m^3/s), both per internal sample.
    float testSource = 0.0f;
    std::vector<float>* windowFlowLog = nullptr;
    float testWindowRadiation = 1.0f, testBodyRadiation = 1.0f;  // Scale what the window / the holes and foot radiate

    // For display.
    std::atomic<float> jetVelocity { 0.0f };   // m/s
    std::atomic<float> level { 0.0f };         // Output RMS, linear

private:
    static constexpr int kMaxSegments = 64;
    static constexpr int kLineSize = 64;      // Per bore section; sections are at most ~3 cm.
    static constexpr int kJetLineSize = 1024;
    static constexpr int kRadiationSize = 512;
    static constexpr int kDecimatorTaps = 63;

    struct DelayLine
    {
        std::array<float, kLineSize> buffer {};
        int write = 0;

        void push (float v) { buffer[(size_t) write] = v; write = (write + 1) & (kLineSize - 1); }
        float read (float delay) const;  // Delay in samples (>= 2), Lagrange interpolated.
    };

    struct Biquad  // Transposed direct form II
    {
        float s1 = 0.0f, s2 = 0.0f;
        float process (float x, const std::array<float, 5>& c)  // b0 b1 b2 a1 a2
        {
            const float y = c[0] * x + s1;
            s1 = c[1] * x - c[3] * y + s2;
            s2 = c[2] * x - c[4] * y;
            return y;
        }
    };

    struct Config
    {
        int numSegments = 0;
        std::array<float, kMaxSegments> delay {};      // Samples
        std::array<float, kMaxSegments> area {};       // m^2
        std::array<std::array<float, 5>, kMaxSegments> loss {};  // Wall loss and dispersion filter (b0 b1 b2 a1 a2)
        std::array<int, kMaxSegments + 1> holeAt {};   // Hole index at junction j (between sections j-1 and j), or -1.
        std::array<float, kMaxSegments + 1> radiationDelay {};  // Samples from junction j to the listener (relative).
        // Per hole (full size): inner air mass (chimney and inner end correction) and outer air mass (outer end
        // correction, b / 2) in kg/m^4, the radiation resistance rho c / A in parallel with the outer mass
        // (Pa s/m^3), and the chimney's compliance when covered (m^3/Pa).
        std::array<double, kNumHoles> holeInnerMass {}, holeOuterMass {}, holeRadiationResistance {}, holeCompliance {};

        float windowArea = 0.0f;
        float windowMass = 0.0f;     // Inertance of the air in the window, as M * 2 * rate / Z0 (trapezoidal rule).
        float boreImpedance = 0.0f;  // Z0 of the top section, rho c / S.
        float footB = 0.0f, footA = 0.0f;  // Open foot reflection lowpass.
        bool bellClosesFoot = false;

        float windwayHeight = 0.001f, windwayLength = 0.018f, windowLength = 0.005f, windowWidth = 0.008f, edgeOffset = 0.0f;
        float jetGrowth = 1.0f;     // Amplification of the jet's deflection on its way to the edge.
        float dipoleLength = 0.0f;  // delta_d in the jet-drive pressure.
    };

    void buildConfig (const Geometry&, Config&) const;
    void resetState();
    void tick (float& window, float& body);  // One sample at the internal rate: the two sources' sound.

    double hostRate = 48000.0, rate = 96000.0;
    Geometry geometry;

    juce::SpinLock pendingLock;
    Config pending;
    std::atomic<bool> hasPending { false };
    Config config;

    // Bore state
    std::array<DelayLine, kMaxSegments> right {}, left {};
    std::array<Biquad, kMaxSegments> lossRight {}, lossLeft {};
    std::array<std::array<double, 9>, kMaxSegments + 1> holeState {};  // Hole junction filters: sum, p, open flow; 3 samples each
    float topPressure = 0.0f, lastSource = 0.0f;  // At the top of the bore, previous sample.
    float footX1 = 0.0f, footY1 = 0.0f;

    // Jet state
    std::array<float, kJetLineSize> jetLine {};
    int jetWrite = 0;
    float flowIntoBore = 0.0f;  // Through the window, last sample (m^3/s)
    float lastJetFlow = 0.0f;
    float windwaySpeed = 0.0f;  // Core speed of the air leaving the windway (m/s)
    float jetB0 = 0.0f, jetA1 = 0.0f, jetA2 = 0.0f;
    std::array<std::array<float, 4>, 3> jetState {};  // x1, x2, y1, y2 per band-pass stage
    int jetFilterCountdown = 0;
    float noiseLow0 = 0.0f, noiseLow = 0.0f, noiseHigh = 0.0f, noiseLowA = 0.0f, noiseHighA = 0.0f, noiseNorm = 1.0f;
    int noiseFilterCountdown = 0;
    std::minstd_rand rng { 12345 };
    std::normal_distribution<float> gauss { 0.0f, 1.0f };

    // Radiation and output
    // Radiation and output, per source: 0 = window, 1 = tone holes and foot.
    std::array<std::array<float, kRadiationSize>, 2> radiation {};
    int radiationRead = 0;
    std::array<float, 2> lastRadiatedFlow {}, dcX1 {}, dcY1 {};
    std::array<float, kDecimatorTaps> decimatorCoeffs {};
    std::array<std::array<float, kDecimatorTaps>, 2> decimatorBuffer {};
    int decimatorWrite = 0;
    float levelState = 0.0f;

    // Controls, ramped per internal sample
    Controls current, target;
    std::array<float, kNumHoles> opennessStep {};
    float footStep = 0.0f, pressureStep = 0.0f;
    int rampRemaining = 0;
};

} // namespace whistle
