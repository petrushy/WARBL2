#include "SensorsPage.h"

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour grid { 0xff3a3d42 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour live { 0xffe0a24f };

using whistle::kHoleNames;
} // namespace

//==============================================================================
CurveView::CurveView (juce::String x, juce::String y) : xLabel (std::move (x)), yLabel (std::move (y)) {}

void CurveView::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto area = getLocalBounds().reduced (10).toFloat();
    auto xAxis = area.removeFromBottom (18.0f);
    area.removeFromLeft (12.0f);
    const auto plot = area.reduced (4.0f);

    auto toScreen = [&] (float x, float y)
    {
        return juce::Point<float> (plot.getX() + x / xRange * plot.getWidth(), plot.getBottom() - std::clamp (y / yRange, 0.0f, 1.05f) * plot.getHeight());
    };

    g.setColour (grid);
    for (int i = 0; i <= 4; ++i)
    {
        g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * i / 4.0f), plot.getX(), plot.getRight());
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * i / 4.0f), plot.getY(), plot.getBottom());
    }

    g.setColour (text.withAlpha (0.7f));
    g.setFont (12.0f);
    g.drawText (xLabel, xAxis, juce::Justification::centred);
    g.drawText ("0", xAxis.withWidth (20.0f).withX (plot.getX() - 4.0f), juce::Justification::centredLeft);
    g.drawText (juce::String (xRange, xRange < 10.0f ? 2 : 0), xAxis.withLeft (plot.getRight() - 60.0f).withWidth (60.0f), juce::Justification::centredRight);
    g.saveState();
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, plot.getX() - 8.0f, plot.getCentreY()));
    g.drawText (yLabel, juce::Rectangle<float> (plot.getHeight(), 14.0f).withCentre ({ plot.getX() - 8.0f, plot.getCentreY() }),
                juce::Justification::centred);
    g.restoreState();

    if (curve)
    {
        juce::Path path;
        for (int i = 0; i <= 200; ++i)
        {
            const float x = xRange * i / 200.0f;
            const auto p = toScreen (x, curve (x));
            if (i == 0)
                path.startNewSubPath (p);
            else
                path.lineTo (p);
        }
        g.setColour (accent);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

    if (points && curve)
    {
        for (const auto& point : points())
        {
            const float x = std::clamp (point.x, 0.0f, xRange);
            const auto p = toScreen (x, std::isnan (point.y) ? curve (x) : point.y);
            g.setColour (live);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
            if (point.label.isNotEmpty())
            {
                g.setColour (text);
                g.drawText (point.label, juce::Rectangle<float> (30.0f, 14.0f).withCentre (p.translated (0.0f, -12.0f)), juce::Justification::centred);
            }
        }
    }
}

