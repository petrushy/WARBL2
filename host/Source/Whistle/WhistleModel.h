// Real-time physical model of a whistle, simulated in the time domain:
//
//  - Bore: sound waves travelling up and down the tube (digital waveguides), split into short sections.
//    At every tone hole the waves meet a junction whose behaviour depends on how open the hole is (from its
//    WARBL sensor), so pitch, half-holing and slides come from the simulated air column, not from a fingering
//    lookup. Wall losses, the radiating open foot and the window opening at the top are included.
//  - Jet: breath pressure sets the jet speed (Bernoulli). The jet leaves the windway, is deflected by the
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

namespace whistle
{

struct Controls
{
    std::array<float, kNumHoles> openness {};  // 0 = covered, 1 = open
    float mouthPressure = 0.0f;                 // Pa above ambient
};

class WhistleModel
{
public:
    WhistleModel();

    void prepare (double hostSampleRate);
    void setGeometry (const Geometry&);

    // New target controls; reached with a short ramp (the sensor frame period) to avoid steps.
    void setControls (const Controls&);

    // Adds the radiated sound (mono) to out.
    void render (float* out, int numSamples);

    std::atomic<float> breathNoise { 0.25f };  // Turbulence in the jet, 0..1
    std::atomic<float> outputGainDb { 0.0f };
    // Jet behaviour (see tick()): the frequency it amplifies most (Strouhal number f h / U), how broad that
    // band is, and how fast disturbances travel along it relative to the jet speed.
    std::atomic<float> jetStrouhal { 0.02f };
    std::atomic<float> jetQ { 0.7f };
    std::atomic<float> jetConvection { 0.6f };
    std::atomic<int> jetStages { 2 };  // Band-pass stages: more = sharper.

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

    struct OnePole
    {
        float b0 = 1.0f, a1 = 0.0f, y1 = 0.0f;
        float process (float x) { y1 = b0 * x + a1 * y1; return y1; }
    };

    struct Config
    {
        int numSegments = 0;
        std::array<float, kMaxSegments> delay {};      // Samples
        std::array<float, kMaxSegments> area {};       // m^2
        std::array<float, kMaxSegments> lossB0 {}, lossA1 {};
        std::array<int, kMaxSegments + 1> holeAt {};   // Hole index at junction j (between sections j-1 and j), or -1.
        std::array<float, kMaxSegments + 1> radiationDelay {};  // Samples from junction j to the listener (relative).
        std::array<float, kNumHoles> holeArea {}, holeLength {};

        float windowArea = 0.0f;
        float windowMass = 0.0f;     // Inertance of the air in the window, as M * 2 * rate / Z0 (trapezoidal rule).
        float boreImpedance = 0.0f;  // Z0 of the top section, rho c / S.
        float footB = 0.0f, footA = 0.0f;  // Open foot reflection lowpass.

        float windwayHeight = 0.001f, windowLength = 0.005f, windowWidth = 0.008f, edgeOffset = 0.0f;
        float jetGrowth = 1.0f;     // Amplification of the jet's deflection on its way to the edge.
        float dipoleLength = 0.0f;  // delta_d in the jet-drive pressure.
    };

    void buildConfig (const Geometry&, Config&) const;
    void resetState();
    float tick();  // One sample at the internal rate.

    double hostRate = 48000.0, rate = 96000.0;
    Geometry geometry;

    juce::SpinLock pendingLock;
    Config pending;
    std::atomic<bool> hasPending { false };
    Config config;

    // Bore state
    std::array<DelayLine, kMaxSegments> right {}, left {};
    std::array<OnePole, kMaxSegments> lossRight {}, lossLeft {};
    std::array<float, kMaxSegments + 1> holeX1 {}, holeY1 {};  // Hole junction filter state.
    float topPressure = 0.0f, lastSource = 0.0f;  // At the top of the bore, previous sample.
    float footX1 = 0.0f, footY1 = 0.0f;

    // Jet state
    std::array<float, kJetLineSize> jetLine {};
    int jetWrite = 0;
    float flowIntoBore = 0.0f;  // Through the window, last sample (m^3/s)
    float lastJetFlow = 0.0f;
    float jetB0 = 0.0f, jetA1 = 0.0f, jetA2 = 0.0f;
    std::array<std::array<float, 4>, 3> jetState {};  // x1, x2, y1, y2 per band-pass stage
    int jetFilterCountdown = 0;
    float noiseState1 = 0.0f, noiseState2 = 0.0f;
    std::minstd_rand rng { 12345 };
    std::normal_distribution<float> gauss { 0.0f, 1.0f };

    // Radiation and output
    std::array<float, kRadiationSize> radiation {};
    int radiationRead = 0;
    float lastRadiatedFlow = 0.0f;
    float dcX1 = 0.0f, dcY1 = 0.0f;
    std::array<float, kDecimatorTaps> decimatorCoeffs {}, decimatorBuffer {};
    int decimatorWrite = 0;
    float levelState = 0.0f;

    // Controls, ramped per internal sample
    Controls current, target;
    std::array<float, kNumHoles> opennessStep {};
    float pressureStep = 0.0f;
    int rampRemaining = 0;
};

} // namespace whistle
