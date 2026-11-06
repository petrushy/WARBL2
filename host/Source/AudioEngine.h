// Audio output. Each block it takes the newest sensor frames from the receiver and turns them into the
// whistle model's controls (hole openness from calibrated sensor readings, breath pressure in Pa). The model's
// sound goes through the room reverb.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Recorder.h"
#include "RoomReverb.h"
#include "SensorResponse.h"
#include "StreamReceiver.h"
#include "Whistle/ScalePlan.h"
#include "Whistle/WhistleModel.h"

#include <atomic>

class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    explicit AudioEngine (StreamReceiver& receiver);
    ~AudioEngine() override;

    juce::AudioDeviceManager deviceManager;
    RoomReverb reverb;
    Recorder recorder;
    whistle::WhistleModel whistleModel;

    std::atomic<bool> whistleEnabled { true };
    std::atomic<bool> silent { false };
    // Bluetooth frames arrive in bursts (one batch per connection interval): play them with their own spacing,
    // delayed just enough to absorb the bursts, instead of applying each burst at once.
    std::atomic<bool> smoothBluetooth { true };
    std::atomic<float> bluetoothDelayMs { 0.0f };  // The current playout delay (for display)  // Computes everything but outputs silence (test runs next to the player's app)

    // Sensor calibration from WARBL (or the simulator), set on the message thread.
    void setCalibration (const warbl::DeviceInfo&);

    // WARBL's pressure sensor: 900 counts (10-bit) = 24 inches of water, i.e. about 1.66 Pa per 12-bit count.
    static constexpr float kPascalPerCount = 24.0f * 249.089f / (900.0f * 4.0f * 4.0f);  // Per 14-bit count

    // Sensor response (see SensorResponse.h), set on the message thread.
    std::atomic<float> breathInputMin { response::Breath().inputMin }, breathInputMax { response::Breath().inputMax },
        breathCurve { response::Breath().curve }, breathOutputMax { response::Breath().outputMax };
    std::atomic<float> holeOpenBelow { response::Hole().openBelow }, holeClosedAt { response::Hole().closedAt },
        holeCurve { response::Hole().curve }, holeCentre { response::Hole().centre };
    std::atomic<bool> holeSCurve { response::Hole().sCurve };
    std::atomic<bool> holeFingerHeight { response::Hole().fingerHeight };
    std::atomic<float> holeFingerRange { response::Hole().fingerRange };
    // The thumb hole's own response (see response::Thumb); set on the message thread.
    std::atomic<bool> thumbOwn { response::Thumb().own };
    std::atomic<float> thumbOpenBelow { response::Thumb().openBelow }, thumbClosedAt { response::Thumb().closedAt },
        thumbCurve { response::Thumb().curve };
    response::Thumb thumbResponse() const { return { thumbOwn.load(), thumbOpenBelow.load(), thumbClosedAt.load(), thumbCurve.load() }; }
    // How closed a hole is for its coverage (the thumb by its own response if set).
    float holeClosedness (int hole, float coverage) const
    {
        if (hole == 0 && thumbOwn.load())
            return thumbResponse().closedness (coverage);
        return holeResponse().closedness (coverage, fingerHole (hole));
    }
    // Tilt aims the jet (see response::Tilt); set on the message thread.
    std::atomic<bool> tiltEnabled { response::Tilt().enabled }, tiltInvert { response::Tilt().invert };
    std::atomic<float> tiltZero { response::Tilt().zero }, tiltDeadZone { response::Tilt().deadZone },
        tiltRange { response::Tilt().range }, tiltMaxShift { response::Tilt().maxShift };
    response::Tilt tiltResponse() const
    {
        return { tiltEnabled.load(), tiltZero.load(), tiltDeadZone.load(), tiltRange.load(), tiltMaxShift.load(), tiltInvert.load() };
    }
    std::atomic<float> measuredTilt { 0.0f };  // WARBL's elevation in degrees, smoothed over ~100 ms (for display and zero)
    // Each hole's size, for the physical (finger height) response; set with the instrument.
    std::array<std::atomic<float>, whistle::kNumHoles> holeRadius {}, holeEffectiveLength {};
    response::FingerHole fingerHole (int hole) const
    {
        return { holeRadius[(size_t) hole].load(), holeEffectiveLength[(size_t) hole].load() };
    }
    response::Breath breathResponse() const;
    // WARBL's calibration as text (for recordings): baseline and covered readings per sensor, ambient pressure.
    juce::String calibrationText() const;
    double getSampleRate() const { return sampleRate; }
    response::Hole holeResponse() const;

    // Last sensor inputs before the response curves, for display.
    std::atomic<float> measuredPressure { 0.0f };
    std::array<std::atomic<float>, whistle::kNumHoles> measuredCoverage {};
    std::atomic<float> measuredBellCoverage { 0.0f };

    // Playing from the screen instead of from WARBL.
    std::atomic<bool> manualPlay { false };
    std::array<std::atomic<float>, whistle::kNumHoles> manualOpenness {};
    std::atomic<float> manualPressure { 0.0f };
    std::atomic<float> manualFootOpenness { 1.0f };

    // Test scale: the current instrument's own fingerings from the lowest note up two octaves, played through
    // the model with the breath pressure the model needs for each (found offline, in the background).
    void setScaleInstrument (const whistle::Geometry&, double lowestHz);  // Message thread
    void playScale();                  // Message thread. Plays (planning first if needed), or stops if playing.
    juce::String getScaleStatus() const;
    bool isScaleActive() const { return scalePlaying || scalePlanning; }

    // Last controls sent to the model, for display.
    std::array<std::atomic<float>, whistle::kNumHoles> shownOpenness {};
    std::atomic<float> shownFootOpenness { 1.0f };
    std::atomic<float> shownPressure { 0.0f };

    // Most frames taken from the queue in one audio block (shows whether frames bunch up).
    std::atomic<int> maxFramesPerBlock { 0 };
    std::atomic<uint64_t> blocksProcessed { 0 };  // Shows whether audio is running.

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

    whistle::Controls controlsFromFrame (const warbl::SensorFrame&);
    bool scaleControls (whistle::Controls&, int numSamples);  // Audio thread
    void startScale (std::vector<whistle::ScaleStep>);

    StreamReceiver& receiver;
    std::array<std::atomic<int>, warbl::kNumHoles> baseline {}, covered {};
    std::atomic<int> ambient14 { 4160 };
    // Test scale state. Planning runs on scalePlanner; the steps reach the audio thread under scaleAudioLock.
    mutable juce::CriticalSection scaleLock;  // Message thread and planner
    whistle::Geometry scaleGeometry;
    double scaleLowestHz = 587.33;
    whistle::Voicing scaleVoicing;
    std::vector<whistle::ScaleStep> scaleCache;
    bool scaleCacheValid = false;
    std::atomic<bool> scalePlanning { false }, scalePlaying { false };
    juce::SpinLock scaleAudioLock;
    std::vector<whistle::ScaleStep> scaleSteps;  // Audio thread copy
    std::atomic<int> scaleSample { 0 }, scaleStepIndex { -1 };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    juce::ThreadPool scalePlanner { juce::ThreadPoolOptions().withNumberOfThreads (1) };  // Last: stops before the rest

    // The instrument as two sources (window, tube), each then at the two ears (window L R, tube L R).
    juce::HeapBlock<float> windowDry, bodyDry;
    std::array<juce::HeapBlock<float>, 4> ears;
    // Each source's recent samples, for the small delay between the ears.
    static constexpr int kEarDelaySize = 128;
    std::array<std::array<float, kEarDelaySize>, 2> earDelay {};
    int earDelayWrite = 0;
    void placeAtEars (int numSamples);
    double sampleRate = 48000.0;
    int samplesSinceControls = 0;
    uint32_t lastFrameTime = 0;  // Device timestamp of the last frame
    bool haveFrameTime = false;
    double frameInterval = 0.002;  // s, smoothed
    float smoothedTilt = 0.0f;     // Degrees (audio thread)
    // Bluetooth playout buffer (audio thread): frames waiting for their time, and the arrival-lag statistics.
    struct PendingFrame
    {
        double playoutMs = 0.0;
        whistle::Controls controls;
    };
    std::array<PendingFrame, 128> pendingFrames {};
    int pendingHead = 0, pendingCount = 0;
    double minArrivalLag = 1.0e12, arrivalLagSpread = 0.0;  // ms
    bool haveTilt = false;
    whistle::Controls lastControls;
    int dryCapacity = 0;
};
