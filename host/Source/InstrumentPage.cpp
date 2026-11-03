#include "InstrumentPage.h"

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour coveredColour { 0xffe0a24f };
const juce::Colour metal { 0xff9aa3ad };

const char* kHoleNames[whistle::kNumHoles] = { "L1", "L2", "L3", "R1", "R2", "R3" };

juce::String noteName (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::String (names[(midi % 12 + 12) % 12]) + juce::String (midi / 12 - 1);
}

juce::String signedCents (int cents) { return (cents >= 0 ? "+" : "") + juce::String (cents); }
} // namespace

//==============================================================================
WhistleView::WhistleView (AudioEngine& a) : audio (a) {}

float WhistleView::xFor (double metres) const
{
    // The window sits at the left; leave room for the mouthpiece.
    const auto area = getLocalBounds().toFloat().reduced (12.0f, 0.0f);
    const float head = 60.0f;
    const double scale = (area.getWidth() - head) / std::max (0.1, geometry.boreLength);
    return area.getX() + head + (float) (metres * scale);
}

juce::Rectangle<float> WhistleView::holeBounds (int hole) const
{
    const auto& h = geometry.holes[(size_t) hole];
    const float scale = (xFor (1.0) - xFor (0.0));
    const float d = std::max (8.0f, (float) h.diameter * scale);
    return juce::Rectangle<float> (d, d).withCentre ({ xFor (h.position), getHeight() * 0.42f });
}

void WhistleView::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    const float scale = xFor (1.0) - xFor (0.0);
    const float centre = getHeight() * 0.55f;
    const float topHalf = (float) geometry.boreDiameterTop * scale * 0.5f + 4.0f;
    const float footHalf = (float) geometry.boreDiameterFoot * scale * 0.5f + 4.0f;

    // Body
    juce::Path body;
    const float x0 = xFor (0.0), x1 = xFor (geometry.boreLength);
    body.startNewSubPath (x0 - 50.0f, centre - topHalf);
    body.lineTo (x1, centre - footHalf);
    body.lineTo (x1, centre + footHalf);
    body.lineTo (x0 - 50.0f, centre + topHalf);
    body.closeSubPath();
    g.setColour (metal.withAlpha (0.35f));
    g.fillPath (body);
    g.setColour (metal);
    g.strokePath (body, juce::PathStrokeType (1.5f));

    // Mouthpiece and window
    const float windowWidth = std::max (6.0f, (float) geometry.windowLength * scale);
    g.setColour (panel.darker (0.4f));
    g.fillRect (juce::Rectangle<float> (x0 - windowWidth, centre - topHalf - 1.0f, windowWidth, 6.0f));
    g.setColour (text.withAlpha (0.7f));
    g.setFont (11.0f);
    g.drawText ("window", juce::Rectangle<float> (x0 - 60.0f, centre - topHalf - 22.0f, 60.0f, 16.0f), juce::Justification::centred);

    // Holes, with live openness
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        const auto r = holeBounds (i);
        const float open = audio.shownOpenness[(size_t) i].load();
        g.setColour (panel.darker (0.6f));
        g.fillEllipse (r);
        g.setColour (coveredColour.withAlpha (1.0f - open));
        g.fillEllipse (r.reduced (1.0f));
        g.setColour (metal);
        g.drawEllipse (r, 1.2f);
        g.setColour (text);
        g.drawText (kHoleNames[i], r.withY (r.getBottom() + 26.0f).withHeight (14.0f).expanded (10.0f, 0.0f), juce::Justification::centred);
    }

    g.setColour (text.withAlpha (0.7f));
    g.drawText (juce::String (geometry.boreLength * 1000.0, 1) + " mm from window to foot",
                getLocalBounds().removeFromBottom (18).toFloat(), juce::Justification::centred);
}

void WhistleView::mouseDown (const juce::MouseEvent& e)
{
    if (! audio.manualPlay)
        return;
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        if (holeBounds (i).expanded (6.0f).contains (e.position))
        {
            auto& open = audio.manualOpenness[(size_t) i];
            if (e.mods.isPopupMenu())
                open = 0.5f;
            else
                open = open.load() > 0.5f ? 0.0f : 1.0f;
            repaint();
        }
    }
}

