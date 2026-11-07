#include "ModelPage.h"

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour grid { 0xff3a3d42 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour dim { 0xff8d9299 };
const juce::Colour accent { 0xff4fa3e0 };     // The jet and what it does
const juce::Colour warm { 0xffe0a24f };       // The sound in the pipe
const juce::Colour metal { 0xff9aa3ad };
const juce::Colour green { 0xff7cc47c };

constexpr double kMinHz = 150.0, kMaxHz = 6000.0;
constexpr double kSlowMotionHz = 0.4;  // Shown cycles per second: the jet in slow motion

juce::String noteName (double hz, double referenceA)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / referenceA));
    return juce::String (names[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1);
}

void title (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& heading, const juce::String& explanation)
{
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (heading, area.removeFromTop (16.0f), juce::Justification::left);
    g.setColour (dim);
    g.setFont (11.0f);
    g.drawFittedText (explanation, area.removeFromTop (28.0f).toNearestInt(), juce::Justification::topLeft, 2);
}

float xForHz (double hz, juce::Rectangle<float> r)
{
    return r.getX() + r.getWidth() * (float) (std::log (hz / kMinHz) / std::log (kMaxHz / kMinHz));
}
} // namespace

ModelPage::ModelPage (AudioEngine& a) : audio (a)
{
    for (int k = 0; k < 500; ++k)
        impedanceHz.push_back (kMinHz * std::pow (kMaxHz / kMinHz, k / 499.0));
}

ModelPage::~ModelPage()
{
    audio.whistleModel.scope.enabled = false;
}

void ModelPage::visibilityChanged()
{
    const bool showing = isShowing();
    audio.whistleModel.scope.enabled = showing;
    if (showing)
        startTimerHz (30);
    else
        stopTimer();
}

void ModelPage::timerCallback()
{
    const auto before = hold();
    takeSnapshot();
    analyse();
    const bool found = playing;
    framesFound = found ? framesFound + 1 : 0;
    framesMissed = found ? 0 : framesMissed + 1;
    const bool show = showingNote ? framesMissed < kFramesToHide : framesFound >= kFramesToShow;
    if (show && ! found)
        restore (before);  // Keep showing the last note, still in slow motion
    else if (! show)
        playing = false;
    showingNote = show;
    updateImpedance();
    repaint();
}

ModelPage::Held ModelPage::hold() const
{
    return { snap, playing, period, cycleStart, swingMin, swingMax, switched, driveRms, vortexRms, levelDb, harmonicsDb };
}

void ModelPage::restore (const Held& h)
{
    snap = h.snap;
    playing = h.playing;
    period = h.period;
    cycleStart = h.cycleStart;
    swingMin = h.swingMin;
    swingMax = h.swingMax;
    switched = h.switched;
    driveRms = h.driveRms;
    vortexRms = h.vortexRms;
    levelDb = h.levelDb;
    harmonicsDb = h.harmonicsDb;
    slowMotion = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001 * kSlowMotionHz, 1.0);
    shownTime = cycleStart + slowMotion * period;
}

void ModelPage::takeSnapshot()
{
    auto& scope = audio.whistleModel.scope;
    const int written = scope.written.load (std::memory_order_acquire);
    const int n = std::min (written, (int) whistle::WhistleModel::Scope::size);
    auto copy = [&] (const auto& from, std::vector<float>& to)
    {
        to.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            to[(size_t) i] = from[(size_t) ((written - n + i) & (whistle::WhistleModel::Scope::size - 1))].load (std::memory_order_relaxed);
    };
    copy (scope.deflection, snap.deflection);
    copy (scope.jetFlow, snap.jetFlow);
    copy (scope.windowFlow, snap.windowFlow);
    copy (scope.drive, snap.drive);
    copy (scope.vortex, snap.vortex);
    copy (scope.sound, snap.sound);
    snap.rate = scope.rate.load();
    snap.pressure = scope.mouthPressure.load();
    snap.jetSpeed = scope.jetSpeed.load();
    snap.flowShare = scope.flowShare.load();
    snap.travel = scope.travelSamples.load();
    snap.exponent = scope.growthExponent.load();
    snap.bandCentre = scope.bandCentre.load();
    snap.slowBoost = scope.slowBoost.load();
    snap.edge = scope.edge.load();
    snap.halfWidth = scope.halfWidth.load();
    snap.jetGain = audio.whistleModel.jetGain.load();
    snap.jetQ = audio.whistleModel.jetQ.load();
    snap.stages = audio.whistleModel.jetStages.load();
    const int junctions = scope.junctions.load();
    snap.junctionX.resize ((size_t) junctions);
    snap.junctionRms.resize ((size_t) junctions);
    snap.junctionSign.resize ((size_t) junctions);
    for (int j = 0; j < junctions; ++j)
    {
        snap.junctionX[(size_t) j] = scope.junctionX[(size_t) j].load();
        snap.junctionRms[(size_t) j] = scope.junctionRms[(size_t) j].load();
        snap.junctionSign[(size_t) j] = scope.junctionSign[(size_t) j].load();
    }
    geometry = audio.whistleModel.getGeometry();
}

