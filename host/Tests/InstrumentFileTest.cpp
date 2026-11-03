// Whistle files: every preset survives a save/load round trip, partial files fill in defaults, and broken or
// meaningless files are rejected with a message.

#include "Whistle/InstrumentFile.h"

#include <cmath>
#include <cstdio>

using namespace whistle;

static int failures = 0;

#define CHECK(condition)                                                \
    do                                                                  \
    {                                                                   \
        if (! (condition))                                              \
        {                                                               \
            std::printf ("FAILED line %d: %s\n", __LINE__, #condition); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static bool near (double a, double b) { return std::abs (a - b) < 1.0e-6; }  // Files keep 0.001 mm

int main()
{
    // Round trip of every preset, with non-default voicing.
    for (const auto& preset : presets())
    {
        Instrument in;
        in.geometry = preset;
        in.voicing = { 0.031f, 0.55f, 0.4f, -3.5f };

        juce::String error;
        const auto out = fromJson (toJson (in), error);
        CHECK (out.has_value());
        CHECK (error.isEmpty());
        if (! out)
            continue;

        const auto& a = in.geometry;
        const auto& b = out->geometry;
        CHECK (b.name == a.name);
        CHECK (near (a.boreLength, b.boreLength) && near (a.boreDiameterTop, b.boreDiameterTop) && near (a.boreDiameterFoot, b.boreDiameterFoot));
        CHECK (near (a.wallThickness, b.wallThickness) && near (a.wallLoss, b.wallLoss));
        for (int i = 0; i < kNumHoles; ++i)
        {
            CHECK (a.holes[(size_t) i].enabled == b.holes[(size_t) i].enabled);
            if (a.holes[(size_t) i].enabled)
                CHECK (near (a.holes[(size_t) i].position, b.holes[(size_t) i].position) && near (a.holes[(size_t) i].diameter, b.holes[(size_t) i].diameter));
        }
        CHECK (a.chart == b.chart && a.bellClosesFoot == b.bellClosesFoot);
        CHECK (near (a.headLength, b.headLength) && near (a.headDiameter, b.headDiameter));
        CHECK (near (a.windwayHeight, b.windwayHeight) && near (a.windowLength, b.windowLength) && near (a.windowWidth, b.windowWidth)
               && near (a.edgeOffset, b.edgeOffset));
        CHECK (near (out->voicing.jetStrouhal, 0.031f) && near (out->voicing.jetConvection, 0.55f) && near (out->voicing.breathNoise, 0.4f)
               && near (out->voicing.outputGainDb, -3.5f));
    }

    // A file with only some fields: the rest come from the first preset.
    {
        juce::String error;
        const auto partial = fromJson (R"({ "format": "warbl-whistle", "version": 1, "name": "Short", "bore": { "length_mm": 250 } })", error);
        CHECK (partial.has_value());
        if (partial)
        {
            CHECK (partial->geometry.name == "Short");
            CHECK (near (partial->geometry.boreLength, 0.25));
            CHECK (near (partial->geometry.boreDiameterTop, presets().front().boreDiameterTop));
            CHECK (near (partial->geometry.holes[3].position, presets().front().holes[3].position));
        }
    }

    // A file from before the thumb and R4 holes: six named holes, the others plugged.
    {
        juce::String error;
        const auto old = fromJson (R"({ "format": "warbl-whistle", "version": 1, "holes": [
            { "name": "L1", "position_mm": 120, "diameter_mm": 6 }, { "name": "L2", "position_mm": 140, "diameter_mm": 7 },
            { "name": "L3", "position_mm": 160, "diameter_mm": 6.5 }, { "name": "R1", "position_mm": 190, "diameter_mm": 7 },
            { "name": "R2", "position_mm": 200, "diameter_mm": 7 }, { "name": "R3", "position_mm": 225, "diameter_mm": 6 } ] })", error);
        CHECK (old.has_value());
        if (old)
        {
            CHECK (! old->geometry.holes[0].enabled && ! old->geometry.holes[7].enabled);
            CHECK (old->geometry.holes[1].enabled && near (old->geometry.holes[1].position, 0.120));
            CHECK (old->geometry.chart == FingeringChart::whistle);
        }
    }

    // Broken or meaningless files are rejected with a reason.
    auto rejected = [] (const char* json)
    {
        juce::String error;
        const auto result = fromJson (json, error);
        return ! result.has_value() && error.isNotEmpty();
    };
    CHECK (rejected ("{ not json"));
    CHECK (rejected (R"({ "format": "something else" })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 99 })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "bore": { "length_mm": 10 } })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "holes": [ { "name": "L1", "position_mm": 900, "diameter_mm": 6 } ] })"));
    CHECK (rejected (R"({ "format": "warbl-whistle", "version": 1, "fipple": { "windwayHeight_mm": 0 } })"));

    std::printf (failures == 0 ? "All instrument file tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
