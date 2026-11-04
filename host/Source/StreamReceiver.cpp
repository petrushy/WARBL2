#include "StreamReceiver.h"

StreamReceiver::StreamReceiver() = default;

StreamReceiver::~StreamReceiver()
{
    stopTimer();
    disconnect();
}

bool StreamReceiver::connect()
{
    disconnect();
    if (leaveDevicesAlone)
        return false;

    for (const auto& d : juce::MidiInput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto in = juce::MidiInput::openDevice (d.identifier, this))
                inputs.push_back (std::move (in));

    for (const auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("WARBL"))
            if (auto out = juce::MidiOutput::openDevice (d.identifier))
                outputs.push_back (std::move (out));

    if (! isConnected())
    {
        disconnect();
        return false;
    }

    resetStats();
    for (auto& in : inputs)
        in->start();
    return true;
}

void StreamReceiver::disconnect()
{
    for (auto& in : inputs)
        in->stop();
    inputs.clear();
    outputs.clear();
    streamingInput = nullptr;
}

juce::String StreamReceiver::getPortNames() const
{
    juce::StringArray names;
    for (const auto& in : inputs)
        names.add (in->getName());
    return names.joinIntoString (", ");
}

juce::String StreamReceiver::getStreamingPortName() const
{
    if (simulating)
        return "built-in simulator";

    const auto* in = streamingInput.load();
    return in != nullptr ? in->getName() : juce::String();
}

void StreamReceiver::setStream (bool on, bool suppressNotes)
{
    sendSysEx (warbl::makeConfig (on, suppressNotes));
}

void StreamReceiver::requestInfo()
{
    sendSysEx (warbl::makeRequestInfo());
}

void StreamReceiver::sendSysEx (const std::vector<uint8_t>& data)
{
    const auto message = juce::MidiMessage::createSysExMessage (data.data(), (int) data.size());
    for (auto& out : outputs)
        out->sendMessageNow (message);
}

void StreamReceiver::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    if (! message.isSysEx())
        return;

    const auto* data = message.getSysExData();
    const auto size = (size_t) message.getSysExDataSize();
    const auto type = warbl::messageType (data, size);
    if (! type)
        return;

    if (*type == warbl::kInfo)
    {
        if (auto info = warbl::decodeInfo (data, size))
            if (! simulating)
                postInfo (*info);
        return;
    }

    auto frame = warbl::decodeFrame (data, size);
    if (! frame || simulating)
        return;

    // Stay with the first port that streams, so two streaming devices don't mix.
    juce::MidiInput* expected = nullptr;
    if (! streamingInput.compare_exchange_strong (expected, source) && expected != source)
        return;

    acceptFrame (*frame, nowMs);
}

void StreamReceiver::postInfo (const warbl::DeviceInfo& info)
{
    juce::MessageManager::callAsync ([this, weakAlive = std::weak_ptr<bool> (alive), info]
    {
        if (weakAlive.lock() != nullptr && onInfo)  // The receiver may be gone by the time this runs.
            onInfo (info);
    });
}

void StreamReceiver::setSimulating (bool shouldSimulate)
{
    if (shouldSimulate == simulating.load())
        return;

    if (shouldSimulate)
    {
        simulatedTick = 0;
        player = {};
        simulating = true;
        resetStats();
        postInfo (warbl::SimulatedPlayer::info());
        startTimer (2);  // Like the firmware's 2 ms loop over USB.
    }
    else
    {
        stopTimer();
        simulating = false;
        resetStats();
        requestInfo();
    }
}

void StreamReceiver::hiResTimerCallback()
{
    const auto frame = player.frame ((double) simulatedTick++ * 0.002);
    acceptFrame (frame, juce::Time::getMillisecondCounterHiRes());
}

void StreamReceiver::acceptFrame (const warbl::SensorFrame& frame, double nowMs)
{
    // The MIDI thread and the simulator timer can both get here around a switch; the queues allow one producer at a time.
    const juce::SpinLock::ScopedLockType producer (producerLock);

    if (resetRequested.exchange (false))
    {
        const juce::SpinLock::ScopedLockType lock (statsLock);
        stats = {};
        lastSeq = -1;
        windowMin[0] = windowMin[1] = 1.0e12;
        windowStartMs = rateWindowStartMs = nowMs;
        rateWindowFrames = 0;
        timestampUnwrapper.reset();
    }

    ReceivedFrame received;
    received.frame = frame;
    received.arrivalMs = nowMs;
    received.deviceMs = (double) timestampUnwrapper.next (frame.timestamp) * 1000.0 / warbl::kDeviceTicksPerSecond;

    audioQueue.push (received);
    uiQueue.push (received);

    // Timing. The device and computer clocks have an unknown offset, so measure each frame's delay relative
    // to the fastest frame of the last 1-2 s. That leaves only the variable part (jitter), and slow clock drift
    // between the two clocks drops out.
    const double offset = nowMs - received.deviceMs;
    if (nowMs - windowStartMs > 1000.0)
    {
        windowMin[1] = windowMin[0];
        windowMin[0] = 1.0e12;
        windowStartMs = nowMs;
    }
    windowMin[0] = std::min (windowMin[0], offset);
    const double extraDelay = offset - std::min (windowMin[0], windowMin[1]);

    const juce::SpinLock::ScopedLockType lock (statsLock);

    if (lastSeq >= 0)
        stats.lostFrames += (uint64_t) ((frame.seq - lastSeq - 1) & ((1 << warbl::kSeqBits) - 1));
    lastSeq = frame.seq;

    stats.frames++;
    const auto bin = (int) (extraDelay / kHistogramBinMs);
    if (bin >= 0 && bin < kHistogramBins)
        stats.histogram[(size_t) bin]++;
    else
        stats.histogramOverflow++;

    rateWindowFrames++;
    if (frame.holesUpdated())
        rateWindowHoleUpdates++;
    if (nowMs - rateWindowStartMs >= 500.0)
    {
        stats.framesPerSecond = (double) rateWindowFrames * 1000.0 / (nowMs - rateWindowStartMs);
        stats.freshHoles = (double) rateWindowHoleUpdates / (double) std::max<uint64_t> (1, rateWindowFrames);
        rateWindowFrames = 0;
        rateWindowHoleUpdates = 0;
        rateWindowStartMs = nowMs;
    }
}

StreamReceiver::Stats StreamReceiver::getStats() const
{
    const juce::SpinLock::ScopedLockType lock (statsLock);
    return stats;
}

void StreamReceiver::resetStats()
{
    resetRequested = true;
}