float ModelPage::at (const std::vector<float>& x, double index) const
{
    if (x.empty())
        return 0.0f;
    index = juce::jlimit (0.0, (double) x.size() - 1.001, index);
    const auto i = (size_t) index;
    const float f = (float) (index - (double) i);
    return x[i] + f * (x[std::min (i + 1, x.size() - 1)] - x[i]);
}

void ModelPage::analyse()
{
    playing = false;
    const auto n = snap.windowFlow.size();
    if (n < 2048)
        return;

    // Pitch from the flow through the window (mostly the fundamental): upward zero crossings with hysteresis.
    double mean = 0.0, power = 0.0;
    for (auto v : snap.windowFlow) mean += v;
    mean /= (double) n;
    for (auto v : snap.windowFlow) power += (v - mean) * (v - mean);
    const double rms = std::sqrt (power / (double) n);
    std::vector<double> crossings;
    bool below = false;
    for (size_t i = 1; i < n; ++i)
    {
        const double v = snap.windowFlow[i] - mean;
        if (v < -0.3 * rms)
            below = true;
        else if (below && v >= 0.0 && snap.windowFlow[i - 1] - mean < 0.0)
        {
            const double previous = snap.windowFlow[i - 1] - mean;
            crossings.push_back ((double) i - 1.0 + (-previous) / (v - previous));
            below = false;
        }
    }

    // Level of the sound (internal rate, before the output gain)
    double soundPower = 0.0;
    for (size_t i = n / 2; i < n; ++i)
        soundPower += (double) snap.sound[i] * snap.sound[i];
    levelDb = (float) (10.0 * std::log10 (soundPower / (double) (n / 2) / 4.0 + 1.0e-12));  // 1.0 = 2 Pa

    if (crossings.size() < 6 || rms < 1.0e-9)
        return;
    period = (crossings.back() - crossings[crossings.size() - 5]) / 4.0;
    if (period < 4.0 || period > 2000.0)
        return;
    // Regular? (A register change or noise gives scattered crossings.)
    for (size_t k = crossings.size() - 4; k < crossings.size(); ++k)
        if (std::abs (crossings[k] - crossings[k - 1] - period) > 0.15 * period)
            return;
    playing = true;

    // The moment shown: slowly advancing through one period, from a crossing early enough that the jet leaving the
    // flue then has already reached the edge in the snapshot.
    slowMotion = std::fmod (juce::Time::getMillisecondCounterHiRes() * 0.001 * kSlowMotionHz, 1.0);
    const double latest = (double) n - 2.0 - snap.travel - period;
    cycleStart = crossings.front();
    for (auto c : crossings)
        if (c <= latest)
            cycleStart = c;
    shownTime = cycleStart + slowMotion * period;

    // Over the last few whole periods: the jet's swing across the edge, the source's parts, and the harmonics.
    const int periods = std::max (1, (int) ((double) n * 0.45 / period));
    const double from = cycleStart - periods * period + period;
    const double to = cycleStart + period;
    const float halfWidth = std::max (1.0e-5f, snap.halfWidth);
    swingMin = 1.0e9f;
    swingMax = -1.0e9f;
    int count = 0, across = 0;
    double drivePower = 0.0, vortexPower = 0.0;
    for (auto i = (size_t) std::max (0.0, from); i < (size_t) to && i < n; ++i)
    {
        const float x = (snap.deflection[i] - snap.edge) / halfWidth;
        swingMin = std::min (swingMin, x);
        swingMax = std::max (swingMax, x);
        across += std::abs (x) > 1.0f ? 1 : 0;
        drivePower += (double) snap.drive[i] * snap.drive[i];
        vortexPower += (double) snap.vortex[i] * snap.vortex[i];
        ++count;
    }
    switched = count > 0 ? (float) across / (float) count : 0.0f;
    driveRms = count > 0 ? (float) std::sqrt (drivePower / count) : 0.0f;
    vortexRms = count > 0 ? (float) std::sqrt (vortexPower / count) : 0.0f;

    double h1 = 0.0;
    for (int k = 1; k <= 10; ++k)
    {
        double re = 0.0, im = 0.0;
        for (auto i = (size_t) std::max (0.0, from); i < (size_t) to && i < n; ++i)
        {
            const double phase = 2.0 * juce::MathConstants<double>::pi * k * ((double) i - from) / period;
            re += snap.sound[i] * std::cos (phase);
            im += snap.sound[i] * std::sin (phase);
        }
        const double amplitude = std::hypot (re, im);
        if (k == 1)
            h1 = std::max (amplitude, 1.0e-12);
        harmonicsDb[(size_t) k] = (float) (20.0 * std::log10 (amplitude / h1 + 1.0e-9));
    }
}

