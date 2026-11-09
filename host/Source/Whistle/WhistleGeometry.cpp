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
    // The recorder's jet band is set higher than the whistles' so its second- and third-octave notes speak at
    // lower breath (top C at ~3 kPa, C6 at ~0.6 kPa; at 0.035 the low C goes sharp); the whistles' defaults give
    // their register changes at ~1 and ~4 kPa. One band-pass stage: no silent pressures between the registers.
    Voicing v;

    // Searched against the recorded whistles (first octave H2/H3 about -17 dB, second octave -32/-28) and for steady
    // playing. One band-pass stage is what steadies quick fingering changes: register jumps at soft breath went from
    // 7 of 14 to 1, settling from ~200 ms to ~6 ms. Jet spreading makes the second octave purer and louder, less
    // vortex loss gives the first octave back its odd harmonics, and slow-jet growth separates the two octaves.
    // (Their edge offsets are set in presets().) The low D keeps its own: spreading didn't help it.
    struct Fitted { const char* namePart; float spreading, slowGrowth, gain; };
    static constexpr Fitted fitted[] = { { "cylindrical metal", 3.0f, 0.25f, 1.0f },
                                         { "conical metal", 2.0f, 0.5f, 1.0f },
                                         { "narrow head", 3.0f, 0.5f, 1.15f },
                                         { "traditional scale", 2.0f, 0.25f, 1.0f } };
    if (index < presets().size())
        for (const auto& f : fitted)
            if (presets()[index].name.contains (f.namePart))
            {
                v.jetSpreading = f.spreading;
                v.slowJetGrowth = f.slowGrowth;
                v.jetGain = f.gain;
                v.vortexLoss = 0.6f;
                v.jetStages = 1;
            }

    if (index < presets().size() && (presets()[index].name.startsWith ("D whistle, Goldie style") || presets()[index].name.contains ("whistle, wood")))
    {
        // Fitted to Real-examples/High-D-Goldie.wav (WhistleCheck analyse): the thin windway wants a lower jet band,
        // one band-pass stage (no silent pressures between registers), and the jet settings that make a softly blown
        // low note swing far across the edge (rich, as the recording's bottom D) while the hard-blown second octave
        // stays nearly pure.
        v.jetStrouhal = 0.017f;
        v.jetStages = 1;
        v.slowJetGrowth = 0.25f;
        v.jetGain = 1.1f;  // 1.3 chirped through another register as a hole opened gradually (WhistleCheck glide)
        // The jet widens as it turns turbulent (the second octave's faster jet more): that octave comes out louder
        // and purer, as on the recorded tune (second-octave H2 -30 dB, was -17; tune -34). Less vortex loss gives
        // the softer first octave back its odd harmonics.
        v.jetSpreading = 3.0f;
        v.vortexLoss = 0.6f;
    }
    if (index < presets().size() && presets()[index].name.startsWith ("Low D"))
    {
        // The low whistle (see its geometry): a jet band low enough for its registers to sit where a low D is
        // blown (low D ~120-670 Pa, the octave from ~670 Pa), one band-pass stage (the pitch follows the breath
        // half as much as with two), and a lower vortex loss, which lets the low notes swing further across the edge:
        // the reedy odd harmonics of a real low D (WhistleCheck analyse with Real-examples/Low-D-Goldie.wav).
        v.jetStrouhal = 0.0095f;
        v.jetStages = 1;
        v.vortexLoss = 0.4f;
        v.jetGain = 0.75f;  // With the octave in tune, more let quick fingering changes flip into it on the way
    }
    if (index < presets().size() && presets()[index].chart == FingeringChart::baroqueRecorder)
    {
        v.jetStrouhal = 0.03f;
        v.jetStages = 1;
        // A slow jet grows more (low notes speak from the softest breath, no gap before the octave), a fast one less
        // (a purer second octave).
        v.slowJetGrowth = 0.67f;
        v.breathNoise = 0.025f;  // A recorder's narrow, smooth windway: a clean tone with little hiss
        // Its scale is in tune at ~0.4-0.6 of a whistle's pressures (low C 170 Pa, the whistle's low D 400 Pa), up
        // to top C at 3.7 kPa: 0.7 of WARBL's breath puts the low notes at a gentle breath and keeps top C in reach
        // (WARBL's full breath gives ~5.5 kPa).
        v.breathScale = 0.7f;
    }
    // A finger landing: the air it pushes in as computed, and the body's resonances by material (first guesses until
    // measured: wood low and short, thin metal higher and ringing).
    if (index < presets().size())
    {
        const auto& name = presets()[index].name;
        const bool wooden = name.contains ("wood") || name.contains ("recorder") || name.contains ("traditional scale");
        v.fingerTap = 1.0f;
        v.bodyKnock = 1.0f;
        v.bodyPitch = wooden ? 900.0f : 2300.0f;
        v.bodyRing = wooden ? 12.0f : 50.0f;
    }
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

