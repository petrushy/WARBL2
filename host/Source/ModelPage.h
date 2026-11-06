// The "Model" tab: a live look inside the whistle model, following the air: breath, windway, jet, edge, the
// sound source at the window, the bore and the sound that comes out. Each block names the settings that act on
// it. Below: the jet in slow motion as it crosses the window, the signals at the edge, the jet's preference
// against the bore's resonances (why a register sounds), the harmonics, and the standing wave in the bore.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Whistle/Acoustics.h"

class ModelPage : public juce::Component, private juce::Timer
{
public:
    explicit ModelPage (AudioEngine&);
    ~ModelPage() override;

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void takeSnapshot();
    void analyse();
    void updateImpedance();

    void paintChain (juce::Graphics&, juce::Rectangle<float>);
    void paintWindow (juce::Graphics&, juce::Rectangle<float>);
    void paintEdgeSignals (juce::Graphics&, juce::Rectangle<float>);
    void paintJetAndBore (juce::Graphics&, juce::Rectangle<float>);
    void paintHarmonics (juce::Graphics&, juce::Rectangle<float>);
    void paintBore (juce::Graphics&, juce::Rectangle<float>);

    float at (const std::vector<float>&, double index) const;  // Linear interpolation, clamped
    float jetCentreAt (double fraction) const;                // Jet centre line at x = fraction W, at the shown time (m)

    AudioEngine& audio;

    // The scope's latest samples, oldest first, and the model's state with them.
    struct Snapshot
    {
        std::vector<float> deflection, jetFlow, windowFlow, drive, vortex, sound;
        float rate = 96000.0f, pressure = 0.0f, jetSpeed = 0.0f, flowShare = 1.0f, travel = 0.0f, exponent = 0.0f;
        float bandCentre = 0.0f, slowBoost = 0.0f, edge = 0.0f, jetGain = 1.0f, jetQ = 0.7f;
        int stages = 2;
        std::vector<float> junctionX, junctionRms, junctionSign;
    } snap;

    whistle::Geometry geometry;

    // From the snapshot
    bool playing = false;
    double period = 0.0;      // Samples
    double shownTime = 0.0;   // Index of the moment shown in slow motion
    double cycleStart = 0.0;  // Index of the zero crossing before it
    float swingMin = 0.0f, swingMax = 0.0f, switched = 0.0f;  // Deflection minus edge over the half-width
    float driveRms = 0.0f, vortexRms = 0.0f, levelDb = -100.0f;
    std::array<float, 11> harmonicsDb {};  // H1..H10 re H1 (index 1..10)
    double slowMotion = 0.0;  // 0..1, the phase shown within a period

    // Bore impedance at the window for the current fingering (refreshed when holes move)
    std::array<double, whistle::kNumHoles> impedanceOpenness {};
    double impedanceFoot = -1.0;
    juce::String impedanceGeometry;
    std::vector<float> impedanceDb;  // Admittance at the window (dB) at impedanceHz: peaks are the resonances
    std::vector<double> impedanceHz, resonanceHz;
    double lastImpedanceMs = 0.0;
};