void ModelPage::updateImpedance()
{
    // Recompute only when the fingering or the instrument changed, at most a few times a second.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    juce::String signature;
    signature << geometry.boreLength << geometry.windowLength << geometry.windwayHeight << geometry.boreDiameterTop << geometry.boreDiameterFoot;
    for (const auto& h : geometry.holes)
        signature << (h.enabled ? "+" : "-") << h.position << h.diameter;
    bool changed = signature != impedanceGeometry || std::abs (audio.shownFootOpenness.load() - impedanceFoot) > 0.05;
    for (int i = 0; i < whistle::kNumHoles; ++i)
        changed = changed || std::abs (audio.shownOpenness[(size_t) i].load() - impedanceOpenness[(size_t) i]) > 0.05;
    if (! changed || now - lastImpedanceMs < 200.0)
        return;
    lastImpedanceMs = now;
    impedanceGeometry = signature;

    whistle::Openness open;
    for (int i = 0; i < whistle::kNumHoles; ++i)
        open.holes[(size_t) i] = impedanceOpenness[(size_t) i] = audio.shownOpenness[(size_t) i].load();
    open.foot = impedanceFoot = audio.shownFootOpenness.load();

    impedanceDb.clear();
    for (auto hz : impedanceHz)
        // Admittance: the jet sees the bore and the window in series, so it plays at minima of their impedance.
        impedanceDb.push_back ((float) (-20.0 * std::log10 (std::abs (whistle::windowImpedance (geometry, open, hz)) + 1.0)));
    resonanceHz.clear();
    const float top = impedanceDb.empty() ? 0.0f : *std::max_element (impedanceDb.begin(), impedanceDb.end());
    for (size_t k = 1; k + 1 < impedanceDb.size(); ++k)
        if (impedanceDb[k] > impedanceDb[k - 1] && impedanceDb[k] >= impedanceDb[k + 1] && impedanceDb[k] > top - 30.0f)
            resonanceHz.push_back (impedanceHz[k]);
}

float ModelPage::jetCentreAt (double fraction) const
{
    // A bit of jet at x = fraction W left the flue x / (c U) ago and reaches the edge (1 - fraction) of the travel
    // time later, having grown by e^(E (1 - fraction)) more on the way: so its deflection now is the edge's
    // deflection then, shrunk by that growth.
    const double growth = snap.exponent + std::log (std::max (0.01f, snap.jetGain * (1.0f + snap.slowBoost)));
    const double later = shownTime + (1.0 - fraction) * snap.travel;
    return at (snap.deflection, later) * (float) std::exp (-growth * (1.0 - fraction));
}

//==============================================================================
void ModelPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
    auto area = getLocalBounds().toFloat().reduced (10.0f);

    paintChain (g, area.removeFromTop (118.0f));
    area.removeFromTop (8.0f);

    auto bore = area.removeFromBottom (170.0f);
    area.removeFromBottom (8.0f);
    const float rowHeight = (area.getHeight() - 8.0f) / 2.0f;
    auto upper = area.removeFromTop (rowHeight);
    area.removeFromTop (8.0f);
    auto lower = area;

    const float split = upper.getWidth() * 0.46f;
    paintWindow (g, upper.removeFromLeft (split));
    upper.removeFromLeft (8.0f);
    paintEdgeSignals (g, upper);
    paintJetAndBore (g, lower.removeFromLeft (split));
    lower.removeFromLeft (8.0f);
    paintHarmonics (g, lower);
    paintBore (g, bore);
}

