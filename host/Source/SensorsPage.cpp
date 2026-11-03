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
            const auto p = toScreen (x, curve (x));
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
    for (auto* l : { &breathTitle, &holeTitle })
    {
        l->setColour (juce::Label::textColourId, accent);
        l->setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (l);
    }
    for (auto* l : { &breathInfo, &holeInfo })
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
    addRow (holeRows, "Curve (64 = linear)", 0.0, 127.0, 1.0, "", audio.holeCurve)
        .setTooltip ("Below 64: the hole closes quickly as the finger arrives. Above 64: it stays mostly open until the finger is nearly down, "
                     "leaving more room for half-holing and slides.");

    holeCurve.curve = [this] (float x) { return audio.holeResponse().closedness (x); };
    holeCurve.points = [this]
    {
        std::vector<CurveView::Point> p;
        for (int i = 0; i < whistle::kNumHoles; ++i)
            p.push_back ({ audio.measuredCoverage[(size_t) i].load(), kHoleNames[(size_t) i] });
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
        resetRows (holeRows, { d.openBelow, d.closedAt, d.curve });
    };
    addAndMakeVisible (breathDefaults);
    addAndMakeVisible (holeDefaults);

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
                            + "Now: measured " + juce::String (juce::roundToInt (measured)) + " Pa -> model "
                            + juce::String (juce::roundToInt (b.map (measured))) + " Pa",
                        juce::dontSendNotification);
    holeInfo.setText ("Coverage comes from WARBL's hole calibration (0 = open, 1 = covered). "
                      "Dots show each hole now; playing from the screen bypasses this.",
                      juce::dontSendNotification);

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

    auto layout = [] (juce::Rectangle<int> column, juce::Label& title, juce::TextButton& defaults, std::vector<Row>& rows,
                      juce::Label& info, CurveView& view)
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
        column.removeFromTop (6);
        info.setBounds (column.removeFromTop (40));
        view.setBounds (column.removeFromTop (std::min (column.getHeight(), column.getWidth())));
    };

    layout (left, breathTitle, breathDefaults, breathRows, breathInfo, breathCurve);
    layout (right, holeTitle, holeDefaults, holeRows, holeInfo, holeCurve);
}