//==============================================================================
void TuningTable::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    auto area = getLocalBounds().reduced (10, 6);
    g.setFont (13.0f);

    auto header = area.removeFromTop (20);
    g.setColour (text.withAlpha (0.7f));
    const int columns[] = { 80, 120 };
    g.drawText ("Note", header.removeFromLeft (columns[0]), juce::Justification::centredLeft);
    g.drawText ("Fingering", header.removeFromLeft (columns[1]), juce::Justification::centredLeft);
    g.drawText ("Resonance vs. the note (> 10 c highlighted)", header, juce::Justification::centredLeft);

    for (const auto& e : entries)
    {
        auto line = area.removeFromTop (19);
        juce::String pattern;
        for (auto ch : e.pattern)
            pattern += juce::String (juce::CharPointer_UTF8 (ch == 'x' ? "\xe2\x97\x8f" : "\xe2\x97\x8b"));  // Filled / open circle

        g.setColour (text);
        g.drawText (e.note, line.removeFromLeft (columns[0]), juce::Justification::centredLeft);
        g.drawText (pattern, line.removeFromLeft (columns[1]), juce::Justification::centredLeft);
        const int cents = (int) std::lround (e.cents);
        g.setColour (std::abs (cents) > 10 ? coveredColour : text);
        g.drawText (signedCents (cents) + " c", line, juce::Justification::centredLeft);
    }
}

//==============================================================================
InstrumentPage::InstrumentPage (AudioEngine& a) : audio (a)
{
    geometry = whistle::presets().front();

    // Instrument choice
    for (size_t i = 0; i < whistle::presets().size(); ++i)
        presetBox.addItem (whistle::presets()[i].name, (int) i + 1);
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this] { loadPreset (presetBox.getSelectedId() - 1); };

    for (size_t i = 0; i < whistle::materials().size(); ++i)
        materialBox.addItem (whistle::materials()[i].name, (int) i + 1);
    materialBox.setSelectedId (1, juce::dontSendNotification);
    materialBox.onChange = [this]
    {
        const auto& m = whistle::materials()[(size_t) materialBox.getSelectedId() - 1];
        geometry.wallThickness = m.wallThickness;
        geometry.wallLoss = m.wallLoss;
        refreshSliders();
        geometryChanged();
    };

    static const char* keys[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        keyBox.addItem (keys[i], i + 1);
    keyBox.setSelectedId (3, juce::dontSendNotification);
    octaveBox.addItem ("high (tin whistle)", 1);
    octaveBox.addItem ("low (low whistle)", 2);
    octaveBox.setSelectedId (1, juce::dontSendNotification);
    keyBox.onChange = octaveBox.onChange = [this]
    {
        tuningDirty = true;
        tuningCountdown = 0;
    };

    crossButton.setColour (juce::ToggleButton::textColourId, text);
    crossButton.setToggleState (true, juce::dontSendNotification);
    crossButton.setTooltip ("A smaller top hole makes the cross-fingered flat 7th (C natural on a D whistle) flatter; "
                            "this picks L1's size so that note is in tune, with C# still set by L1's position.");

    tuneButton.onClick = [this]
    {
        const double hz = lowestNoteHz();
        auto tuned = geometry;
        if (whistle::tuneToScale (tuned, hz, crossButton.getToggleState()))
        {
            geometry = tuned;
            tuneStatus.setText ("Holes placed for " + noteName (hz), juce::dontSendNotification);
            refreshSliders();
            geometryChanged();
        }
        else
        {
            tuneStatus.setText ("Couldn't tune: try other hole sizes", juce::dontSendNotification);
        }
    };

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &materialBox, &keyBox, &octaveBox, &tuneButton, &tuneStatus, &crossButton })
        addAndMakeVisible (c);
    tuneStatus.setColour (juce::Label::textColourId, text);

    auto mm = [] (double& v) { return std::make_pair ([&v] { return v * 1000.0; }, [&v] (double x) { v = x / 1000.0; }); };
    auto addLength = [&] (juce::String name, double& v, double min, double max, double step = 0.1, RowKind kind = RowKind::normal)
    {
        auto [get, set] = mm (v);
        addRow (std::move (name), min, max, step, "mm", get, set, true, kind);
    };

    addHeading ("Bore");
    addLength ("Length (window to foot)", geometry.boreLength, 100.0, 700.0);
    addLength ("Diameter at top", geometry.boreDiameterTop, 6.0, 30.0);
    addLength ("Diameter at foot", geometry.boreDiameterFoot, 6.0, 30.0);
    addLength ("Wall thickness", geometry.wallThickness, 0.2, 8.0);
    addRow ("Wall roughness (losses)", 1.0, 2.0, 0.01, "x", [this] { return geometry.wallLoss; }, [this] (double v) { geometry.wallLoss = v; });

    addHeading ("Tone holes: position from the window, diameter");
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        addLength (kHoleNames[i], geometry.holes[(size_t) i].position, 20.0, 690.0, 0.1, RowKind::holePosition);
        addLength ({}, geometry.holes[(size_t) i].diameter, 2.0, 16.0, 0.1, RowKind::holeDiameter);
    }

    addHeading ("Fipple");
    addLength ("Windway height (jet thickness)", geometry.windwayHeight, 0.4, 3.0, 0.01);
    addLength ("Window length (flue to edge)", geometry.windowLength, 2.0, 14.0, 0.05);
    addLength ("Window width", geometry.windowWidth, 3.0, 25.0);
    addLength ("Edge offset (into the pipe)", geometry.edgeOffset, -0.5, 0.5, 0.01);

    addHeading ("Voicing (jet behaviour)");
    auto& model = audio.whistleModel;
    addRow ("Jet response (Strouhal f h/U)", 0.008, 0.05, 0.001, "", [&model] { return (double) model.jetStrouhal.load(); },
            [&model] (double v) { model.jetStrouhal = (float) v; }, false);
    addRow ("Jet wave speed (x jet speed)", 0.3, 0.9, 0.01, "", [&model] { return (double) model.jetConvection.load(); },
            [&model] (double v) { model.jetConvection = (float) v; }, false);
    addRow ("Breath noise", 0.0, 1.0, 0.01, "", [&model] { return (double) model.breathNoise.load(); },
            [&model] (double v) { model.breathNoise = (float) v; }, false);

    addHeading ("Output (breath and hole response: Sensors tab)");
    addRow ("Output level", -30.0, 12.0, 0.5, "dB", [&model] { return (double) model.outputGainDb.load(); },
            [&model] (double v) { model.outputGainDb = (float) v; }, false);

    // Right side
    addAndMakeVisible (whistleView);
    addAndMakeVisible (tuningTable);
    for (auto* b : { &enableButton, &manualButton })
    {
        b->setColour (juce::ToggleButton::textColourId, text);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    enableButton.setToggleState (audio.whistleEnabled.load(), juce::dontSendNotification);
    enableButton.onClick = [this] { audio.whistleEnabled = enableButton.getToggleState(); };
    manualButton.onClick = [this] { audio.manualPlay = manualButton.getToggleState(); };

    manualBreath.setRange (0.0, 2500.0, 1.0);
    manualBreath.setTextValueSuffix (" Pa");
    manualBreath.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    manualBreath.setValue (0.0, juce::dontSendNotification);
    manualBreath.onValueChange = [this] { audio.manualPressure = (float) manualBreath.getValue(); };
    manualBreath.setTooltip ("Breath pressure when playing from the screen. A D whistle's low register starts around 400 Pa; above ~1000 Pa it jumps to the second register.");

    for (auto* l : { &manualBreathLabel, &liveLabel, &tableTitle })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (l);
    }
    addAndMakeVisible (manualBreath);
    tableTitle.setText ("Tuning predicted from the geometry, for the key above (design aid; the sound comes from the simulation)", juce::dontSendNotification);

    loadPreset (0);
    startTimerHz (30);
}