void ModelPage::paintChain (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float bernoulli = std::sqrt (2.0f * std::max (0.0f, snap.pressure) / (float) whistle::kAirDensity);
    const double f0 = playing ? snap.rate / period : 0.0;
    int reg = 0;
    if (playing && ! resonanceHz.empty())
    {
        double best = 1.0e9;
        for (size_t k = 0; k < resonanceHz.size(); ++k)
            if (std::abs (std::log (resonanceHz[k] / f0)) < best)
            {
                best = std::abs (std::log (resonanceHz[k] / f0));
                reg = (int) k + 1;
            }
    }
    const double travelMs = snap.travel / snap.rate * 1000.0;
    const double growth = std::exp (snap.exponent) * snap.jetGain;
    auto pa = [] (float v) { return juce::String (juce::roundToInt (v)) + " Pa"; };

    struct Block { juce::String heading, line1, line2, settings; };
    const std::vector<Block> blocks {
        { "Breath", "mouth " + pa (snap.pressure), "", "Sensors tab: breath curve" },
        { "Windway", "jet speed " + juce::String (snap.jetSpeed, 1) + " m/s", "(" + juce::String (bernoulli, 1) + " steady); flow " + juce::String (juce::roundToInt (100.0f * snap.flowShare)) + " %",
          "Windway height, length, air mass, friction" },
        snap.jetSpeed < 2.0f ? Block { "Jet across the window", "no jet", "", "Window length, Strouhal, wave speed, jet gain, slow-jet growth, stages" } :
        Block { "Jet across the window", "crosses in " + juce::String (travelMs, 2) + " ms" + (playing ? " (" + juce::String (snap.travel / period, 2) + " period)" : juce::String()),
          "prefers " + juce::String (juce::roundToInt (snap.bandCentre)) + " Hz, grows x" + juce::String (growth * (1.0 + snap.slowBoost), 1),
          "Window length, Strouhal, wave speed, jet gain, slow-jet growth, stages" },
        { "Edge", playing ? "swings " + juce::String (swingMin, 1) + " .. " + juce::String (swingMax, 1) + " half-widths" : juce::String ("-"),
          playing ? "switched fully " + juce::String (juce::roundToInt (100.0f * switched)) + " % of the time" : juce::String(),
          "Edge offset (" + juce::String (snap.edge * 1000.0f, 2) + " mm), tilt" },
        { "Sound source at window", playing ? "jet drive " + pa (driveRms) + " rms" : juce::String ("-"), playing ? "vortex loss " + pa (vortexRms) + " rms" : juce::String(),
          "Edge vortex loss, breath noise, attack chiff" },
        { "Bore", playing ? juce::String (f0, 1) + " Hz " + noteName (f0, geometry.referenceA) : juce::String ("not sounding"),
          reg > 0 ? "register " + juce::String (reg) + " (resonance " + juce::String (reg) + ")" : juce::String(),
          "Instrument tab: bore, holes, wall; finger gap damping" },
        { "Sound out", juce::String (levelDb, 1) + " dB re 100 dB SPL", "window + holes and foot", "Output level; room: Monitor tab" },
    };

    const float gap = 18.0f;
    const float width = (area.getWidth() - gap * (float) (blocks.size() - 1)) / (float) blocks.size();
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        auto r = area.withX (area.getX() + (float) i * (width + gap)).withWidth (width);
        g.setColour (panel);
        g.fillRoundedRectangle (r, 6.0f);
        if (i + 1 < blocks.size())
        {
            const float y = r.getCentreY();
            juce::Path arrow;
            arrow.addArrow ({ r.getRight() + 2.0f, y, r.getRight() + gap - 2.0f, y }, 2.0f, 8.0f, 7.0f);
            g.setColour (dim);
            g.fillPath (arrow);
        }
        r = r.reduced (8.0f, 6.0f);
        g.setColour (i == 2 || i == 3 ? accent : i == 5 ? warm : text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawFittedText (blocks[i].heading, r.removeFromTop (17.0f).toNearestInt(), juce::Justification::left, 1);
        g.setColour (text);
        g.setFont (12.0f);
        g.drawFittedText (blocks[i].line1, r.removeFromTop (16.0f).toNearestInt(), juce::Justification::left, 1, 0.8f);
        g.drawFittedText (blocks[i].line2, r.removeFromTop (16.0f).toNearestInt(), juce::Justification::left, 1, 0.8f);
        g.setColour (dim);
        g.setFont (11.0f);
        g.drawFittedText (blocks[i].settings, r.toNearestInt(), juce::Justification::bottomLeft, 3, 0.8f);
    }
}