//==============================================================================
SensorsPage::SensorsPage (AudioEngine& a) : audio (a)
{
    for (auto* l : { &breathTitle, &holeTitle, &tiltTitle, &thumbTitle })
    {
        l->setColour (juce::Label::textColourId, accent);
        l->setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }
    for (auto* l : { &breathInfo, &holeInfo, &tiltInfo, &thumbInfo })
    {
        l->setColour (juce::Label::textColourId, text);
        l->setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (l);
    }

    // Breath
    addRow (breathRows, "Input minimum (threshold)", 0.0, 400.0, 1.0, "Pa", audio.breathInputMin)
        .setTooltip ("Measured pressure below this counts as no breath. Raise it if the sensor's noise makes sound when you aren't blowing.");
    addRow (breathRows, "Input maximum", 200.0, 6000.0, 10.0, "Pa", audio.breathInputMax)
        .setTooltip ("The measured pressure that gives the full output. Lower = more sensitive.");
    addRow (breathRows, "Curve (64 = linear)", 0.0, 127.0, 1.0, "", audio.breathCurve)
        .setTooltip ("Below 64: logarithmic, more response to soft blowing. Above 64: exponential, more response to hard blowing.");
    addRow (breathRows, "Output maximum", 200.0, 10000.0, 10.0, "Pa", audio.breathOutputMax)
        .setTooltip ("Breath pressure the model gets at the input maximum. A D whistle's low register starts around 400 Pa, overblows to the octave "
                     "above about 1000 Pa, to the twelfth around 4000 Pa and the double octave around 7000 Pa. Raise this (above WARBL's ~6 kPa "
                     "sensor range if needed) to reach the upper registers.");

    breathCurve.curve = [this] (float x) { return audio.breathResponse().map (x); };
    breathCurve.points = [this] { return std::vector<CurveView::Point> { { audio.measuredPressure.load(), {} } }; };
    addAndMakeVisible (breathCurve);

    // Holes
    addRow (holeRows, "Open below", 0.0, 0.6, 0.01, "", audio.holeOpenBelow)
        .setTooltip ("Coverage below this leaves the hole fully open, so a finger hovering above it has no effect.");
    addRow (holeRows, "Closed at", 0.3, 1.3, 0.01, "", audio.holeClosedAt)
        .setTooltip ("Coverage at which the hole is fully closed. Lower = closes sooner (more sensitive).");
    addRow (holeRows, "Curve (64 = straight)", 0.0, 127.0, 1.0, "", audio.holeCurve)
        .setTooltip ("Power curve: below 64 the hole closes quickly as the finger arrives, above 64 it stays mostly open until the finger "
                     "is nearly down. S-curve: how steep the S is (above 64 flat at both ends, below 64 an inverted S).");
    addRow (holeRows, "Centre (S-curve)", 0.1, 0.9, 0.01, "", audio.holeCentre)
        .setTooltip ("Where between 'open below' and 'closed at' the S-curve is steepest: lower moves it towards the open end.");
    addRow (holeRows, "Finger range (physical)", 0.5, 8.0, 0.1, "mm", audio.holeFingerRange)
        .setTooltip ("The infrared sensor's distance scale: how far above the hole a finger still shows on it. Larger = the finger "
                     "reads as higher, so holes open sooner as the finger lifts.");

    holeShape.addItem ("Power curve", 1);
    holeShape.addItem ("S-curve", 2);
    holeShape.addItem ("Finger height (physical)", 3);
    holeShape.setSelectedId (audio.holeFingerHeight.load() ? 3 : audio.holeSCurve.load() ? 2 : 1, juce::dontSendNotification);
    holeShape.setTooltip ("S-curve: little change while the finger is far away and in the last bit before it's down, steep in between. "
                          "Finger height: each hole opens as the air escaping under a hovering finger would let it, from the "
                          "finger's height (estimated from the sensor) and the hole's size; a hovering finger still flattens the note.");
    holeShape.onChange = [this]
    {
        audio.holeSCurve = holeShape.getSelectedId() == 2;
        audio.holeFingerHeight = holeShape.getSelectedId() == 3;
    };
    addAndMakeVisible (holeShape);

    // The curve drawn: the first hole in use (with the physical shape, each hole has its own; dots show each).
    auto shownHole = [this]
    {
        for (int i = 1; i < whistle::kNumHoles; ++i)
            if (audio.holeRadius[(size_t) i].load() > 0.0f)
                return i;
        return 0;
    };
    holeCurve.curve = [this, shownHole] (float x) { return audio.holeResponse().closedness (x, audio.fingerHole (shownHole())); };
    holeCurve.points = [this]
    {
        std::vector<CurveView::Point> p;
        for (int i = 0; i < whistle::kNumHoles; ++i)
        {
            const float x = audio.measuredCoverage[(size_t) i].load();
            p.push_back ({ x, kHoleNames[(size_t) i], std::clamp (audio.holeClosedness (i, x), 0.0f, 1.0f) });
        }
        p.push_back ({ audio.measuredBellCoverage.load(), "Bell" });
        return p;
    };
    holeCurve.setRanges (1.3f, 1.0f);
    addAndMakeVisible (holeCurve);

    auto resetRows = [] (std::vector<Row>& rows, std::initializer_list<float> values)
    {
        size_t i = 0;
        for (float v : values)
            rows[i++].slider->setValue (v, juce::sendNotificationSync);
    };
    breathDefaults.onClick = [this, resetRows]
    {
        const response::Breath d;
        resetRows (breathRows, { d.inputMin, d.inputMax, d.curve, d.outputMax });
    };
    holeDefaults.onClick = [this, resetRows]
    {
        const response::Hole d;
        resetRows (holeRows, { d.openBelow, d.closedAt, d.curve, d.centre, d.fingerRange });
        holeShape.setSelectedId (2, juce::sendNotificationSync);  // The default shape is the S-curve
    };
    addAndMakeVisible (breathDefaults);
    addAndMakeVisible (holeDefaults);

    // Thumb
    thumbTitle.setTooltip ("A recorder's thumb pinch uncovers a strip of the hole at the rim while the thumb stays over the "
                           "sensor, so WARBL still reads it as mostly covered. With its own response the thumb's open area follows "
                           "the coverage in a straight line (curve 64) instead of the fingers' S-curve, which hears a pinch as "
                           "nearly closed. The chart is tuned for a pinch 25 % open.");
    thumbToggle.setColour (juce::ToggleButton::textColourId, text);
    thumbToggle.setToggleState (audio.thumbOwn.load(), juce::dontSendNotification);
    thumbToggle.onClick = [this] { audio.thumbOwn = thumbToggle.getToggleState(); };
    addAndMakeVisible (thumbToggle);
    addRow (thumbRows, "Thumb open below", 0.0, 0.6, 0.01, "", audio.thumbOpenBelow)
        .setTooltip ("Coverage below which the thumb hole is fully open.");
    addRow (thumbRows, "Thumb closed at", 0.3, 1.3, 0.01, "", audio.thumbClosedAt)
        .setTooltip ("Coverage at which the thumb hole is fully closed. Lower: a pinch reads as more closed.");
    addRow (thumbRows, "Thumb curve (64 = straight)", 0.0, 127.0, 1.0, "", audio.thumbCurve)
        .setTooltip ("Above 64 the hole stays more open as the thumb covers it (a pinch reads as more open), below 64 it closes sooner.");

    // Tilt
    tiltTitle.setTooltip ("WARBL's elevation shifts where the jet meets the edge, as if the whistle's voicing were slightly "
                          "flexible: mostly the balance of even and odd harmonics (fuller or hollower), a little pitch and "
                          "register. A real whistle is rigid; this is an extra control. Hold WARBL as you play and press "
                          "\"Set zero now\", so your normal angle leaves the voicing as it is.");
    for (auto* t : { &tiltToggle, &tiltInvertToggle })
    {
        t->setColour (juce::ToggleButton::textColourId, text);
        addAndMakeVisible (t);
    }
    tiltToggle.setToggleState (audio.tiltEnabled.load(), juce::dontSendNotification);
    tiltToggle.onClick = [this] { audio.tiltEnabled = tiltToggle.getToggleState(); };
    tiltInvertToggle.setToggleState (audio.tiltInvert.load(), juce::dontSendNotification);
    tiltInvertToggle.setTooltip ("Swap which way of tilting moves the jet into the pipe.");
    tiltInvertToggle.onClick = [this] { audio.tiltInvert = tiltInvertToggle.getToggleState(); };
    tiltZeroButton.setTooltip ("Takes WARBL's angle right now as the neutral one (hold it as you normally play).");
    tiltZeroButton.onClick = [this] { audio.tiltZero = audio.measuredTilt.load(); };
    addAndMakeVisible (tiltZeroButton);
    addRow (tiltRows, "Dead zone", 0.0, 15.0, 0.5, juce::String::fromUTF8 ("°"), audio.tiltDeadZone)
        .setTooltip ("Degrees either side of the zero angle that do nothing, so small movements don't change the tone.");
    addRow (tiltRows, "Range (full shift at)", 5.0, 60.0, 1.0, juce::String::fromUTF8 ("°"), audio.tiltRange)
        .setTooltip ("How far from the zero angle you tilt for the full shift.");
    addRow (tiltRows, "Maximum edge shift", 0.0, 0.4, 0.01, "mm", audio.tiltMaxShift)
        .setTooltip ("How far the jet's aim moves at full tilt. Into the pipe the tone gets fuller (stronger even harmonics), "
                     "out of it hollower; beyond about 0.1-0.15 mm into the pipe some notes stop speaking.");

    startTimerHz (30);
}

