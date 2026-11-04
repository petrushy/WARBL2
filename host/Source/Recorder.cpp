#include "Recorder.h"

Recorder::Recorder()
{
    writerThread.startThread();
}

Recorder::~Recorder()
{
    stop();
    writerThread.stopThread (2000);
}

juce::File Recorder::getFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads").getChildFile ("WARBL Stream recordings");
}

bool Recorder::start (double sampleRate, const juce::StringArray& header)
{
    stop();
    rate = sampleRate;
    const auto folder = getFolder();
    folder.createDirectory();
    baseName = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");

    const auto wavFile = folder.getChildFile (baseName + ".wav");
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wavFile);
    if (! static_cast<juce::FileOutputStream*> (stream.get())->openedOk())
        return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24));
    if (writer == nullptr)
        return false;

    auto csvFile = folder.getChildFile (baseName + ".csv");
    csvFile.deleteFile();
    auto newCsv = std::make_unique<juce::FileOutputStream> (csvFile);
    if (! newCsv->openedOk())
        return false;
    for (const auto& line : header)
        *newCsv << "# " << line << "\n";
    *newCsv << "device_ms,arrival_ms,seq,bell,r4,r3,r2,r1,l3,l2,l1,thumb,pressure,flags\n";

    framesWritten = 0;
    audioStartMs = 0.0;
    {
        const juce::ScopedLock lock (writerLock);
        audioWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (writer.release(), writerThread, 32768);
        csv = std::move (newCsv);
    }
    recording = true;
    return true;
}

void Recorder::stop()
{
    if (! recording.exchange (false))
        return;
    const juce::ScopedLock lock (writerLock);
    audioWriter.reset();  // Flushes the rest to disk
    if (csv != nullptr)
    {
        *csv << "# audio started at arrival_ms " << juce::String (audioStartMs.load(), 3) << "\n";
        csv->flush();
        csv.reset();
    }
}

void Recorder::addFrame (const ReceivedFrame& r)
{
    if (! recording.load() || csv == nullptr)
        return;
    auto& out = *csv;
    out << juce::String (r.deviceMs, 3) << "," << juce::String (r.arrivalMs, 3) << "," << (int) r.frame.seq;
    for (auto h : r.frame.holes)
        out << "," << (int) h;
    out << "," << (int) r.frame.pressure << "," << (int) r.frame.flags << "\n";
}

void Recorder::addAudio (const float* const* channels, int numChannels, int numSamples)
{
    if (! recording.load())
        return;
    const juce::ScopedTryLock lock (writerLock);
    if (! lock.isLocked() || audioWriter == nullptr || numChannels < 2)
        return;
    if (framesWritten.load() == 0)
        audioStartMs = juce::Time::getMillisecondCounterHiRes();
    audioWriter->write (channels, numSamples);
    framesWritten += numSamples;
}