void ModelPage::paintWindow (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    area = area.reduced (10.0f);
    title (g, area.removeFromTop (46.0f), "The jet crossing the window (slow motion)",
           "The sound's flow in and out of the window (orange) pushes the jet as it leaves the flue; the push grows on the way "
           "and arrives at the edge a moment later, where the jet goes into the pipe or out of it.");

    const float h = (float) geometry.windwayHeight, halfWidth = std::max (1.0e-5f, snap.halfWidth), W = (float) geometry.windowLength;
    if (h <= 0.0f || W <= 0.0f)
        return;

    // Horizontal: flue (x = 0) to edge (W), with some windway before and the labium after. Vertical: magnified.
    auto plot = area.reduced (4.0f, 10.0f);
    const float x0 = plot.getX() + plot.getWidth() * 0.18f, x1 = plot.getX() + plot.getWidth() * 0.78f;
    float reach = 3.0f * halfWidth;
    if (playing)
        reach = std::max (reach, 1.15f * std::max (std::abs (swingMin), std::abs (swingMax)) * halfWidth + std::abs (snap.edge));
    const float yScale = plot.getHeight() * 0.5f / reach;  // px per metre, positive = into the pipe = down
    const float yc = plot.getCentreY();
    auto px = [&] (float metres) { return x0 + (x1 - x0) * metres / W; };
    auto py = [&] (float metres) { return yc + metres * yScale; };

    g.setColour (dim);
    g.setFont (11.0f);
    g.drawText ("out of the pipe", plot.withHeight (14.0f).withX (x1 + 8.0f).withWidth (plot.getRight() - x1 - 8.0f), juce::Justification::right);
    g.drawText ("into the pipe", plot.withTrimmedTop (plot.getHeight() - 14.0f).withX (x1 + 8.0f).withWidth (plot.getRight() - x1 - 8.0f), juce::Justification::right);

    // Windway walls
    g.setColour (metal);
    const float wallY = h * 0.5f * yScale;
    g.fillRect (juce::Rectangle<float> (plot.getX(), yc - wallY - 8.0f, x0 - plot.getX(), 8.0f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), yc + wallY, x0 - plot.getX(), 8.0f));
    g.setColour (dim);
    g.drawText ("windway (h = " + juce::String (h * 1000.0f, 2) + " mm)", juce::Rectangle<float> (plot.getX(), yc + wallY + 10.0f, x0 - plot.getX() + 30.0f, 14.0f),
                juce::Justification::left);

    // Edge (labium): a wedge whose tip is at the edge offset
    const float edgeY = py (snap.edge);
    juce::Path wedge;
    wedge.startNewSubPath (px (W), edgeY);
    wedge.lineTo (plot.getRight() - 4.0f, edgeY - plot.getHeight() * 0.18f);
    wedge.lineTo (plot.getRight() - 4.0f, edgeY + plot.getHeight() * 0.06f);
    wedge.closeSubPath();
    g.setColour (metal);
    g.fillPath (wedge);
    g.setColour (dim);
    g.drawText ("edge " + juce::String (snap.edge * 1000.0f, 2) + " mm", juce::Rectangle<float> (px (W) + 6.0f, edgeY + plot.getHeight() * 0.07f, 110.0f, 14.0f),
                juce::Justification::left);
    g.drawText ("W = " + juce::String (W * 1000.0f, 1) + " mm (vertical x" + juce::String (juce::roundToInt (yScale / ((x1 - x0) / W))) + ")",
                juce::Rectangle<float> (x0, plot.getBottom() - 14.0f, x1 - x0, 14.0f), juce::Justification::centred);

    if (! playing)
    {
        g.setColour (text);
        g.drawText (snap.jetSpeed > 1.0f ? "not sounding yet" : "blow to start", plot, juce::Justification::centred);
        // A still jet
        g.setColour (accent.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x0, yc - halfWidth * yScale, x1 - x0, 2.0f * halfWidth * yScale));
        return;
    }

    // The jet: centre line and its half-width, grown from the flue to the edge
    constexpr int steps = 48;
    juce::Path band, centre;
    std::array<float, steps + 1> ys {};
    for (int i = 0; i <= steps; ++i)
        ys[(size_t) i] = jetCentreAt ((double) i / steps);
    band.startNewSubPath (px (0.0f), py (ys[0] - halfWidth));
    for (int i = 1; i <= steps; ++i)
        band.lineTo (px (W * (float) i / steps), py (ys[(size_t) i] - halfWidth));
    for (int i = steps; i >= 0; --i)
        band.lineTo (px (W * (float) i / steps), py (ys[(size_t) i] + halfWidth));
    band.closeSubPath();
    for (int i = 0; i <= steps; ++i)
    {
        const float x = px (W * (float) i / steps), y = py (ys[(size_t) i]);
        if (i == 0) centre.startNewSubPath (x, y); else centre.lineTo (x, y);
    }
    g.setColour (accent.withAlpha (0.35f));
    g.fillPath (band);
    g.setColour (accent);
    g.strokePath (centre, juce::PathStrokeType (2.0f));

    // Where it goes at the edge: the share into the pipe and out
    const float into = 0.5f * (1.0f + std::tanh ((ys[steps] - snap.edge) / halfWidth));
    auto share = [&] (float amount, float direction)
    {
        juce::Path arrow;
        const float length = 14.0f + 30.0f * amount;
        arrow.addArrow ({ px (W) + 2.0f, edgeY + direction * 4.0f, px (W) + 2.0f + length * 0.7f, edgeY + direction * (4.0f + length * 0.7f) }, 1.0f + 5.0f * amount, 10.0f, 8.0f);
        g.setColour (accent.withAlpha (0.3f + 0.7f * amount));
        g.fillPath (arrow);
    };
    share (into, 1.0f);
    share (1.0f - into, -1.0f);
    g.setColour (text);
    g.drawText (juce::String (juce::roundToInt (100.0f * into)) + " % in", juce::Rectangle<float> (px (W) + 30.0f, edgeY + 22.0f, 70.0f, 14.0f), juce::Justification::left);

    // The sound's flow through the window at this moment (what disturbs the jet at the flue)
    float flowMax = 1.0e-12f;
    for (auto v : snap.windowFlow) flowMax = std::max (flowMax, std::abs (v));
    const float flow = at (snap.windowFlow, shownTime) / flowMax;
    juce::Path push;
    const float pushX = px (W * 0.08f);
    push.addArrow ({ pushX, yc, pushX, yc + flow * plot.getHeight() * 0.4f }, 3.0f, 10.0f, 8.0f);
    g.setColour (warm);
    g.fillPath (push);
    g.drawText ("sound", juce::Rectangle<float> (pushX - 50.0f, plot.getY(), 46.0f, 14.0f), juce::Justification::right);
}

