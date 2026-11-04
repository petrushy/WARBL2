#include "RoomReverb.h"

RoomReverb::RoomReverb() = default;

RoomReverb::~RoomReverb()
{
    generator.removeAllJobs (true, 10000);
}

void RoomReverb::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    for (auto& c : convolution)
        c.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    preDelay.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 4 });
    preDelay.setMaximumDelayInSamples ((int) (sampleRate * 0.2));
    work.setSize (4, maxBlockSize);
    wetGain.reset (sampleRate, 0.05);
    wetGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (wetDb.load(), -60.0f));

    if (presetIndex >= 0)
        startGenerating (presetIndex);  // The generated IR depends on the sample rate.
}

void RoomReverb::process (const float* windowLeft, const float* windowRight, const float* bodyLeft, const float* bodyRight,
                          float* left, float* right, int numSamples)
{
    jassert (numSamples <= work.getNumSamples());

    wetGain.setTargetValue (juce::Decibels::decibelsToGain (wetDb.load(), -60.0f));
    preDelay.setDelay ((float) (preDelayMs.load() * 0.001 * sampleRate));

    const float* inputs[4] = { windowLeft, windowRight, bodyLeft, bodyRight };
    for (int ch = 0; ch < 4; ++ch)
    {
        auto* w = work.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            preDelay.pushSample (ch, inputs[ch][i]);
            w[i] = preDelay.popSample (ch);
        }
    }

    for (int source = 0; source < 2; ++source)
    {
        float* channels[2] = { work.getWritePointer (2 * source), work.getWritePointer (2 * source + 1) };
        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) numSamples);
        convolution[(size_t) source].process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    const float* wl = work.getReadPointer (0);
    const float* wr = work.getReadPointer (1);
    const float* bl = work.getReadPointer (2);
    const float* br = work.getReadPointer (3);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = wetGain.getNextValue();
        left[i] += (wl[i] + bl[i]) * g;
        right[i] += (wr[i] + br[i]) * g;
    }
}

void RoomReverb::setSpread (float degrees)
{
    spreadDegrees = degrees;
    if (presetIndex >= 0)
        startGenerating (presetIndex);
}

void RoomReverb::loadPreset (int index)
{
    presetIndex = index;
    currentFile = juce::File();
    startGenerating (index);
}

void RoomReverb::startGenerating (int index)
{
    const auto& rooms = reverb::presets();
    if (index < 0 || index >= (int) rooms.size())
        return;

    const auto room = rooms[(size_t) index];
    const double rate = sampleRate;
    // The two sources sideways from straight ahead, at the room's listening distance.
    const double offset = room.distance * std::tan (0.5 * spreadDegrees.load() * juce::MathConstants<double>::pi / 180.0);

    generator.removeAllJobs (false, 0);  // Drop a pending generation if the user is clicking through rooms.
    generator.addJob ([this, room, rate, offset]
    {
        // Each source its own place and its own (independent) diffuse tail. The generator already sets the
        // level (direct sound = 1), so no normalising.
        for (int source = 0; source < 2; ++source)
            convolution[(size_t) source].loadImpulseResponse (reverb::generate (room, rate, (uint32_t) source + 1, source == 0 ? -offset : offset),
                                                              rate, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                                              juce::dsp::Convolution::Normalise::no);
        const juce::ScopedLock lock (nameLock);
        currentName = room.name;
    });
}

juce::String RoomReverb::loadFile (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Can't read " + file.getFileName();
    if (reader->lengthInSamples > (juce::int64) (reader->sampleRate * 20.0))
        return "IR is longer than 20 s";

    juce::AudioBuffer<float> ir (2, (int) reader->lengthInSamples);
    reader->read (&ir, 0, (int) reader->lengthInSamples, 0, true, true);  // Mono files are copied to both channels.

    // Measured IRs start with the direct sound and have arbitrary level: trim the silence before it and normalise.
    // One measured position only, so both sources share it.
    presetIndex = -1;
    currentFile = file;
    for (auto& c : convolution)
        c.loadImpulseResponse (juce::AudioBuffer<float> (ir), reader->sampleRate, juce::dsp::Convolution::Stereo::yes,
                               juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
    const juce::ScopedLock lock (nameLock);
    currentName = file.getFileNameWithoutExtension();
    return {};
}

juce::String RoomReverb::getCurrentName() const
{
    const juce::ScopedLock lock (nameLock);
    return currentName;
}