InstrumentPage::~InstrumentPage()
{
    audio.manualPlay = false;
}

juce::Slider& InstrumentPage::addRow (juce::String name, double min, double max, double step, juce::String unit,
                                      std::function<double()> get, std::function<void (double)> set, bool changesGeometry,
                                      RowKind kind)
{
    Row row;
    row.kind = kind;
    row.label = std::make_unique<juce::Label> (juce::String(), name);
    row.label->setColour (juce::Label::textColourId, text);
    row.slider = std::make_unique<juce::Slider>();
    row.slider->setRange (min, max, step);
    row.slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 20);
    if (unit.isNotEmpty())
        row.slider->setTextValueSuffix (" " + unit);
    row.slider->setValue (get(), juce::dontSendNotification);

    auto* slider = row.slider.get();
    slider->onValueChange = [this, slider, set, changesGeometry]
    {
        set (slider->getValue());
        if (changesGeometry)
            geometryChanged();
    };
    // Remember how to read the value back when the geometry changes as a whole (preset, material, tuning).
    refreshers.push_back ([slider, get] { slider->setValue (get(), juce::dontSendNotification); });

    addAndMakeVisible (*row.label);
    addAndMakeVisible (*row.slider);
    leftColumn.push_back (row.label.get());
    rows.push_back (std::move (row));
    return *slider;
}

void InstrumentPage::addHeading (juce::String name)
{
    auto label = std::make_unique<juce::Label> (juce::String(), name);
    label->setColour (juce::Label::textColourId, accent);
    label->setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (*label);
    leftColumn.push_back (label.get());
    headings.push_back (std::move (label));
}

void InstrumentPage::refreshSliders()
{
    for (auto& refresh : refreshers)
        refresh();
}

