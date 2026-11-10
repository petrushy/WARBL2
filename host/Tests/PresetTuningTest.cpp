// The presets hold tuned dimensions (from `WhistleCheck code`). This checks they are still in tune with the
// current acoustics: a fresh least-squares fit can't improve on them.

#include "Whistle/Acoustics.h"

#include <cmath>
#include <cstdio>

using namespace whistle;

int main()
{
    int failures = 0;
    bool foundAlto = false, foundTenor = false;
    for (const auto& g : presets())
    {
        const double lowest = presetLowestNote (g);
        const bool alto = g.name.startsWith ("Alto recorder"), tenor = g.name.startsWith ("Tenor recorder");
        foundAlto = foundAlto || alto;
        foundTenor = foundTenor || tenor;
        if ((alto || tenor) && (g.chart != FingeringChart::baroqueRecorder || ! g.bellClosesFoot
                               || std::abs (lowest - (alto ? 349.228 : 261.626)) > 0.01))
        {
            std::printf ("FAILED recorder key/chart: %s, %.2f Hz\n", g.name.toRawUTF8(), lowest);
            ++failures;
        }
        // Refitting has to find next to nothing: the preset is at the tuner's minimum.
        const auto now = tuningError (g, lowest);
        auto refitted = g;
        fitTuning (refitted, lowest, true);
        const auto best = tuningError (refitted, lowest);
        if (now.rms - best.rms > 0.5)
        {
            std::printf ("FAILED %s: %.1f cents rms, a fit reaches %.1f (retune with WhistleCheck code)\n", g.name.toRawUTF8(), now.rms, best.rms);
            ++failures;
        }
    }
    if (! foundAlto || ! foundTenor)
    {
        std::printf ("FAILED missing alto or tenor recorder preset\n");
        ++failures;
    }
    // A whistle moved from WARBL's L1-R3 sensors to L2-R4 is the same instrument: identical predicted tuning.
    for (const auto& preset : presets())
    {
        if (preset.chart == FingeringChart::baroqueRecorder)
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