double presetLowestNote (const Geometry& g)
{
    const double at440 = g.chart == FingeringChart::baroqueRecorder ? 523.25
                       : g.chart == FingeringChart::harjedalspipa  ? 466.16
                       : g.boreLength > 0.4                        ? 293.66
                                                                   : 587.33;
    return at440 * g.referenceA / 440.0;
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
            make ("D whistle, wood", 0.25647, 0.0125, 0.0125,
                  { { { false, 0.0, 0.0 }, { true, 0.11588, 0.00672 }, { true, 0.13773, 0.00850 }, { true, 0.15646, 0.00800 },
                      { true, 0.17938, 0.00700 }, { true, 0.19271, 0.00920 }, { true, 0.21411, 0.00720 }, { false, 0.0, 0.0 } } },
                  0.0040, 1.3, 0.00087, 0.0042, 0.0080),  // The Goldie style's fipple: the easiest to play
            [&]
            {
                // In the style of a Goldie high D (not measured): a thin aluminium tube, a narrow head (as the
                // narrow-head preset), a low windway ceiling (Goldie's "medium" is ~0.87 mm) and a short window.
                auto g = make ("D whistle, Goldie style", 0.26324, 0.0120, 0.0120,
                               { { { false, 0.0, 0.0 }, { true, 0.11715, 0.00449 }, { true, 0.14123, 0.00700 }, { true, 0.16070, 0.00650 },
                                   { true, 0.18684, 0.00720 }, { true, 0.19674, 0.00680 }, { true, 0.22180, 0.00580 }, { false, 0.0, 0.0 } } },
                               0.0010, 1.0, 0.00087, 0.0042, 0.0080);
                g.headLength = 0.030;
                g.headDiameter = 0.0115;  // Narrow, but less than the narrow-head preset's 10.5 mm: its octave played 20-30 c sharp
                g.edgeOffset = 0.00015;   // Into the pipe: the second harmonic of the recorded Goldie's low notes
                return g;
            }(),
            [&]
            {
                // Low D whistle: a 22 mm cylindrical bore (a Goldie low D measures ~21.8), a 1.0 mm windway (Goldie
                // medium blowers 0.9-1.0 mm) and a 13 mm wide window, as forum measurements of low whistles give. The
                // window is short (5.2 mm, Overton style): a longer one let the jet grow so much that quick fingering
                // changes flipped into the octave on the way (WhistleCheck transition).
                auto g = make ("Low D whistle", 0.53008, 0.0220, 0.0220,
                               { { { false, 0.0, 0.0 }, { true, 0.23128, 0.00693 }, { true, 0.28032, 0.01100 }, { true, 0.32098, 0.01050 },
                                   { true, 0.37384, 0.01150 }, { true, 0.39448, 0.01100 }, { true, 0.44635, 0.00950 }, { false, 0.0, 0.0 } } },
                               0.0010, 1.0, 0.0010, 0.0052, 0.0130);
                g.windwayLength = 0.028;  // A bigger fipple
                // A slightly wider head (24 mm over the top 60 mm) flattens the second octave into tune: with the
                // plain bore it played 10-18 cents sharp (WhistleCheck chart "low D" headlength= headdiameter=).
                g.headLength = 0.060;
                g.headDiameter = 0.024;
                return g;
            }(),
            [&]
            {
                // Härjedalspipa (Härjedalen, Sweden): a wooden duct flute with a cylindrical bore and six finger holes,
                // in the traditional scale on A# with its neutral thirds and sevenths (see chartFor). Typical
                // dimensions, scaled from the wooden whistle, not a measured instrument; holes placed by the tuner.
                // R1 small and R2 large (4 and 10 mm) keep D+35 and D#, only 65 cents apart, about 20 mm apart.
                auto g = make (juce::String::fromUTF8 ("Härjedalspipa (A#, traditional scale)"), 0.33542, 0.0130, 0.0130,
                               { { { false, 0.0, 0.0 }, { true, 0.15612, 0.00700 }, { true, 0.18242, 0.00700 }, { true, 0.20466, 0.00700 },
                                   { true, 0.22923, 0.00400 }, { true, 0.24933, 0.01000 }, { true, 0.27676, 0.00500 }, { false, 0.0, 0.0 } } },
                               0.0040, 1.3, 0.0010, 0.0055, 0.0090);
                g.chart = FingeringChart::harjedalspipa;
                g.windwayLength = 0.022;
                return g;
            }(),
            [&]
            {
                // Soprano recorder in C with baroque fingering: reverse-conical bore (wide at the top), thumb hole on
                // the back, all seven front holes, thick walls and a wide windway. Typical dimensions, not a measured
                // instrument. The sizes of the thumb hole, L3, R1 and R3 come from the tuner (high Bb, high A, the F#
                // fork and high B); L2's (5 mm) puts the Bb fork in tune; L1's, R2's and R4's are first guesses.
                auto g = make ("Soprano recorder in C (baroque fingering)", 0.29147, 0.0130, 0.0095,
                               { { { true, 0.10313, 0.00533 }, { true, 0.11984, 0.00600 }, { true, 0.12810, 0.00503 }, { true, 0.14534, 0.00449 },
                                   { true, 0.16734, 0.00402 }, { true, 0.19821, 0.00650 }, { true, 0.21303, 0.00547 }, { true, 0.23932, 0.00450 } } },
                               0.0035, 1.1, 0.0010, 0.0037, 0.0088);  // Window 3.7 x 8.8 mm, as a measured soprano (NMMSD)
                g.chart = FingeringChart::baroqueRecorder;
                // A short window for the jet's thickness (W/h 3.7, a whistle's ~5) grows the jet less on its way to the
                // edge: it switches less fully, a purer tone, and the top notes speak at lower breath.
                // The edge nearly on the jet's centre line: weak even harmonics, as on baroque recorders.
                g.edgeOffset = 0.00005;
                g.windwayLength = 0.030;  // The block is longer than a whistle's fipple plug
                g.bellClosesFoot = true;  // Top C# closes the foot (on the knee); without a bell sensor it stays open.
                return g;
            }(),
        };

        // Edge offsets found with the voicings in presetVoicing() (m, into the pipe).
        for (auto& g : instruments)
        {
            if (g.name.contains ("cylindrical metal") || g.name.contains ("traditional scale"))
                g.edgeOffset = 0.00015;
            else if (g.name.contains ("conical metal") || g.name.contains ("narrow head"))
                g.edgeOffset = 0.00025;
            else if (g.name.contains ("whistle, wood"))
                g.edgeOffset = 0.00015;  // As the Goldie style, whose fipple it shares
        }

        // Each whistle comes twice: on WARBL's L1-R3 sensors and on L2-R4 (the same instrument, played with
        // the lower six fingers).
        std::vector<Geometry> withSensors;
        for (const auto& g : instruments)
        {
            if (g.chart == FingeringChart::baroqueRecorder)  // Uses all eight holes
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
