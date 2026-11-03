// Saving and loading a whistle design as JSON: geometry (lengths in millimetres, so the file is easy to read and
// edit by hand), voicing and output level. Fields that are missing keep the value of the first preset, so
// older or hand-written files still load.

#pragma once

#include "WhistleGeometry.h"

#include <optional>

namespace whistle
{

struct Voicing
{
    float jetStrouhal = 0.02f;
    float jetConvection = 0.6f;
    float breathNoise = 0.25f;
    float outputGainDb = 0.0f;
};

struct Instrument
{
    Geometry geometry;
    Voicing voicing;
};

juce::var toVar (const Instrument&);
std::optional<Instrument> fromVar (const juce::var&, juce::String& error);

juce::String toJson (const Instrument&);
std::optional<Instrument> fromJson (const juce::String&, juce::String& error);

} // namespace whistle