void ModelPage::paintEdgeSignals (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    area = area.reduced (10.0f);
    title (g, area.removeFromTop (46.0f), "At the edge, over two periods",
           "Beyond one half-width from the edge (shaded) the jet goes all one way: the further it swings past, the squarer "
           "its flow and the richer the tone. An offset edge makes the two halves unequal: even harmonics.");
    if (! playing)
        return;

    const float halfWidth = std::max (1.0e-5f, snap.halfWidth);
    const double from = cycleStart - period, to = cycleStart + period;
    const float maxJetFlow = (float) geometry.windowWidth * halfWidth * std::max (snap.jetSpeed, 0.5f) * snap.flowShare;
    float flowMax = 1.0e-12f;
    for (auto i = (size_t) std::max (0.0, from); i < (size_t) to && i < snap.windowFlow.size(); ++i)
        flowMax = std::max (flowMax, std::abs (snap.windowFlow[i]));
    const float reach = std::max ({ 2.0f, std::abs (swingMin), std::abs (swingMax) }) * 1.1f;

    struct Trace { juce::String name; std::function<float (double)> value; float range; juce::Colour colour; bool band; };
    const std::vector<Trace> traces {
        { "jet centre line minus edge, in half-widths", [&] (double i) { return (at (snap.deflection, i) - snap.edge) / halfWidth; }, reach, accent, true },
        { "jet flow into the pipe (of its most)", [&] (double i) { return at (snap.jetFlow, i) / maxJetFlow; }, 1.1f, accent.brighter (0.4f), false },
        { "sound's flow through the window", [&] (double i) { return at (snap.windowFlow, i) / flowMax; }, 1.1f, warm, false },
    };

    const float rowHeight = area.getHeight() / (float) traces.size();
    for (size_t t = 0; t < traces.size(); ++t)
    {
        auto r = area.withY (area.getY() + (float) t * rowHeight).withHeight (rowHeight - 6.0f);
        g.setColour (dim);
        g.setFont (11.0f);
        g.drawText (traces[t].name, r.removeFromTop (13.0f), juce::Justification::left);
        const auto& trace = traces[t];
        auto y = [&] (float v) { return r.getCentreY() - v / trace.range * r.getHeight() * 0.5f; };
        g.setColour (grid);
        g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());
        if (trace.band)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (r.getX(), y (1.0f), r.getWidth(), y (-1.0f) - y (1.0f)));
        }
        juce::Path p;
        constexpr int steps = 300;
        for (int s = 0; s <= steps; ++s)
        {
            const double i = from + (to - from) * s / steps;
            const float x = r.getX() + r.getWidth() * (float) s / steps;
            const float v = juce::jlimit (-trace.range, trace.range, trace.value (i));
            if (s == 0) p.startNewSubPath (x, y (v)); else p.lineTo (x, y (v));
        }
        g.setColour (trace.colour);
        g.strokePath (p, juce::PathStrokeType (1.6f));

        // The moment shown in slow motion
        const float cursor = r.getX() + r.getWidth() * (float) ((shownTime - from) / (to - from));
        g.setColour (text.withAlpha (0.35f));
        g.drawVerticalLine ((int) cursor, r.getY(), r.getBottom());
    }
}

