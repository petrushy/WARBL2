// The presets hold tuned dimensions (from `WhistleCheck code`). This checks they are still in tune with the
// current acoustics: every chart note that the tuner sets is within 2 cents.

#include "Whistle/Acoustics.h"

#include <cmath>
#include <cstdio>

using namespace whistle;

int main()
{
    int failures = 0;
    for (const auto& g : presets())
    {
        const double lowest = g.chart == FingeringChart::baroqueRecorder ? 523.25 : (g.boreLength > 0.4 ? 293.66 : 587.33);
        for (const auto& c : chartFor (g.chart))
        {
            if (c.tunes == ChartFingering::kTunesNothing && c.tunesDiameter == ChartFingering::kTunesNothing)
                continue;
            const double cents = predictedCents (g, c, lowest);
            if (std::abs (cents) > 2.0)
            {
                std::printf ("FAILED %s, %s: %+.1f cents (retune with WhistleCheck code)\n", g.name.toRawUTF8(), c.name, cents);
                ++failures;
            }
        }
    }
    // A whistle moved from WARBL's L1-R3 sensors to L2-R4 is the same instrument: identical predicted tuning.
    for (const auto& preset : presets())
    {
        if (preset.chart != FingeringChart::whistle)
            continue;
        auto moved = preset;
        if (! moveFrontHoles (moved, 2) || firstFrontHole (moved) != 2 || moved.holes[1].enabled || ! moved.holes[7].enabled)
        {
            std::printf ("FAILED moving %s to L2-R4\n", preset.name.toRawUTF8());
            ++failures;
            continue;
        }
        for (const auto& c : chartFor (preset.chart))
        {
            const double before = predictedCents (preset, c, 587.33), after = predictedCents (moved, c, 587.33);
            if (std::abs (before - after) > 0.01)
            {
                std::printf ("FAILED %s on L2-R4, %s: %+.2f vs %+.2f cents\n", preset.name.toRawUTF8(), c.name, after, before);
                ++failures;
            }
        }
    }

    std::printf (failures == 0 ? "All presets in tune\n" : "%d notes out of tune\n", failures);
    return failures == 0 ? 0 : 1;
}
