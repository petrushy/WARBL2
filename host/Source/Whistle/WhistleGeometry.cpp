#include "WhistleGeometry.h"

namespace whistle
{

const std::vector<Material>& materials()
{
    static const std::vector<Material> list {
        { "Metal (brass, nickel)", 0.0005, 1.0 },
        { "Aluminium tube", 0.0010, 1.0 },
        { "Plastic (ABS, Delrin)", 0.0020, 1.05 },
        { "Wood", 0.0040, 1.3 },
    };
    return list;
}

Voicing presetVoicing (size_t index)
{
    // The recorder's jet band is set a little higher than the whistles' so its second- and third-octave notes
    // (high Bb, B, C ...) speak; the whistles' defaults give their register changes at ~1 and ~4 kPa.
    Voicing v;
    if (index < presets().size() && presets()[index].chart == FingeringChart::baroqueRecorder)
        v.jetStrouhal = 0.025f;
    return v;
}

bool moveFrontHoles (Geometry& g, int firstHole)
{
    std::vector<ToneHole> used;
    for (int i = 1; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            used.push_back (g.holes[(size_t) i]);
    if (firstHole < 1 || firstHole + (int) used.size() > kNumHoles)
        return false;

    for (int i = 1; i < kNumHoles; ++i)
        g.holes[(size_t) i] = {};
    for (size_t k = 0; k < used.size(); ++k)
        g.holes[(size_t) firstHole + k] = used[k];
    return true;
}

int firstFrontHole (const Geometry& g)
{
    for (int i = 1; i < kNumHoles; ++i)
        if (g.holes[(size_t) i].enabled)
            return i;
    return 0;
}

const std::vector<Geometry>& presets()
{
    auto make = [] (juce::String name, double length, double top, double foot, std::array<ToneHole, kNumHoles> holes,
                    double wall, double loss, double h, double w, double width)
    {
        Geometry g;
        g.name = std::move (name);
        g.boreLength = length;
        g.boreDiameterTop = top;
        g.boreDiameterFoot = foot;
        g.headDiameter = top;
        g.holes = holes;
        g.wallThickness = wall;
        g.wallLoss = loss;
        g.windwayHeight = h;
        g.windowLength = w;
        g.windowWidth = width;
        g.edgeOffset = 0.0001;
        return g;
    };

    // Bore lengths, hole positions and the cross-fingering hole sizes (whistles: L1 for C natural; recorder:
    // R1 for the F# fork, R3 for E'' and L3 for A'') come from the tuner: `WhistleCheck code` prints them. PresetTuningTest checks they are
    // still in tune with the current acoustics. Holes: LT, L1, L2, L3, R1, R2, R3, R4 as { used, position, diameter }.
    static const std::vector<Geometry> list = [&]
    {
        std::vector<Geometry> instruments {
            make ("D whistle, cylindrical metal", 0.26679, 0.0120, 0.0120,
                  { { { false, 0.0, 0.0 }, { true, 0.11921, 0.00402 }, { true, 0.14440, 0.00700 }, { true, 0.16425, 0.00650 },
                      { true, 0.19032, 0.00720 }, { true, 0.20065, 0.00680 }, { true, 0.22627, 0.00580 }, { false, 0.0, 0.0 } } },
                  0.0005, 1.0, 0.0010, 0.0050, 0.0080),
            make ("D whistle, conical metal", 0.26352, 0.0130, 0.0105,
                  { { { false, 0.0, 0.0 }, { true, 0.11603, 0.00396 }, { true, 0.14163, 0.00700 }, { true, 0.16171, 0.00650 },
                      { true, 0.18774, 0.00720 }, { true, 0.19854, 0.00680 }, { true, 0.22473, 0.00580 }, { false, 0.0, 0.0 } } },
                  0.0005, 1.0, 0.0010, 0.0050, 0.0080),
            [&]
            {
                // A narrower bore around the fipple, widening to the 12 mm bore over 30 mm: widens the octaves,
                // which in the model brings the played second register close to in tune (WhistleCheck head).
                auto g = make ("D whistle, narrow head", 0.26342, 0.0120, 0.0120,
                               { { { false, 0.0, 0.0 }, { true, 0.11778, 0.00405 }, { true, 0.14237, 0.00700 }, { true, 0.16183, 0.00650 },
                                   { true, 0.18758, 0.00720 }, { true, 0.19775, 0.00680 }, { true, 0.22312, 0.00580 }, { false, 0.0, 0.0 } } },
                               0.0005, 1.0, 0.0010, 0.0050, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.0105;
                return g;
            }(),
            make ("D whistle, wood", 0.25926, 0.0125, 0.0125,
                  { { { false, 0.0, 0.0 }, { true, 0.11835, 0.00672 }, { true, 0.14030, 0.00850 }, { true, 0.15909, 0.00800 },
                      { true, 0.18208, 0.00700 }, { true, 0.19543, 0.00920 }, { true, 0.21689, 0.00720 }, { false, 0.0, 0.0 } } },
                  0.0040, 1.3, 0.0010, 0.0050, 0.0085),
            [&]
            {
                auto g = make ("Low D whistle", 0.54129, 0.0220, 0.0220,
                               { { { false, 0.0, 0.0 }, { true, 0.24368, 0.00693 }, { true, 0.29238, 0.01100 }, { true, 0.33283, 0.01050 },
                                   { true, 0.38549, 0.01150 }, { true, 0.40603, 0.01100 }, { true, 0.45773, 0.00950 }, { false, 0.0, 0.0 } } },
                               0.0010, 1.0, 0.0016, 0.0090, 0.0150);
                g.windwayLength = 0.028;  // A bigger fipple
                return g;
            }(),
            [&]
            {
                // Soprano recorder in C with baroque fingering: reverse-conical bore (wide at the top), thumb hole on
                // the back, all seven front holes, thick walls and a wide windway. Typical dimensions, not a measured
                // instrument. R1, R3 and L3's sizes come from the tuner (F# fork, E'' and A''); the others are first guesses.
                auto g = make ("Soprano recorder in C (baroque fingering)", 0.29545, 0.0130, 0.0095,
                               { { { true, 0.10920, 0.00600 }, { true, 0.12361, 0.00600 }, { true, 0.13570, 0.00600 }, { true, 0.14961, 0.00455 },
                                   { true, 0.17122, 0.00403 }, { true, 0.20204, 0.00650 }, { true, 0.21228, 0.00453 }, { true, 0.24317, 0.00450 } } },
                               0.0035, 1.1, 0.0012, 0.0045, 0.0100);
                g.chart = FingeringChart::baroqueRecorder;
                g.windwayHeight = 0.0009;  // A thin windway lets the jet drive the top notes (WhistleCheck notes/chart)
                g.windwayLength = 0.030;  // The block is longer than a whistle's fipple plug
                g.bellClosesFoot = true;  // Top C# closes the foot (on the knee); without a bell sensor it stays open.
                return g;
            }(),
        };

        // Each whistle comes twice: on WARBL's L1-R3 sensors and on L2-R4 (the same instrument, played with
        // the lower six fingers).
        std::vector<Geometry> withSensors;
        for (const auto& g : instruments)
        {
            if (g.chart != FingeringChart::whistle)
            {
                withSensors.push_back (g);
                continue;
            }
            auto upper = g, lower = g;
            upper.name << ", holes L1-R3";
            lower.name << ", holes L2-R4";
            moveFrontHoles (lower, 2);
            withSensors.push_back (upper);
            withSensors.push_back (lower);
        }
        return withSensors;
    }();
    return list;
}

} // namespace whistle