void ModelPage::paintJetAndBore (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    area = area.reduced (10.0f);
    title (g, area.removeFromTop (46.0f), "Which resonance sounds",
           "Orange: the bore's resonances for this fingering (how easily air moves in and out at the window). Blue: how strongly the jet amplifies "
           "each frequency at this breath. Blowing harder moves the blue curve up, towards the next register.");

    auto plot = area.withTrimmedBottom (16.0f);
    g.setColour (grid);
    for (double hz : { 200.0, 500.0, 1000.0, 2000.0, 5000.0 })
    {
        const float x = xForHz (hz, plot);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (dim);
        g.setFont (10.0f);
        g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + " kHz" : juce::String ((int) hz), juce::Rectangle<float> (x - 25.0f, plot.getBottom() + 2.0f, 50.0f, 12.0f),
                    juce::Justification::centred);
        g.setColour (grid);
    }

    // Bore impedance, top 40 dB
    if (! impedanceDb.empty())
    {
        const float top = *std::max_element (impedanceDb.begin(), impedanceDb.end());
        juce::Path p;
        for (size_t k = 0; k < impedanceDb.size(); ++k)
        {
            const float x = xForHz (impedanceHz[k], plot);
            const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (impedanceDb[k] - top + 40.0f) / 40.0f);
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (warm);
        g.strokePath (p, juce::PathStrokeType (1.5f));
        g.setFont (10.0f);
        float lastLabel = -1.0e9f;
        for (size_t k = 0; k < resonanceHz.size() && k < 5; ++k)
        {
            if (xForHz (resonanceHz[k], plot) - lastLabel < 40.0f)
                continue;
            lastLabel = xForHz (resonanceHz[k], plot);
            g.drawText (juce::String (juce::roundToInt (resonanceHz[k])), juce::Rectangle<float> (xForHz (resonanceHz[k], plot) - 20.0f, plot.getY(), 40.0f, 12.0f),
                        juce::Justification::centred);
        }
    }

    // The jet's preference: its band-pass stages around the band centre, times the slow jet's extra growth for
    // long waves; relative to its best, over 40 dB.
    if (snap.bandCentre > 0.0f && snap.jetSpeed >= 2.0f)
    {
        juce::Path p;
        const double fc = snap.bandCentre, q = std::max (0.1f, snap.jetQ);
        std::vector<float> gains;
        for (int k = 0; k <= 200; ++k)
        {
            const double hz = kMinHz * std::pow (kMaxHz / kMinHz, k / 200.0);
            const double detune = hz / fc - fc / hz;
            const double stage = 1.0 / std::sqrt (1.0 + q * q * detune * detune);
            const std::complex<double> lowpass = 1.0 / std::complex<double> (1.0, hz / fc);
            const double boost = std::abs (1.0 + (double) snap.slowBoost * lowpass);
            gains.push_back ((float) (20.0 * std::log10 (std::pow (stage, snap.stages) * boost + 1.0e-9)));
        }
        const float top = *std::max_element (gains.begin(), gains.end());
        for (size_t k = 0; k < gains.size(); ++k)
        {
            const float x = xForHz (kMinHz * std::pow (kMaxHz / kMinHz, (double) k / 200.0), plot);
            const float y = plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (gains[k] - top + 40.0f) / 40.0f);
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
    }

    // What plays: the note and its harmonics
    if (playing)
    {
        const double f0 = snap.rate / period;
        for (int k = 1; k <= 10; ++k)
        {
            const double hz = f0 * k;
            if (hz > kMaxHz)
                break;
            const float x = xForHz (hz, plot);
            g.setColour (k == 1 ? green : green.withAlpha (0.45f));
            g.drawLine (x, plot.getBottom() - (k == 1 ? 26.0f : 12.0f), x, plot.getBottom(), k == 1 ? 3.0f : 1.5f);
        }
        g.setColour (green);
        g.setFont (11.0f);
        g.drawText ("playing", juce::Rectangle<float> (xForHz (f0, plot) + 4.0f, plot.getBottom() - 26.0f, 60.0f, 12.0f), juce::Justification::left);
    }
}

void ModelPage::paintHarmonics (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    area = area.reduced (10.0f);
    title (g, area.removeFromTop (46.0f), "Harmonics of the sound",
           "In dB below the fundamental. Real high-D whistles (host/tools/Real-examples): bottom D about -2 dB at H2 and H3; "
           "first-octave average -12, -15, -26, -23; the second octave is nearly pure (-31).");
    if (! playing)
        return;

    auto plot = area.withTrimmedBottom (16.0f).withTrimmedTop (12.0f);
    constexpr float floorDb = -50.0f;
    g.setColour (grid);
    for (float db : { -10.0f, -20.0f, -30.0f, -40.0f })
    {
        const float y = plot.getY() + plot.getHeight() * db / floorDb;
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (dim);
        g.setFont (10.0f);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX(), y - 12.0f, 30.0f, 12.0f), juce::Justification::left);
        g.setColour (grid);
    }
    const float slot = (plot.getWidth() - 30.0f) / 10.0f;
    for (int k = 1; k <= 10; ++k)
    {
        const float db = juce::jlimit (floorDb, 0.0f, harmonicsDb[(size_t) k]);
        const float x = plot.getX() + 30.0f + (float) (k - 1) * slot;
        const float top = plot.getY() + plot.getHeight() * db / floorDb;
        g.setColour (k == 1 ? green : (k % 2 == 0 ? accent : warm));
        g.fillRect (juce::Rectangle<float> (x + slot * 0.2f, top, slot * 0.6f, plot.getBottom() - top));
        g.setColour (text);
        g.setFont (10.0f);
        g.drawText ("H" + juce::String (k), juce::Rectangle<float> (x, plot.getBottom() + 2.0f, slot, 12.0f), juce::Justification::centred);
        if (k > 1)
            g.drawText (juce::String (juce::roundToInt (harmonicsDb[(size_t) k])), juce::Rectangle<float> (x, top - 13.0f, slot, 12.0f), juce::Justification::centred);
    }
    g.setColour (dim);
    g.drawText ("blue: even (from the edge offset), orange: odd (from the jet's switching)", plot.withHeight (12.0f).translated (30.0f, -12.0f),
                juce::Justification::left);
}

