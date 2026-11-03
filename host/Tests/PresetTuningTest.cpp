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
    std::printf (failures == 0 ? "All presets in tune\n" : "%d notes out of tune\n", failures);
    return failures == 0 ? 0 : 1;
}
