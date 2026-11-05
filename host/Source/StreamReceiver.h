// Connects to WARBL over USB MIDI, decodes sensor stream SysEx on the MIDI thread, and hands frames
// to the audio thread and the UI through lock-free queues. Also measures frame rate, lost frames and timing jitter.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "SimulatedPlayer.h"
#include "StreamProtocol.h"

#include <array>
#include <atomic>

// Single-producer single-consumer queue (MIDI thread -> one consumer), no locks or allocation.
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item)
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 == 0)
            return false;
        buffer[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = item;
        return true;
    }

    bool pop (T& item)
    {
        const auto scope = fifo.read (1);
        if (scope.blockSize1 + scope.blockSize2 == 0)
            return false;
        item = buffer[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        return true;
    }

    void clear() { fifo.reset(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, Capacity> buffer {};
};

// A frame plus when it reached the app.
struct ReceivedFrame
{
    warbl::SensorFrame frame;
    double arrivalMs = 0.0;  // juce::Time::getMillisecondCounterHiRes() when the MIDI callback got it.
    double deviceMs = 0.0;   // Unwrapped device timestamp.
    bool batched = false;    // Came in a Bluetooth batch (several frames per message, arriving together)
};

class StreamReceiver : private juce::MidiInputCallback, private juce::HighResolutionTimer
{
public:
    // Histogram of extra transport delay: how much later than the fastest recent frame each frame arrived.
    static constexpr int kHistogramBins = 100;
    static constexpr double kHistogramBinMs = 0.1;

    StreamReceiver();
    ~StreamReceiver() override;

    // Opens every MIDI input and output whose name contains "WARBL" (macOS may also list an old Bluetooth
    // pairing or a simulator under that name). Commands go to all of them, and frames are taken from the first
    // input that streams. Returns true if at least one input and output were opened.
    bool connect();

    // Test runs (--snapshot) never touch WARBL: connect() then does nothing, so they can't interfere with the
    // user's own copy of the app. Set before the receiver is used.
    static inline std::atomic<bool> leaveDevicesAlone { false };
    void disconnect();
    bool isConnected() const { return ! inputs.empty() && ! outputs.empty(); }
    juce::String getPortNames() const;
    juce::String getStreamingPortName() const;  // Empty until frames arrive.

    void setStream (bool on, bool suppressNotes);
    void requestInfo();

    // Generate frames from a scripted player instead of WARBL (frames from WARBL are ignored meanwhile).
    void setSimulating (bool shouldSimulate);
    std::atomic<bool> simulateBluetooth { false };  // The simulator sends Bluetooth batches every 15 ms instead of frames
    bool isSimulating() const { return simulating; }

    // Consumers. The audio queue must only be read by the audio thread, the UI queue by the message thread.
    SpscQueue<ReceivedFrame, 1024> audioQueue;
    SpscQueue<ReceivedFrame, 8192> uiQueue;

    // Set on the message thread when an info message arrives.
    std::function<void (const warbl::DeviceInfo&)> onInfo;

    struct Stats
    {
        uint64_t frames = 0;
        uint64_t lostFrames = 0;
        double framesPerSecond = 0.0;
        double freshHoles = 0.0;
        uint64_t rejectedHoleReadings = 0;  // Garbled tone hole transfers WARBL caught (see SensorFrame::holesRejected)
        bool bluetooth = false;             // The recent frames came in Bluetooth batches
        double framesPerBatch = 0.0, batchIntervalMs = 0.0;  // Smoothed  // Share of recent frames with a new tone hole reading (WARBL's ATmega kept up)
        std::array<uint32_t, kHistogramBins> histogram {};
        uint32_t histogramOverflow = 0;
    };

    Stats getStats() const;
    void resetStats();

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void hiResTimerCallback() override;
    void acceptFrame (const warbl::SensorFrame&, double nowMs, bool batched = false);
    void acceptBatch (const uint8_t* data, size_t size, double nowMs);
    std::vector<warbl::SensorFrame> simulatedBatch;  // Simulator timer thread only
    double lastBatchMs = 0.0;
    void postInfo (const warbl::DeviceInfo&);
    void sendSysEx (const std::vector<uint8_t>&);

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::vector<std::unique_ptr<juce::MidiOutput>> outputs;
    std::atomic<juce::MidiInput*> streamingInput { nullptr };

    // Written only on the MIDI thread (statistics are copied under a lock that is held only briefly).
    warbl::Unwrapper timestampUnwrapper { warbl::kTimestampBits };
    int lastSeq = -1;
    double windowMin[2] = { 1.0e12, 1.0e12 };  // Minimum (arrival - device) in the current and previous second.
    double windowStartMs = 0.0;
    double rateWindowStartMs = 0.0;
    uint64_t rateWindowFrames = 0, rateWindowHoleUpdates = 0;

    std::atomic<bool> simulating { false };
    warbl::SimulatedPlayer player;  // Simulator timer thread only.
    uint64_t simulatedTick = 0;
    juce::SpinLock producerLock;

    mutable juce::SpinLock statsLock;
    Stats stats;
    std::atomic<bool> resetRequested { false };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);  // Expires with the receiver, for async callbacks.
};
