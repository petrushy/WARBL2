#include "WhistleGeometry.h"

#include "Acoustics.h"

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
        g.holes = holes;
        g.wallThickness = wall;
        g.wallLoss = loss;
        g.windwayHeight = h;
        g.windowLength = w;
        g.windowWidth = width;
        g.edgeOffset = 0.0001;
        return g;
    };

    // Hole positions and bore lengths below are starting points; they are tuned when the list is first made.
    static const std::vector<Geometry> list = [&]
    {
        std::vector<Geometry> whistles {
        make ("D whistle, cylindrical metal", 0.2550, 0.0120, 0.0120,
              { { { 0.1010, 0.0065 }, { 0.1235, 0.0070 }, { 0.1445, 0.0065 }, { 0.1730, 0.0072 }, { 0.1960, 0.0068 }, { 0.2170, 0.0058 } } },
              0.0005, 1.0, 0.0010, 0.0050, 0.0080),
        make ("D whistle, conical metal", 0.2550, 0.0130, 0.0105,
              { { { 0.1010, 0.0065 }, { 0.1235, 0.0070 }, { 0.1445, 0.0065 }, { 0.1730, 0.0072 }, { 0.1960, 0.0068 }, { 0.2170, 0.0058 } } },
              0.0005, 1.0, 0.0010, 0.0050, 0.0080),
        make ("D whistle, wood", 0.2550, 0.0125, 0.0125,
              { { { 0.1010, 0.0080 }, { 0.1235, 0.0085 }, { 0.1445, 0.0080 }, { 0.1730, 0.0070 }, { 0.1960, 0.0092 }, { 0.2170, 0.0072 } } },
              0.0040, 1.3, 0.0010, 0.0050, 0.0085),
        make ("Low D whistle", 0.5300, 0.0220, 0.0220,
              { { { 0.2050, 0.0100 }, { 0.2500, 0.0110 }, { 0.2920, 0.0105 }, { 0.3500, 0.0115 }, { 0.3960, 0.0110 }, { 0.4380, 0.0095 } } },
              0.0010, 1.0, 0.0016, 0.0090, 0.0150),
        };
        for (auto& g : whistles)
            tuneToScale (g, g.boreLength > 0.4 ? 293.66 : 587.33);  // D4 / D5
        return whistles;
    }();
    return list;
}

} // namespace whistle
