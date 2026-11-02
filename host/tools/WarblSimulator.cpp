// Pretends to be a WARBL sending the sensor stream, through virtual MIDI ports named "WARBL Simulator".
// For trying the host app or a synth without the instrument. Plays a slow scale with breath swells
// (see SimulatedPlayer.h). The app also has this built in ("Simulate WARBL").
//
//   WarblSimulator [seconds]     (runs until killed if no time is given)

#include <juce_audio_devices/juce_audio_devices.h>

#include "SimulatedPlayer.h"

#include <atomic>
#include <cmath>

class Simulator : private juce::MidiInputCallback, private juce::HighResolutionTimer
{
public:
    Simulator()
    {
        output = juce::MidiOutput::createNewDevice ("WARBL Simulator");
        input = juce::MidiInput::createNewDevice ("WARBL Simulator", this);
        if (input != nullptr)
            input->start();
        startTimer (2);  // Like the firmware's 2 ms loop over USB.
    }

    ~Simulator() override
    {
        stopTimer();
        if (input != nullptr)
            input->stop();
    }

    bool isOpen() const { return input != nullptr && output != nullptr; }
    uint64_t framesSent() const { return sent; }

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (! m.isSysEx())
            return;

        const auto* data = m.getSysExData();
        const auto size = (size_t) m.getSysExDataSize();
        const auto type = warbl::messageType (data, size);
        if (! type)
            return;

        if (*type == warbl::kConfig && size >= 5)
        {
            streaming = (data[4] & warbl::kStreamOn) != 0;
            std::printf ("stream %s\n", streaming ? "on" : "off");
        }
        else if (*type == warbl::kRequestInfo)
        {
            send (warbl::kInfo, warbl::encodeInfoPayload (warbl::SimulatedPlayer::info()));
        }
    }

    void hiResTimerCallback() override
    {
        const double t = (double) tick++ * 0.002;
        if (! streaming)
            return;

        send (warbl::kFrame, warbl::encodeFramePayload (player.frame (t)));
        ++sent;
    }

    void send (uint8_t type, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> data { warbl::kSysExId1, warbl::kSysExId2, warbl::kProtocolVersion, type };
        data.insert (data.end(), payload.begin(), payload.end());
        output->sendMessageNow (juce::MidiMessage::createSysExMessage (data.data(), (int) data.size()));
    }

    std::unique_ptr<juce::MidiOutput> output;
    std::unique_ptr<juce::MidiInput> input;
    std::atomic<bool> streaming { false };
    std::atomic<uint64_t> sent { 0 };
    uint64_t tick = 0;
    warbl::SimulatedPlayer player;
};

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;  // CoreMIDI needs JUCE initialised.

    Simulator simulator;
    if (! simulator.isOpen())
    {
        std::printf ("Couldn't create the virtual MIDI ports\n");
        return 1;
    }

    std::printf ("WARBL Simulator running; waiting for a host to turn the stream on\n");
    const int seconds = argc > 1 ? std::atoi (argv[1]) : -1;
    for (int s = 0; seconds < 0 || s < seconds; ++s)
        juce::Thread::sleep (1000);

    std::printf ("sent %llu frames\n", (unsigned long long) simulator.framesSent());
    return 0;
}
