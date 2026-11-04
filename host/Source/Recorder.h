// Records a playing session for analysis: the app's sound (stereo WAV, as heard) and every sensor frame with the
// settings needed to replay it through the model offline (CSV). Files go to ~/Downloads/WARBL Stream recordings.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "StreamReceiver.h"

class Recorder
{
public:
    Recorder();
    ~Recorder();

    // Message thread. `header` lines go at the top of the CSV (as # comments). Returns false if a file couldn't be
    // opened.
    bool start (double sampleRate, const juce::StringArray& header);
    void stop();
    bool isRecording() const { return recording.load(); }
    juce::File getFolder() const;
    juce::String getBaseName() const { return baseName; }

    // Message thread: one sensor frame.
    void addFrame (const ReceivedFrame&);

    // Audio thread: the sound as heard (try-lock only; a block is skipped while starting or stopping).
    void addAudio (const float* const* channels, int numChannels, int numSamples);

    double getSeconds() const { return framesWritten.load() / std::max (1.0, rate); }

private:
    juce::TimeSliceThread writerThread { "Recorder" };
    juce::CriticalSection writerLock;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> audioWriter;
    std::unique_ptr<juce::FileOutputStream> csv;
    std::atomic<bool> recording { false };
    std::atomic<double> audioStartMs { 0.0 };  // Hi-res time of the first recorded audio block
    std::atomic<int64_t> framesWritten { 0 };
    double rate = 48000.0;
    juce::String baseName;
};
