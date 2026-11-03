#include "InstrumentFile.h"

namespace whistle
{

namespace
{
constexpr const char* kFormat = "warbl-whistle";
constexpr int kVersion = 1;
const char* kHoleNames[kNumHoles] = { "L1", "L2", "L3", "R1", "R2", "R3" };

juce::var object()
{
    return juce::var (new juce::DynamicObject());
}

void set (juce::var& o, const char* key, double value)
{
    o.getDynamicObject()->setProperty (key, value);
}

// Reads o[key] into value if it's there; lengths are stored in mm.
void read (const juce::var& o, const char* key, double& value, double scale = 1.0)
{
    if (o.hasProperty (key))
        value = (double) o[key] * scale;
}

void read (const juce::var& o, const char* key, float& value)
{
    if (o.hasProperty (key))
        value = (float) (double) o[key];
}
} // namespace

juce::var toVar (const Instrument& instrument)
{
    const auto& g = instrument.geometry;
    auto root = object();
    root.getDynamicObject()->setProperty ("format", kFormat);
    root.getDynamicObject()->setProperty ("version", kVersion);
    root.getDynamicObject()->setProperty ("name", g.name);

    auto bore = object();
    set (bore, "length_mm", g.boreLength * 1000.0);
    set (bore, "diameterTop_mm", g.boreDiameterTop * 1000.0);
    set (bore, "diameterFoot_mm", g.boreDiameterFoot * 1000.0);
    set (bore, "wallThickness_mm", g.wallThickness * 1000.0);
    set (bore, "wallLoss", g.wallLoss);
    root.getDynamicObject()->setProperty ("bore", bore);

    juce::Array<juce::var> holes;
    for (int i = 0; i < kNumHoles; ++i)
    {
        auto hole = object();
        hole.getDynamicObject()->setProperty ("name", kHoleNames[i]);
        set (hole, "position_mm", g.holes[(size_t) i].position * 1000.0);
        set (hole, "diameter_mm", g.holes[(size_t) i].diameter * 1000.0);
        holes.add (hole);
    }
    root.getDynamicObject()->setProperty ("holes", holes);

    auto fipple = object();
    set (fipple, "windwayHeight_mm", g.windwayHeight * 1000.0);
    set (fipple, "windowLength_mm", g.windowLength * 1000.0);
    set (fipple, "windowWidth_mm", g.windowWidth * 1000.0);
    set (fipple, "edgeOffset_mm", g.edgeOffset * 1000.0);
    root.getDynamicObject()->setProperty ("fipple", fipple);

    const auto& v = instrument.voicing;
    auto voicing = object();
    set (voicing, "jetStrouhal", v.jetStrouhal);
    set (voicing, "jetConvection", v.jetConvection);
    set (voicing, "breathNoise", v.breathNoise);
    set (voicing, "outputGain_dB", v.outputGainDb);
    root.getDynamicObject()->setProperty ("voicing", voicing);

    return root;
}

std::optional<Instrument> fromVar (const juce::var& root, juce::String& error)
{
    if (! root.isObject() || root["format"].toString() != kFormat)
    {
        error = "Not a WARBL whistle file";
        return std::nullopt;
    }
    if ((int) root["version"] > kVersion)
    {
        error = "Made by a newer version of WARBL Stream";
        return std::nullopt;
    }

    Instrument instrument;
    auto& g = instrument.geometry;
    g = presets().front();
    if (root.hasProperty ("name"))
        g.name = root["name"].toString();

    const auto& bore = root["bore"];
    read (bore, "length_mm", g.boreLength, 0.001);
    read (bore, "diameterTop_mm", g.boreDiameterTop, 0.001);
    read (bore, "diameterFoot_mm", g.boreDiameterFoot, 0.001);
    read (bore, "wallThickness_mm", g.wallThickness, 0.001);
    read (bore, "wallLoss", g.wallLoss);

    if (const auto* holes = root["holes"].getArray())
    {
        for (int i = 0; i < std::min (kNumHoles, holes->size()); ++i)
        {
            read ((*holes)[i], "position_mm", g.holes[(size_t) i].position, 0.001);
            read ((*holes)[i], "diameter_mm", g.holes[(size_t) i].diameter, 0.001);
        }
    }

    const auto& fipple = root["fipple"];
    read (fipple, "windwayHeight_mm", g.windwayHeight, 0.001);
    read (fipple, "windowLength_mm", g.windowLength, 0.001);
    read (fipple, "windowWidth_mm", g.windowWidth, 0.001);
    read (fipple, "edgeOffset_mm", g.edgeOffset, 0.001);

    const auto& voicing = root["voicing"];
    read (voicing, "jetStrouhal", instrument.voicing.jetStrouhal);
    read (voicing, "jetConvection", instrument.voicing.jetConvection);
    read (voicing, "breathNoise", instrument.voicing.breathNoise);
    read (voicing, "outputGain_dB", instrument.voicing.outputGainDb);

    // Sanity checks: anything that would make the model meaningless.
    auto bad = [&] (bool condition, const char* message)
    {
        if (condition && error.isEmpty())
            error = message;
        return condition;
    };
    bad (g.boreLength < 0.05 || g.boreLength > 2.0, "Bore length must be between 50 and 2000 mm");
    bad (g.boreDiameterTop < 0.002 || g.boreDiameterFoot < 0.002, "Bore diameters must be at least 2 mm");
    bad (g.windwayHeight <= 0.0 || g.windowLength <= 0.0 || g.windowWidth <= 0.0, "Fipple sizes must be positive");
    for (const auto& h : g.holes)
        bad (h.diameter <= 0.0 || h.position <= 0.0 || h.position >= g.boreLength, "Every hole needs a positive size and a position inside the bore");
    if (error.isNotEmpty())
        return std::nullopt;

    return instrument;
}

juce::String toJson (const Instrument& instrument)
{
    return juce::JSON::toString (toVar (instrument), false, 3);
}

std::optional<Instrument> fromJson (const juce::String& text, juce::String& error)
{
    juce::var parsed;
    const auto result = juce::JSON::parse (text, parsed);
    if (result.failed())
    {
        error = "Not valid JSON: " + result.getErrorMessage();
        return std::nullopt;
    }
    return fromVar (parsed, error);
}

} // namespace whistle
