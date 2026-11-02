#include "RoomReverb.h"

RoomReverb::RoomReverb() = default;

RoomReverb::~RoomReverb()
{
    generator.removeAllJobs (true, 10000);
}

void RoomReverb::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;
    convolution.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
    preDelay.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
    preDelay.setMaximumDelayInSamples ((int) (sampleRate * 0.2));
    work.setSize (2, maxBlockSize);
    wetGain.reset (sampleRate, 0.05);
    wetGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (wetDb.load(), -60.0f));

    if (presetIndex >= 0)
        startGenerating (presetIndex);  // The generated IR depends on the sample rate.
}

void RoomReverb::process (const float* input, float* left, float* right, int numSamples)
{
    jassert (numSamples <= work.getNumSamples());

    wetGain.setTargetValue (juce::Decibels::decibelsToGain (wetDb.load(), -60.0f));
    preDelay.setDelay ((float) (preDelayMs.load() * 0.001 * sampleRate));

    auto* a = work.getWritePointer (0);
    auto* b = work.getWritePointer (1);
    for (int i = 0; i < numSamples; ++i)
    {
        preDelay.pushSample (0, input[i]);
        a[i] = b[i] = preDelay.popSample (0);
    }

    juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 2, (size_t) numSamples);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = wetGain.getNextValue();
        left[i] += a[i] * g;
        right[i] += b[i] * g;
    }
}

void RoomReverb::loadPreset (int index)
{
    presetIndex = index;
    startGenerating (index);
}

void RoomReverb::startGenerating (int index)
{
    const auto& rooms = reverb::presets();
    if (index < 0 || index >= (int) rooms.size())
        return;

    const auto room = rooms[(size_t) index];
    const double rate = sampleRate;

    generator.removeAllJobs (false, 0);  // Drop a pending generation if the user is clicking through rooms.
    generator.addJob ([this, room, rate]
    {
        auto ir = reverb::generate (room, rate);
        // The generator already sets the level (direct sound = 1), so no normalising.
        convolution.loadImpulseResponse (std::move (ir), rate, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
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
    presetIndex = -1;
    convolution.loadImpulseResponse (std::move (ir), reader->sampleRate, juce::dsp::Convolution::Stereo::yes,
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