void InstrumentPage::loadPreset (int index)
{
    if (index < 0 || index >= (int) whistle::presets().size())
        return;
    geometry = whistle::presets()[(size_t) index];

    for (size_t i = 0; i < whistle::materials().size(); ++i)
        if (std::abs (whistle::materials()[i].wallThickness - geometry.wallThickness) < 1e-6)
            materialBox.setSelectedId ((int) i + 1, juce::dontSendNotification);

    // The presets are all in D; low whistles an octave down.
    keyBox.setSelectedId (3, juce::dontSendNotification);
    octaveBox.setSelectedId (geometry.boreLength > 0.4 ? 2 : 1, juce::dontSendNotification);
    tuneStatus.setText ({}, juce::dontSendNotification);
    refreshSliders();
    geometryChanged();
}

void InstrumentPage::geometryChanged()
{
    audio.whistleModel.setGeometry (geometry);
    whistleView.setGeometry (geometry);
    tuningDirty = true;
    tuningCountdown = 6;  // Recompute the table once the sliders have been still for a moment.
}

void InstrumentPage::timerCallback()
{
    if (tuningDirty && --tuningCountdown <= 0)
    {
        tuningDirty = false;
        const double lowest = lowestNoteHz();
        std::vector<TuningTable::Entry> entries;
        for (const auto& c : whistle::standardChart())
            entries.push_back ({ noteName (lowest * std::pow (2.0, c.semitones / 12.0)), c.pattern, whistle::predictedCents (geometry, c, lowest) });
        tuningTable.setEntries (std::move (entries));
    }

    const float pressure = audio.shownPressure.load();
    const float jet = audio.whistleModel.jetVelocity.load();
    const float level = audio.whistleModel.level.load();
    liveLabel.setText ("Breath " + juce::String (juce::roundToInt (pressure)) + " Pa   jet " + juce::String (jet, 1) + " m/s   level "
                           + juce::String (juce::roundToInt (juce::Decibels::gainToDecibels (level, -90.0f))) + " dBFS",
                       juce::dontSendNotification);
    whistleView.repaint();
}

double InstrumentPage::lowestNoteHz() const
{
    // Lowest note in octave 5 (high whistles: C5..B5) or 4 (low whistles).
    const int midi = (octaveBox.getSelectedId() == 1 ? 72 : 60) + keyBox.getSelectedId() - 1;
    return 440.0 * std::pow (2.0, (midi - 69) / 12.0);
}

void InstrumentPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void InstrumentPage::resized()
{
    auto area = getLocalBounds().reduced (10);

    // Left: instrument choice and settings
    auto left = area.removeFromLeft (560);
    area.removeFromLeft (10);

    auto choose = left.removeFromTop (28);
    presetBox.setBounds (choose.removeFromLeft (270).reduced (2));
    materialBox.setBounds (choose.reduced (2));
    auto tuneRow = left.removeFromTop (28);
    tuneButton.setBounds (tuneRow.removeFromLeft (100).reduced (2));
    keyBox.setBounds (tuneRow.removeFromLeft (70).reduced (2));
    octaveBox.setBounds (tuneRow.removeFromLeft (160).reduced (2));
    tuneStatus.setBounds (tuneRow);
    crossButton.setBounds (left.removeFromTop (24));
    left.removeFromTop (4);

    // Rows: holes get two sliders side by side to save space.
    const int rowHeight = 23;
    size_t rowIndex = 0;
    for (auto* c : leftColumn)
    {
        const bool isHeading = std::any_of (headings.begin(), headings.end(), [c] (const auto& h) { return h.get() == c; });
        if (isHeading)
        {
            c->setBounds (left.removeFromTop (rowHeight));
            continue;
        }

        auto& row = rows[rowIndex];
        if (row.kind == RowKind::holeDiameter)
        {
            ++rowIndex;
            continue;  // Laid out together with its position row.
        }

        auto line = left.removeFromTop (rowHeight);
        if (row.kind == RowKind::holePosition)
        {
            auto& diameter = rows[rowIndex + 1];
            row.label->setBounds (line.removeFromLeft (40));
            row.slider->setBounds (line.removeFromLeft (270));
            diameter.label->setBounds ({});
            diameter.slider->setBounds (line);
        }
        else
        {
            row.label->setBounds (line.removeFromLeft (230));
            row.slider->setBounds (line);
        }
        ++rowIndex;
    }

    // Right: drawing, playing, table
    auto controls = area.removeFromTop (28);
    enableButton.setBounds (controls.removeFromLeft (160));
    manualButton.setBounds (controls.removeFromLeft (150));
    manualBreathLabel.setBounds (controls.removeFromLeft (55));
    manualBreath.setBounds (controls);

    whistleView.setBounds (area.removeFromTop (190));
    liveLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    tableTitle.setBounds (area.removeFromTop (22));
    tuningTable.setBounds (area.removeFromTop (330));
}