void ModelPage::paintBore (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);
    area = area.reduced (10.0f);
    title (g, area.removeFromTop (34.0f), "The air column (slow motion)",
           "Sound pressure along the bore: the shaded envelope is its size at each point (small at open holes and the foot), the line "
           "its swing at the moment shown.");

    const float length = (float) geometry.boreLength;
    if (length <= 0.0f)
        return;
    auto r = area.reduced (20.0f, 6.0f);
    const float x0 = r.getX() + 30.0f, x1 = r.getRight() - 10.0f;
    auto px = [&] (float metres) { return x0 + (x1 - x0) * metres / length; };
    const float yc = r.getCentreY() + 6.0f, tube = r.getHeight() * 0.32f;

    // Tube and holes
    g.setColour (metal.withAlpha (0.25f));
    g.fillRect (juce::Rectangle<float> (x0, yc - tube, x1 - x0, 2.0f * tube));
    g.setColour (metal);
    g.drawRect (juce::Rectangle<float> (x0, yc - tube, x1 - x0, 2.0f * tube), 1.0f);
    g.setColour (dim);
    g.setFont (10.0f);
    g.drawText ("window", juce::Rectangle<float> (x0 - 40.0f, yc + tube + 2.0f, 80.0f, 12.0f), juce::Justification::centred);
    g.drawText ("foot", juce::Rectangle<float> (x1 - 40.0f, yc + tube + 2.0f, 80.0f, 12.0f), juce::Justification::centred);
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        const auto& hole = geometry.holes[(size_t) i];
        if (! hole.enabled)
            continue;
        const float open = audio.shownOpenness[(size_t) i].load();
        const float x = px ((float) hole.position), d = 12.0f;
        const auto circle = juce::Rectangle<float> (x - d / 2.0f, yc - tube - d - 2.0f, d, d);
        g.setColour (background);
        g.fillEllipse (circle);
        g.setColour (warm.withAlpha (1.0f - open));
        g.fillEllipse (circle.reduced (1.5f));
        g.setColour (metal);
        g.drawEllipse (circle, 1.0f);
        g.setColour (dim);
        g.drawText (whistle::kHoleNames[(size_t) i], circle.withY (circle.getY() - 13.0f).expanded (12.0f, 0.0f), juce::Justification::centred);
    }

    if (! playing || snap.junctionX.size() < 2)
        return;
    const float most = std::max (1.0e-6f, *std::max_element (snap.junctionRms.begin(), snap.junctionRms.end()));
    juce::Path envelope, line;
    const float swing = (float) std::cos (2.0 * juce::MathConstants<double>::pi * slowMotion);
    for (size_t j = 0; j < snap.junctionX.size(); ++j)
    {
        const float x = px (snap.junctionX[j]);
        const float a = snap.junctionRms[j] / most * tube * 0.9f;
        if (j == 0) envelope.startNewSubPath (x, yc - a); else envelope.lineTo (x, yc - a);
        const float y = yc - a * snap.junctionSign[j] * swing;
        if (j == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    for (size_t j = snap.junctionX.size(); j-- > 0;)
        envelope.lineTo (px (snap.junctionX[j]), yc + snap.junctionRms[j] / most * tube * 0.9f);
    envelope.closeSubPath();
    g.setColour (warm.withAlpha (0.2f));
    g.fillPath (envelope);
    g.setColour (warm);
    g.strokePath (line, juce::PathStrokeType (2.0f));
    g.setColour (dim);
    g.drawText ("peak " + juce::String (juce::roundToInt (most * std::sqrt (2.0f))) + " Pa", juce::Rectangle<float> (r.getX(), yc - 6.0f, 60.0f, 12.0f).translated (-20.0f, 0.0f),
                juce::Justification::left);
}
