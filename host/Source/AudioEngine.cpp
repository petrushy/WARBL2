#include "AudioEngine.h"

void ClickModel::prepare (double sampleRate, int)
{
    clickLength = juce::roundToInt (sampleRate * 0.001);  // 1 ms
    clickRemaining = 0;
}

void ClickModel::handleFrame (const warbl::SensorFrame& frame)
{
    const int pressure = (int) frame.pressure - ambient12.load();
    const int t = threshold.load();

    if (! above && pressure > t)
    {
        above = true;
        clickRemaining = clickLength;
    }
    else if (above && pressure < t - 10)  // Hysteresis, so noise around the threshold doesn't retrigger.
    {
        above = false;
    }
}

void ClickModel::render (float* const* outputs, int numChannels, int numSamples)
{
    const float g = gain.load();
    for (int i = 0; i < numSamples && clickRemaining > 0; ++i, --clickRemaining)
        for (int ch = 0; ch < numChannels; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][i] += g;
}

AudioEngine::AudioEngine (StreamReceiver& r) : receiver (r)
{
    // Until WARBL reports its calibration, assume typical values.
    for (int i = 0; i < warbl::kNumHoles; ++i)
    {
        baseline[(size_t) i] = 40;
        covered[(size_t) i] = 700;
    }
    for (auto& o : manualOpenness)
        o = 1.0f;

    deviceManager.initialiseWithDefaultDevices (0, 2);

    // Ask for a small buffer; the device may round it.
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = 64;
    deviceManager.setAudioDeviceSetup (setup, true);

    reverb.loadPreset (2);  // Chamber music hall
    deviceManager.addAudioCallback (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::setScaleInstrument (const whistle::Geometry& g, double lowestHz)
{
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        const bool used = g.holes[(size_t) i].enabled;
        holeRadius[(size_t) i] = used ? (float) (0.5 * g.holes[(size_t) i].diameter) : 0.0f;
        holeEffectiveLength[(size_t) i] = used ? (float) g.holeEffectiveLength (i) : 0.0f;
    }

    const juce::ScopedLock lock (scaleLock);
    scaleGeometry = g;
    scaleLowestHz = lowestHz;
    scaleCacheValid = false;
}

void AudioEngine::playScale()
{
    if (scalePlaying)
    {
        scalePlaying = false;
        return;
    }
    if (scalePlanning)
        return;

    const auto voicing = whistleModel.getVoicing();
    whistle::Geometry g;
    double lowest;
    {
        const juce::ScopedLock lock (scaleLock);
        // Voicing is set from sliders: compare to within a tiny tolerance.
        auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
        const bool sameVoicing = same (voicing.jetStrouhal, scaleVoicing.jetStrouhal) && same (voicing.jetConvection, scaleVoicing.jetConvection)
                                 && same (voicing.breathNoise, scaleVoicing.breathNoise) && same (voicing.windwayMass, scaleVoicing.windwayMass)
                                 && same (voicing.windwayFriction, scaleVoicing.windwayFriction)
                                 && same (voicing.vortexLoss, scaleVoicing.vortexLoss)
                                 && same (voicing.attackChiff, scaleVoicing.attackChiff);
        if (scaleCacheValid && sameVoicing)
        {
            startScale (scaleCache);
            return;
        }
        g = scaleGeometry;
        lowest = scaleLowestHz;
        scaleVoicing = voicing;
    }

    // Finding each note's pressure plays the model offline for a few seconds: do it in the background.
    scalePlanning = true;
    scalePlanner.addJob ([this, g, voicing, lowest, weakAlive = std::weak_ptr<bool> (alive)]
    {
        auto planned = whistle::planScale (g, voicing, lowest);
        juce::MessageManager::callAsync ([this, weakAlive, steps = std::move (planned)]
        {
            if (weakAlive.lock() == nullptr)
                return;
            {
                const juce::ScopedLock lock (scaleLock);
                scaleCache = steps;
                scaleCacheValid = true;
            }
            scalePlanning = false;
            startScale (steps);
        });
    });
}

void AudioEngine::startScale (std::vector<whistle::ScaleStep> steps)
{
    if (steps.empty())
        return;
    {
        const juce::SpinLock::ScopedLockType lock (scaleAudioLock);
        scaleSteps = std::move (steps);
    }
    scaleSample = 0;
    scaleStepIndex = 0;
    scalePlaying = true;
}

juce::String AudioEngine::getScaleStatus() const
{
    if (scalePlanning)
        return "Preparing the scale (finding each note's breath pressure)...";
    if (! scalePlaying)
        return {};
    const juce::ScopedLock lock (scaleLock);
    const int i = scaleStepIndex.load();
    if (i < 0 || i >= (int) scaleCache.size())
        return {};
    const auto& s = scaleCache[(size_t) i];
    return "Scale: " + juce::String (s.name) + " at " + juce::String (juce::roundToInt (s.pressure)) + " Pa (" + juce::String (i + 1) + " of "
           + juce::String ((int) scaleCache.size()) + ")";
}

bool AudioEngine::scaleControls (whistle::Controls& controls, int numSamples)
{
    if (! scalePlaying)
        return false;

    const juce::SpinLock::ScopedTryLockType lock (scaleAudioLock);
    if (! lock.isLocked())
        return false;

    const int position = scaleSample.load();
    const int step = whistle::scaleControlsAt (scaleSteps, position / sampleRate, controls);
    if (step < 0)
    {
        scalePlaying = false;
        scaleStepIndex = -1;
        return true;  // Breath off
    }

    scaleStepIndex = step;
    scaleSample = position + numSamples;
    return true;
}

void AudioEngine::setCalibration (const warbl::DeviceInfo& info)
{
    for (int i = 0; i < warbl::kNumHoles; ++i)
    {
        baseline[(size_t) i] = info.baseline[(size_t) i];
        covered[(size_t) i] = info.covered[(size_t) i];
    }
    ambient12 = info.ambientPressure * 4;
    clickModel.ambient12 = info.ambientPressure * 4;
}

juce::String AudioEngine::calibrationText() const
{
    juce::String text ("calibration baseline");
    for (const auto& b : baseline)
        text << " " << b.load();
    text << " covered";
    for (const auto& c : covered)
        text << " " << c.load();
    text << " ambient12 " << ambient12.load() << " pascal_per_count " << juce::String (kPascalPerCount, 6);
    return text;
}

response::Breath AudioEngine::breathResponse() const
{
    return { breathInputMin.load(), breathInputMax.load(), breathCurve.load(), breathOutputMax.load() };
}

response::Hole AudioEngine::holeResponse() const
{
    return { holeOpenBelow.load(), holeClosedAt.load(), holeCurve.load(), holeSCurve.load(), holeCentre.load(),
             holeFingerHeight.load(), holeFingerRange.load() };
}

whistle::Controls AudioEngine::controlsFromFrame (const warbl::SensorFrame& frame)
{
    const auto holes = holeResponse();
    whistle::Controls c;
    for (int hole = 0; hole < whistle::kNumHoles; ++hole)
    {
        const auto sensor = (size_t) whistle::kSensorForHole[(size_t) hole];
        const float span = (float) std::max (1, covered[sensor].load() - baseline[sensor].load());
        const float coverage = ((float) frame.holes[sensor] - (float) baseline[sensor].load()) / span;
        measuredCoverage[(size_t) hole] = coverage;
        c.openness[(size_t) hole] = 1.0f - holes.closedness (coverage, fingerHole (hole));
    }

    // The bell sensor; the model only uses it if the instrument lets it close the foot.
    {
        const auto sensor = (size_t) whistle::kBellSensor;
        const float span = (float) std::max (1, covered[sensor].load() - baseline[sensor].load());
        const float coverage = ((float) frame.holes[sensor] - (float) baseline[sensor].load()) / span;
        measuredBellCoverage = coverage;
        c.footOpenness = 1.0f - holes.closedness (coverage);
    }

    const float measured = std::max (0.0f, (float) ((int) frame.pressure - ambient12.load()) * kPascalPerCount);
    measuredPressure = measured;
    c.mouthPressure = breathResponse().map (measured);
    return c;
}

void AudioEngine::placeAtEars (int numSamples)
{
    // Each source as heard from its direction (azimuth, from the reverb's spread): the far ear hears it later
    // (spherical head, Woodworth: a/c (theta + sin theta), a = 8.75 cm) and a little quieter (head shadow,
    // about 6 dB at the side). The room responses start at each ear's direct sound, so they get the same signals.
    constexpr double headRadius = 0.0875, speedOfSound = 343.0;
    const float* sources[2] = { windowDry.get(), bodyDry.get() };
    float delay[2][2], gain[2][2];  // [source][ear], samples
    for (int s = 0; s < 2; ++s)
    {
        const double azimuth = reverb.sourceAzimuth (s), side = std::abs (azimuth);
        const int far = azimuth < 0.0 ? 1 : 0;  // Source on the left: the right ear is the far one
        delay[s][far] = (float) (headRadius / speedOfSound * (side + std::sin (side)) * sampleRate);
        delay[s][1 - far] = 0.0f;
        gain[s][far] = (float) std::pow (10.0, -6.0 * std::sin (side) / 20.0);
        gain[s][1 - far] = 1.0f;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        for (int s = 0; s < 2; ++s)
            earDelay[(size_t) s][(size_t) earDelayWrite] = sources[s][i];
        for (int s = 0; s < 2; ++s)
            for (int e = 0; e < 2; ++e)
            {
                const float d = std::min (delay[s][e], (float) kEarDelaySize - 2.0f);
                const int whole = (int) d;
                const float frac = d - (float) whole;
                const auto& line = earDelay[(size_t) s];
                const float a = line[(size_t) ((earDelayWrite - whole + kEarDelaySize) % kEarDelaySize)];
                const float b = line[(size_t) ((earDelayWrite - whole - 1 + kEarDelaySize) % kEarDelaySize)];
                ears[(size_t) (2 * s + e)][i] = gain[s][e] * (a + frac * (b - a));
            }
        earDelayWrite = (earDelayWrite + 1) % kEarDelaySize;
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device->getCurrentSampleRate();
    sampleRate = rate;
    const int maxBlock = std::max (device->getCurrentBufferSizeSamples(), 4096);
    windowDry.allocate ((size_t) maxBlock, true);
    bodyDry.allocate ((size_t) maxBlock, true);
    for (auto& e : ears)
        e.allocate ((size_t) maxBlock, true);
    dryCapacity = maxBlock;

    clickModel.prepare (rate, maxBlock);
    reverb.prepare (rate, maxBlock);
    whistleModel.prepare (rate);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    blocksProcessed++;

    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    // Apply every frame that has arrived as soon as possible: no de-jitter buffer, lowest latency. Frames that
    // arrive together (USB and MIDI can deliver them in bunches) are spread over this block, each reached within
    // its share of it, so a short finger lift or breath gap isn't lost by jumping straight to the last frame.
    constexpr int kMaxBunch = 16;
    std::array<whistle::Controls, kMaxBunch> bunch;
    ReceivedFrame received;
    int count = 0;
    whistle::Controls controls;
    bool haveControls = false;
    while (receiver.audioQueue.pop (received))
    {
        clickModel.handleFrame (received.frame);
        controls = controlsFromFrame (received.frame);
        // WARBL's frame interval (2 ms, or 1 ms with the 1 kHz stream loop), from its own timestamps: each frame's
        // controls are reached over that time, so the model follows the sensors without steps or extra lag.
        if (haveFrameTime)
        {
            const auto ticks = (received.frame.timestamp - lastFrameTime) & ((1u << warbl::kTimestampBits) - 1);
            const double interval = ticks / warbl::kDeviceTicksPerSecond;
            if (interval > 0.0003 && interval < 0.01)
                frameInterval += 0.1 * (interval - frameInterval);
        }
        lastFrameTime = received.frame.timestamp;
        haveFrameTime = true;
        bunch[(size_t) (count % kMaxBunch)] = controls;  // Beyond kMaxBunch, the oldest are dropped (stale anyway)
        haveControls = true;
        ++count;
    }
    int bunchSize = std::min (count, kMaxBunch);

    if (scaleControls (controls, numSamples))
    {
        haveControls = true;
        bunchSize = 0;
    }
    else if (manualPlay)
    {
        for (int i = 0; i < whistle::kNumHoles; ++i)
            controls.openness[(size_t) i] = manualOpenness[(size_t) i].load();
        controls.mouthPressure = manualPressure.load();
        controls.footOpenness = manualFootOpenness.load();
        haveControls = true;
        bunchSize = 0;
    }

    // If frames stop arriving (stream off, cable pulled), stop blowing rather than hold the last breath.
    if (! haveControls)
    {
        samplesSinceControls += numSamples;
        if (samplesSinceControls > (int) (0.05 * sampleRate) && lastControls.mouthPressure > 0.0f)
        {
            controls = lastControls;
            controls.mouthPressure = 0.0f;
            haveControls = true;
        }
    }

    if (haveControls)
    {
        samplesSinceControls = 0;
        lastControls = controls;
        if (bunchSize < 2)
            whistleModel.setControls (controls, frameInterval);
        for (int i = 0; i < whistle::kNumHoles; ++i)
            shownOpenness[(size_t) i] = controls.openness[(size_t) i];
        shownFootOpenness = controls.footOpenness;
        shownPressure = controls.mouthPressure;
    }

    if (count > maxFramesPerBlock.load())
        maxFramesPerBlock = count;

    if (numSamples > dryCapacity || numOutputs < 2 || outputs[0] == nullptr || outputs[1] == nullptr)
        return;

    // Instrument as two sources -> placed at the two ears (dry) + room reverb for each.
    juce::FloatVectorOperations::clear (windowDry.get(), numSamples);
    juce::FloatVectorOperations::clear (bodyDry.get(), numSamples);
    if (whistleEnabled)
    {
        if (bunchSize >= 2)
        {
            // Oldest first: frame i takes over at i/n of the block and is reached by the end of its share.
            const int oldest = count - bunchSize;
            for (int i = 0; i < bunchSize; ++i)
            {
                const int from = i * numSamples / bunchSize, to = (i + 1) * numSamples / bunchSize;
                whistleModel.setControls (bunch[(size_t) ((oldest + i) % kMaxBunch)], (to - from) / sampleRate);
                whistleModel.render (windowDry.get() + from, bodyDry.get() + from, to - from);
            }
        }
        else
        {
            whistleModel.render (windowDry.get(), bodyDry.get(), numSamples);
        }
    }

    placeAtEars (numSamples);
    juce::FloatVectorOperations::add (outputs[0], ears[0].get(), ears[2].get(), numSamples);
    juce::FloatVectorOperations::add (outputs[1], ears[1].get(), ears[3].get(), numSamples);
    reverb.process (ears[0].get(), ears[1].get(), ears[2].get(), ears[3].get(), outputs[0], outputs[1], numSamples);

    // The click stays dry so latency measurements aren't smeared by the reverb.
    if (clickEnabled)
        clickModel.render (outputs, numOutputs, numSamples);

    recorder.addAudio (outputs, numOutputs, numSamples);

    if (silent)
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);
}