juce::Slider& SensorsPage::addRow (std::vector<Row>& rows, juce::String name, double min, double max, double step, juce::String unit,
                                   std::atomic<float>& target)
{
    Row row;
    row.label = std::make_unique<juce::Label> (juce::String(), name);
    row.label->setColour (juce::Label::textColourId, text);
    row.slider = std::make_unique<juce::Slider>();
    row.slider->setRange (min, max, step);
    row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
    if (unit.isNotEmpty())
        row.slider->setTextValueSuffix (" " + unit);
    row.slider->setValue (target.load(), juce::dontSendNotification);
    auto* slider = row.slider.get();
    slider->onValueChange = [slider, &target] { target = (float) slider->getValue(); };
    addAndMakeVisible (*row.label);
    addAndMakeVisible (*row.slider);
    rows.push_back (std::move (row));
    return *slider;
}

void SensorsPage::timerCallback()
{
    const auto b = audio.breathResponse();
    breathCurve.setRanges (std::max (b.inputMax * 1.2f, 500.0f), std::max (b.outputMax * 1.1f, 500.0f));

    const float measured = audio.measuredPressure.load();
    breathInfo.setText ("Sensitivity " + juce::String (b.outputMax / std::max (1.0f, b.inputMax - b.inputMin), 2) + "x\n"
                            + "Now: measured " + juce::String (juce::roundToInt (measured)) + " Pa -> "
                            + juce::String (juce::roundToInt (b.map (measured))) + " Pa"
                            + (std::abs (audio.whistleModel.breathScale.load() - 1.0f) > 0.005f
                                   ? " x " + juce::String (audio.whistleModel.breathScale.load(), 2) + " (instrument's breath scale) = "
                                         + juce::String (juce::roundToInt (b.map (measured) * audio.whistleModel.breathScale.load())) + " Pa to the model"
                                   : juce::String (" to the model")),
                        juce::dontSendNotification);
    const bool physical = audio.holeFingerHeight.load();
    holeInfo.setText (physical ? "Coverage comes from WARBL's hole calibration. Each hole opens by the finger's height and the hole's "
                                 "size; the line is the first hole in use, dots show each hole now."
                               : "Coverage comes from WARBL's hole calibration (0 = open, 1 = covered). "
                                 "Dots show each hole now; playing from the screen bypasses this.",
                      juce::dontSendNotification);
    const auto tilt = audio.tiltResponse();
    const float tiltNow = audio.measuredTilt.load();
    tiltInfo.setText ("Now " + juce::String (tiltNow, 1) + juce::String::fromUTF8 ("°") + " (zero "
                          + juce::String (tilt.zero, 1) + juce::String::fromUTF8 ("°") + "): edge offset "
                          + (tilt.enabled ? juce::String (tilt.edgeShiftMm (tiltNow) >= 0.0f ? "+" : "") + juce::String (tilt.edgeShiftMm (tiltNow), 3) + " mm"
                                          : juce::String ("unchanged (off)")),
                      juce::dontSendNotification);

    {
        const float coverage = audio.measuredCoverage[0].load();
        const float open = 1.0f - std::clamp (audio.holeClosedness (0, coverage), 0.0f, 1.0f);
        thumbInfo.setText ("Thumb now: coverage " + juce::String (coverage, 2) + " -> " + juce::String (juce::roundToInt (100.0f * open))
                               + " % open. The recorder's chart pinches to 25 %; each 15 % more makes the high notes ~23 c sharper.",
                           juce::dontSendNotification);
        for (auto& row : thumbRows)
            row.slider->setEnabled (audio.thumbOwn.load());
    }

    holeRows[2].slider->setEnabled (! physical);  // Curve
    holeRows[3].slider->setEnabled (! physical);  // Centre
    holeRows[4].slider->setEnabled (physical);    // Finger range

    breathCurve.repaint();
    holeCurve.repaint();
}

void SensorsPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void SensorsPage::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto left = area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (8);
    auto right = area.withTrimmedLeft (8);

    // Tilt, at the bottom of the breath column.
    auto tiltArea = left.removeFromBottom (28 + 28 + 26 * (int) tiltRows.size() + 24);
    left.removeFromBottom (8);
    tiltTitle.setBounds (tiltArea.removeFromTop (28));
    auto toggles = tiltArea.removeFromTop (28);
    tiltToggle.setBounds (toggles.removeFromLeft (60));
    tiltInvertToggle.setBounds (toggles.removeFromLeft (80));
    tiltZeroButton.setBounds (toggles.removeFromLeft (130).reduced (2));
    for (auto& row : tiltRows)
    {
        auto line = tiltArea.removeFromTop (26);
        row.label->setBounds (line.removeFromLeft (190));
        row.slider->setBounds (line);
    }
    tiltInfo.setBounds (tiltArea.removeFromTop (24));

    auto layout = [] (juce::Rectangle<int> column, juce::Label& title, juce::TextButton& defaults, std::vector<Row>& rows,
                      juce::Label& info, CurveView& view, juce::Component* shape)
    {
        auto top = column.removeFromTop (28);
        defaults.setBounds (top.removeFromRight (90).reduced (2));
        title.setBounds (top);
        for (auto& row : rows)
        {
            auto line = column.removeFromTop (26);
            row.label->setBounds (line.removeFromLeft (190));
            row.slider->setBounds (line);
        }
        if (shape != nullptr)
            shape->setBounds (column.removeFromTop (28).removeFromLeft (220).reduced (0, 2));
        column.removeFromTop (6);
        info.setBounds (column.removeFromTop (40));
        view.setBounds (column.removeFromTop (std::min (column.getHeight(), column.getWidth())));
    };

    layout (left, breathTitle, breathDefaults, breathRows, breathInfo, breathCurve, nullptr);
    // Thumb, at the bottom of the holes column.
    auto thumbArea = right.removeFromBottom (28 + 26 * (int) thumbRows.size() + 40);
    right.removeFromBottom (8);
    auto thumbTop = thumbArea.removeFromTop (28);
    thumbToggle.setBounds (thumbTop.removeFromRight (130));
    thumbTitle.setBounds (thumbTop);
    for (auto& row : thumbRows)
    {
        auto line = thumbArea.removeFromTop (26);
        row.label->setBounds (line.removeFromLeft (190));
        row.slider->setBounds (line);
    }
    thumbInfo.setBounds (thumbArea);

    layout (right, holeTitle, holeDefaults, holeRows, holeInfo, holeCurve, &holeShape);
}
