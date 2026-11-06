#include "InstrumentPage.h"

namespace
{
const juce::Colour background { 0xff1e1f22 };
const juce::Colour panel { 0xff2a2c30 };
const juce::Colour text { 0xffd8d8d8 };
const juce::Colour accent { 0xff4fa3e0 };
const juce::Colour coveredColour { 0xffe0a24f };
const juce::Colour metal { 0xff9aa3ad };

using whistle::kHoleNames;

juce::String noteName (double hz, double referenceA)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / referenceA));
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
    // Front holes above the tube; the thumb hole (on the back) below it.
    const auto& h = geometry.holes[(size_t) hole];
    const float scale = (xFor (1.0) - xFor (0.0));
    const float d = std::max (8.0f, (float) h.diameter * scale);
    return juce::Rectangle<float> (d, d).withCentre ({ xFor (h.position), getHeight() * (hole == 0 ? 0.72f : 0.38f) });
}

juce::Rectangle<float> WhistleView::footBounds() const
{
    const float x = xFor (geometry.boreLength);
    return { x - 4.0f, getHeight() * 0.55f - 22.0f, 12.0f, 44.0f };
}

void WhistleView::paint (juce::Graphics& g)
{
    g.fillAll (panel);

    const float scale = xFor (1.0) - xFor (0.0);
    const float centre = getHeight() * 0.55f;
    const float topHalf = (float) geometry.radiusAt (0.0) * scale + 4.0f;

    // Body: the bore profile (including any head section), drawn 4 px wider than the bore for the wall.
    juce::Path body;
    const float x0 = xFor (0.0);
    constexpr int steps = 100;
    body.startNewSubPath (x0 - 50.0f, centre - topHalf);
    for (int i = 0; i <= steps; ++i)
    {
        const double x = geometry.boreLength * i / steps;
        body.lineTo (xFor (x), centre - (float) geometry.radiusAt (x) * scale - 4.0f);
    }
    for (int i = steps; i >= 0; --i)
    {
        const double x = geometry.boreLength * i / steps;
        body.lineTo (xFor (x), centre + (float) geometry.radiusAt (x) * scale + 4.0f);
    }
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

    // Holes in use, with live openness
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        if (! geometry.holes[(size_t) i].enabled)
            continue;
        const auto r = holeBounds (i);
        const float open = audio.shownOpenness[(size_t) i].load();
        g.setColour (panel.darker (0.6f));
        g.fillEllipse (r);
        g.setColour (coveredColour.withAlpha (1.0f - open));
        g.fillEllipse (r.reduced (1.0f));
        g.setColour (metal);
        g.drawEllipse (r, 1.2f);
        g.setColour (text);
        const auto label = i == 0 ? r.withY (r.getBottom() + 2.0f) : r.withY (r.getY() - 16.0f);
        g.drawText (i == 0 ? "LT (back)" : kHoleNames[(size_t) i], label.withHeight (14.0f).expanded (24.0f, 0.0f), juce::Justification::centred);
    }

    // Foot, if the bell sensor can close it
    if (geometry.bellClosesFoot)
    {
        const auto r = footBounds();
        g.setColour (coveredColour.withAlpha (1.0f - audio.shownFootOpenness.load()));
        g.fillRect (r);
        g.setColour (metal);
        g.drawRect (r, 1.2f);
        g.setColour (text);
        g.drawText ("bell", r.withY (r.getBottom() + 2.0f).withHeight (14.0f).expanded (14.0f, 0.0f), juce::Justification::centred);
    }

    g.setColour (text.withAlpha (0.7f));
    g.drawText (juce::String (geometry.boreLength * 1000.0, 1) + " mm from window to foot",
                getLocalBounds().removeFromBottom (18).toFloat(), juce::Justification::centred);
}

void WhistleView::mouseDown (const juce::MouseEvent& e)
{
    if (! audio.manualPlay)
        return;
    auto toggle = [&] (std::atomic<float>& open)
    {
        if (e.mods.isPopupMenu())
            open = 0.5f;
        else
            open = open.load() > 0.5f ? 0.0f : 1.0f;
        repaint();
    };
    for (int i = 0; i < whistle::kNumHoles; ++i)
        if (geometry.holes[(size_t) i].enabled && holeBounds (i).expanded (6.0f).contains (e.position))
            toggle (audio.manualOpenness[(size_t) i]);
    if (geometry.bellClosesFoot && footBounds().expanded (6.0f).contains (e.position))
        toggle (audio.manualFootOpenness);
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

    // Rows shrink if the chart is long, so every note shows.
    const int rowHeight = entries.empty() ? 19 : juce::jlimit (13, 19, area.getHeight() / (int) entries.size());
    for (const auto& e : entries)
    {
        auto line = area.removeFromTop (rowHeight);
        // Filled, open and half circles; plugged or unused holes left out.
        juce::String pattern;
        for (auto ch : e.pattern)
        {
            if (ch == 'x')
                pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f"));
            else if (ch == 'o')
                pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b"));
            else if (ch == 'h' || ch == 'p')
                pattern += juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x90"));
            else if (ch == '|')
                pattern += " ";
        }

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

    // Instrument choice: built-in presets, then the player's own.
    refreshPresetList();
    presetBox.setSelectedId (1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= kUserPresetId)
            loadUserPreset (id - kUserPresetId);
        else if (id > 0)
            loadPreset (id - 1);
        deleteButton.setEnabled (id >= kUserPresetId);
    };

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
    octaveBox.addItem ("high (C5-B5)", 1);
    octaveBox.addItem ("low (C4-B4)", 2);
    octaveBox.setSelectedId (1, juce::dontSendNotification);
    octaveBox.setTooltip ("The instrument's lowest note: in octave 5 (high whistles, recorders) or 4 (low whistles)");
    keyBox.onChange = octaveBox.onChange = [this]
    {
        tuningDirty = true;
        tuningCountdown = 0;
        audio.setScaleInstrument (geometry, lowestNoteHz());
    };

    referenceBox.setSliderStyle (juce::Slider::IncDecButtons);
    referenceBox.setRange (380.0, 500.0, 0.5);
    referenceBox.setTextValueSuffix (" Hz");
    referenceBox.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 62, 20);
    referenceBox.setValue (geometry.referenceA, juce::dontSendNotification);
    referenceBox.setTooltip ("Pitch standard: the frequency of A4 that the tuner, the tuning table and the scale aim for "
                             "(440 Hz; e.g. 442 for many orchestras, 415 for baroque pitch). Press Tune holes to retune.");
    referenceBox.onValueChange = [this]
    {
        geometry.referenceA = referenceBox.getValue();
        tuningDirty = true;
        tuningCountdown = 0;
        audio.setScaleInstrument (geometry, lowestNoteHz());
    };
    referenceLabel.setColour (juce::Label::textColourId, text);
    referenceLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (referenceBox);
    addAndMakeVisible (referenceLabel);
    refreshers.push_back ([this] { referenceBox.setValue (geometry.referenceA, juce::dontSendNotification); });

    crossButton.setColour (juce::ToggleButton::textColourId, text);
    crossButton.setToggleState (true, juce::dontSendNotification);
    crossButton.setTooltip ("A smaller top hole makes the cross-fingered flat 7th (C natural on a D whistle) flatter; "
                            "this picks L1's size so that note is in tune, with C# still set by L1's position.");

    chartBox.addItem ("Whistle fingering", 1);
    chartBox.addItem ("Recorder fingering (baroque)", 2);
    chartBox.addItem (juce::String::fromUTF8 ("Härjedalspipa (traditional scale)"), 3);
    chartBox.setTooltip ("Which fingering chart the tuning table and the tuner use. The sound never uses a chart.");
    chartBox.onChange = [this]
    {
        const int id = chartBox.getSelectedId();
        geometry.chart = id == 2 ? whistle::FingeringChart::baroqueRecorder
                       : id == 3 ? whistle::FingeringChart::harjedalspipa
                                 : whistle::FingeringChart::whistle;
        geometryChanged();
    };
    bellButton.setColour (juce::ToggleButton::textColourId, text);
    bellButton.setTooltip ("Covering WARBL's bell sensor closes the instrument's open foot (a recorder technique for some high notes). "
                           "Off: the bell sensor is ignored.");
    bellButton.setWantsKeyboardFocus (false);
    bellButton.onClick = [this]
    {
        geometry.bellClosesFoot = bellButton.getToggleState();
        geometryChanged();
    };

    tuneButton.onClick = [this]
    {
        const double hz = lowestNoteHz();
        auto tuned = geometry;
        bool ok = whistle::tuneToScale (tuned, hz, crossButton.getToggleState());
        juce::String resized;
        if (! ok)
        {
            // The holes can't all reach their notes at these sizes: let the tuner change the sizes of the ones
            // that are stuck, and say which.
            tuned = geometry;
            ok = whistle::tuneToScale (tuned, hz, crossButton.getToggleState(), true);
            for (int i = 0; ok && i < whistle::kNumHoles; ++i)
            {
                const double before = geometry.holes[(size_t) i].diameter, after = tuned.holes[(size_t) i].diameter;
                if (geometry.holes[(size_t) i].enabled && std::abs (after - before) > 0.00005)
                    resized << (resized.isEmpty() ? "; resized " : ", ") << whistle::kHoleNames[(size_t) i] << " "
                            << juce::String (before * 1000.0, 1) << " -> " << juce::String (after * 1000.0, 1) << " mm";
            }
        }
        if (ok)
        {
            geometry = tuned;
            tuneStatus.setText ("Holes placed for " + noteName (hz, geometry.referenceA) + resized, juce::dontSendNotification);
            tuneStatus.setTooltip (tuneStatus.getText());
            refreshSliders();
            geometryChanged();
        }
        else
        {
            tuneStatus.setText ("Couldn't tune, even with other hole sizes: try another key or bore length", juce::dontSendNotification);
        }
    };

    savePresetButton.onClick = [this] { savePreset(); };
    deleteButton.onClick = [this] { deletePreset(); };
    exportButton.onClick = [this] { saveToFile(); };
    importButton.onClick = [this] { loadFromFile(); };
    savePresetButton.setTooltip ("Save this whistle (geometry and voicing) as one of your presets, in the list above");
    deleteButton.setTooltip ("Delete the selected preset of yours");
    exportButton.setTooltip ("Save this whistle to a .whistle file anywhere, e.g. to share it");
    importButton.setTooltip ("Load a .whistle file");
    deleteButton.setEnabled (false);

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &materialBox, &keyBox, &octaveBox, &tuneButton, &tuneStatus, &crossButton,
                                                             &savePresetButton, &deleteButton, &exportButton, &importButton, &chartBox, &bellButton })
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
    addLength ("Head section length (0 = none)", geometry.headLength, 0.0, 150.0, 0.5);
    addLength ("Head diameter at the window", geometry.headDiameter, 4.0, 30.0);
    addLength ("Wall thickness", geometry.wallThickness, 0.2, 8.0);
    addRow ("Wall roughness (losses)", 1.0, 2.0, 0.01, "x", [this] { return geometry.wallLoss; }, [this] (double v) { geometry.wallLoss = v; });

    addHeading ("Tone holes (unticked = plugged): position from the window, diameter");
    for (int i = 0; i < whistle::kNumHoles; ++i)
    {
        holeRow[(size_t) i] = rows.size();
        addLength (kHoleNames[(size_t) i], geometry.holes[(size_t) i].position, 10.0, 690.0, 0.1, RowKind::holePosition);
        addLength ({}, geometry.holes[(size_t) i].diameter, 2.0, 16.0, 0.1, RowKind::holeDiameter);

        auto toggle = std::make_unique<juce::ToggleButton>();
        toggle->setWantsKeyboardFocus (false);
        toggle->setTooltip (juce::String ("Use the ") + kHoleNames[(size_t) i] + " hole (" + (i == 0 ? "left thumb, on the back" : "front")
                            + "); unticked, it is plugged and its sensor is ignored");
        auto* t = toggle.get();
        toggle->onClick = [this, i, t] { setHoleEnabled (i, t->getToggleState()); };
        addAndMakeVisible (*toggle);
        holeToggles[(size_t) i] = std::move (toggle);
    }
    refreshers.push_back ([this]
    {
        for (int i = 0; i < whistle::kNumHoles; ++i)
        {
            const bool on = geometry.holes[(size_t) i].enabled;
            holeToggles[(size_t) i]->setToggleState (on, juce::dontSendNotification);
            for (size_t r : { holeRow[(size_t) i], holeRow[(size_t) i] + 1 })
            {
                rows[r].slider->setEnabled (on);
                rows[r].label->setEnabled (on);
            }
        }
        chartBox.setSelectedId (geometry.chart == whistle::FingeringChart::baroqueRecorder ? 2
                                : geometry.chart == whistle::FingeringChart::harjedalspipa ? 3
                                                                                          : 1, juce::dontSendNotification);

        bellButton.setToggleState (geometry.bellClosesFoot, juce::dontSendNotification);
    });

    addHeading ("Fipple");
    addLength ("Windway height (jet thickness)", geometry.windwayHeight, 0.4, 3.0, 0.01);
    addLength ("Windway length", geometry.windwayLength, 5.0, 50.0, 0.5);
    addLength ("Window length (flue to edge)", geometry.windowLength, 2.0, 14.0, 0.05);
    addLength ("Window width", geometry.windowWidth, 3.0, 25.0);
    addLength ("Edge offset (into the pipe)", geometry.edgeOffset, -0.5, 0.5, 0.01);

    addHeading ("Voicing (jet behaviour)");
    auto& model = audio.whistleModel;
    addRow ("Jet response (Strouhal f h/U)", 0.008, 0.05, 0.001, "", [&model] { return (double) model.jetStrouhal.load(); },
            [&model] (double v) { model.jetStrouhal = (float) v; }, false);
    addRow ("Jet wave speed (x jet speed)", 0.3, 0.9, 0.01, "", [&model] { return (double) model.jetConvection.load(); },
            [&model] (double v) { model.jetConvection = (float) v; }, false);
    addRow ("Jet gain", 0.5, 2.5, 0.01, "", [&model] { return (double) model.jetGain.load(); },
            [&model] (double v) { model.jetGain = (float) v; }, false)
        .setTooltip ("How much the jet amplifies the sound's push on its way across the window (1 = the textbook growth). Higher: "
                     "the jet swings further across the edge, so the tone gets fuller and squarer, and notes speak more softly.");
    addRow ("Slow-jet growth", 0.0, 1.0, 0.01, "", [&model] { return (double) model.slowJetGrowth.load(); },
            [&model] (double v) { model.slowJetGrowth = (float) v; }, false)
        .setTooltip ("A slowly blown jet amplifies more (more wavelengths fit across the window), a fast one less: soft notes come "
                     "out rich, like a real whistle's bottom D, and the hard-blown second octave purer. 0 = the same at any "
                     "speed; 0.67 = thin-jet theory.");
    addRow ("Jet band-pass stages", 1.0, 2.0, 1.0, "", [&model] { return (double) model.jetStages.load(); },
            [&model] (double v) { model.jetStages = juce::roundToInt (v); }, false)
        .setTooltip ("How sharply the jet prefers frequencies near its Strouhal number. 2 = sharper, but its extra phase shift "
                     "narrows each register and leaves silent pressures between them; 1 = no silent gaps, notes speak more softly.");
    addRow ("Breath noise", 0.0, 1.0, 0.01, "", [&model] { return (double) model.breathNoise.load(); },
            [&model] (double v) { model.breathNoise = (float) v; }, false)
        .setTooltip ("Turbulence where the jet hits the edge. It grows with jet speed and gets brighter as you blow harder, and it "
                     "goes into the bore, so the bore's resonances colour it (an airy note below the playing threshold).");
    addRow ("Breath noise heard directly", 0.0, 1.0, 0.01, "", [&model] { return (double) model.directNoise.load(); },
            [&model] (double v) { model.directNoise = (float) v; }, false)
        .setTooltip ("How much of the edge noise escapes straight out of the window, rather than only through the bore.");
    addRow ("Windway air mass", 0.0, 1.0, 0.01, "", [&model] { return (double) model.windwayMass.load(); },
            [&model] (double v) { model.windwayMass = (float) v; }, false)
        .setTooltip ("The breath has to accelerate the air in the windway, so the jet reaches its speed in about a millisecond "
                     "(rounder attacks). 1 = as for the windway's length, 0 = off: WARBL's mouthpiece already smooths the breath a little.");
    addRow ("Windway friction", 0.0, 1.0, 0.01, "", [&model] { return (double) model.windwayFriction.load(); },
            [&model] (double v) { model.windwayFriction = (float) v; }, false)
        .setTooltip ("Boundary layers on the windway walls carry less air than the jet's core, most of all when blowing softly "
                     "(a long, narrow windway loses more). 1 = as computed, 0 = off.");
    addRow ("Edge vortex loss", 0.0, 1.5, 0.01, "", [&model] { return (double) model.vortexLoss.load(); },
            [&model] (double v) { model.vortexLoss = (float) v; }, false)
        .setTooltip ("Energy lost in the vortices shed where the sound flows round the edge (1 = the textbook value). It is what "
                     "limits how loud the low notes get: lower gives fuller, louder low notes with more harmonics.");
    addRow ("Attack chiff", 0.0, 10.0, 0.1, "", [&model] { return (double) model.attackChiff.load(); },
            [&model] (double v) { model.attackChiff = (float) v; }, false)
        .setTooltip ("A burst of breath noise at a quick (tongued) attack: the jet has just started, is turbulent, and isn't yet "
                     "steered by the sound in the pipe. It fades as the note takes over; slow swells and held notes are unchanged. "
                     "A multiple of the breath noise; 0 = none.");
    addRow ("Finger gap damping", 0.0, 1.0, 0.01, "", [&model] { return (double) model.fingerGapDamping.load(); },
            [&model] (double v) { model.fingerGapDamping = (float) v; }, false)
        .setTooltip ("Air leaving a hole under a finger just above it has to squeeze through a thin gap, which barely lets it "
                     "through: 1 = that physical resistance (a just-lifted finger changes little at first), 0 = off (the hole "
                     "acts as soon as the finger lifts).");

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
    scaleButton.setWantsKeyboardFocus (false);
    scaleButton.setTooltip ("Plays this instrument through its own fingerings from the lowest note up two octaves, with the breath "
                            "pressure the model needs for each note (found by playing the model first, which takes a few seconds).");
    scaleButton.onClick = [this] { audio.playScale(); };
    addAndMakeVisible (scaleButton);

    manualBreath.setRange (0.0, 9000.0, 1.0);
    manualBreath.setSkewFactorFromMidPoint (1500.0);  // Fine control in the low register, room for the third and fourth.
    manualBreath.setTextValueSuffix (" Pa");
    manualBreath.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    manualBreath.setValue (0.0, juce::dontSendNotification);
    manualBreath.onValueChange = [this] { audio.manualPressure = (float) manualBreath.getValue(); };
    manualBreath.setTooltip ("Breath pressure when playing from the screen. A D whistle's low register starts around 400 Pa; "
                             "it jumps to the octave above ~1000 Pa, the twelfth around 4000 Pa and the double octave around 7000 Pa.");

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

void InstrumentPage::setHoleEnabled (int index, bool enabled)
{
    auto& hole = geometry.holes[(size_t) index];
    hole.enabled = enabled;
    if (enabled && (hole.diameter <= 0.0 || hole.position <= 0.0 || hole.position >= geometry.boreLength))
    {
        // A hole that was never used: give it a size and put it between its neighbours.
        hole.diameter = 0.006;
        double above = 0.0, below = geometry.boreLength;
        for (int i = 1; i < index; ++i)
            if (geometry.holes[(size_t) i].enabled)
                above = std::max (above, geometry.holes[(size_t) i].position);
        for (int i = index + 1; i < whistle::kNumHoles; ++i)
            if (geometry.holes[(size_t) i].enabled)
                below = std::min (below, geometry.holes[(size_t) i].position);
        hole.position = index == 0 ? std::max (0.02, geometry.holes[1].position - 0.015) : 0.5 * (above + below);
    }
    refreshSliders();
    geometryChanged();
}

void InstrumentPage::showKeyOfInstrument()
{
    // The key the instrument is in: its lowest note (all holes covered).
    const auto r = whistle::resonances (geometry, whistle::Openness::allClosed(), 100.0, 2000.0);
    if (! r.empty())
    {
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (r.front() / geometry.referenceA));
        octaveBox.setSelectedId (midi >= 72 ? 1 : 2, juce::dontSendNotification);
        keyBox.setSelectedId ((midi % 12 + 12) % 12 + 1, juce::dontSendNotification);
    }
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

    audio.whistleModel.setVoicing (whistle::presetVoicing ((size_t) index));

    for (size_t i = 0; i < whistle::materials().size(); ++i)
        if (std::abs (whistle::materials()[i].wallThickness - geometry.wallThickness) < 1e-6)
            materialBox.setSelectedId ((int) i + 1, juce::dontSendNotification);

    showKeyOfInstrument();
    tuneStatus.setText ({}, juce::dontSendNotification);
    refreshSliders();
    geometryChanged();
}

void InstrumentPage::geometryChanged()
{
    audio.whistleModel.setGeometry (geometry);
    audio.setScaleInstrument (geometry, lowestNoteHz());
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
        for (const auto& c : whistle::chartFor (geometry.chart))
        {
            // The pattern for the holes in use, grouped as thumb | left hand | right hand.
            const auto holes = c.holePattern (geometry);
            juce::String pattern;
            for (int i = 0; i < whistle::kNumHoles; ++i)
            {
                if (! geometry.holes[(size_t) i].enabled)
                    continue;
                if ((i == 1 && geometry.holes[0].enabled) || i == 4)
                    pattern << '|';
                pattern << (holes[(size_t) i] == '-' ? 'o' : holes[(size_t) i]);
            }
            entries.push_back ({ noteName (lowest * std::pow (2.0, c.semitones / 12.0), geometry.referenceA), pattern, whistle::predictedCents (geometry, c, lowest) });
        }
        tuningTable.setEntries (std::move (entries));
    }

    const float pressure = audio.shownPressure.load();
    const float jet = audio.whistleModel.jetVelocity.load();
    const float level = audio.whistleModel.level.load();
    liveLabel.setText ("Breath " + juce::String (juce::roundToInt (pressure)) + " Pa   jet " + juce::String (jet, 1) + " m/s   level "
                           + juce::String (juce::roundToInt (juce::Decibels::gainToDecibels (level, -90.0f))) + " dBFS   "
                           + audio.getScaleStatus(),
                       juce::dontSendNotification);
    scaleButton.setButtonText (audio.isScaleActive() ? "Stop scale" : "Play scale");
    whistleView.repaint();
}

whistle::Instrument InstrumentPage::getInstrument() const
{
    whistle::Instrument instrument;
    instrument.geometry = geometry;
    instrument.voicing = audio.whistleModel.getVoicing();
    return instrument;
}

void InstrumentPage::setInstrument (const whistle::Instrument& instrument)
{
    geometry = instrument.geometry;
    audio.whistleModel.setVoicing (instrument.voicing);

    selectPresetNamed (geometry.name);
    materialBox.setSelectedId (0, juce::dontSendNotification);
    for (size_t i = 0; i < whistle::materials().size(); ++i)
        if (std::abs (whistle::materials()[i].wallThickness - geometry.wallThickness) < 1e-6
            && std::abs (whistle::materials()[i].wallLoss - geometry.wallLoss) < 1e-6)
            materialBox.setSelectedId ((int) i + 1, juce::dontSendNotification);

    showKeyOfInstrument();

    tuneStatus.setText ({}, juce::dontSendNotification);
    refreshSliders();
    geometryChanged();
}

juce::File InstrumentPage::userPresetFolder()
{
    // macOS: ~/Library/Application Support/WARBL Stream/Presets; Linux: ~/.config/...; Windows: AppData\Roaming\...
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    folder = folder.getChildFile ("Application Support");
   #endif
    return folder.getChildFile ("WARBL Stream").getChildFile ("Presets");
}

void InstrumentPage::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    for (size_t i = 0; i < whistle::presets().size(); ++i)
        presetBox.addItem (whistle::presets()[i].name, (int) i + 1);

    userPresets.clear();
    for (const auto& file : userPresetFolder().findChildFiles (juce::File::findFiles, false, "*.whistle"))
        userPresets.push_back (file);
    std::sort (userPresets.begin(), userPresets.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    if (! userPresets.empty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("My presets");
        for (size_t i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), kUserPresetId + (int) i);
    }
}

void InstrumentPage::selectPresetNamed (const juce::String& name)
{
    // A preset of the player's with this name, else a built-in one, else just show the name.
    for (size_t i = 0; i < userPresets.size(); ++i)
        if (userPresets[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (name))
        {
            presetBox.setSelectedId (kUserPresetId + (int) i, juce::dontSendNotification);
            deleteButton.setEnabled (true);
            return;
        }
    for (size_t i = 0; i < whistle::presets().size(); ++i)
        if (whistle::presets()[i].name == name)
        {
            presetBox.setSelectedId ((int) i + 1, juce::dontSendNotification);
            deleteButton.setEnabled (false);
            return;
        }
    presetBox.setText (name, juce::dontSendNotification);
    deleteButton.setEnabled (false);
}

void InstrumentPage::loadUserPreset (int index)
{
    if (index < 0 || index >= (int) userPresets.size())
        return;
    const auto file = userPresets[(size_t) index];
    juce::String error;
    if (auto instrument = whistle::fromJson (file.loadFileAsString(), error))
    {
        instrument->geometry.name = file.getFileNameWithoutExtension();
        setInstrument (*instrument);
    }
    else
    {
        tuneStatus.setText (file.getFileName() + ": " + error, juce::dontSendNotification);
    }
}

void InstrumentPage::savePreset()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name for this whistle (an existing preset of yours with the same name is "
                                                         "replaced):", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", geometry.name);
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
    juce::Component::SafePointer<InstrumentPage> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, safeWindow] (int result)
    {
        if (result != 1 || safeThis == nullptr || safeWindow == nullptr)
            return;
        auto& page = *safeThis;
        const auto name = juce::File::createLegalFileName (safeWindow->getTextEditorContents ("name").trim());
        if (name.isEmpty())
            return;
        const auto folder = userPresetFolder();
        folder.createDirectory();
        const auto file = folder.getChildFile (name + ".whistle");
        page.geometry.name = name;
        if (file.replaceWithText (whistle::toJson (page.getInstrument())))
        {
            page.refreshPresetList();
            page.selectPresetNamed (name);
            page.tuneStatus.setText ("Saved preset " + name, juce::dontSendNotification);
        }
        else
        {
            page.tuneStatus.setText ("Couldn't write " + file.getFullPathName(), juce::dontSendNotification);
        }
    }), true);
}

void InstrumentPage::deletePreset()
{
    const int index = presetBox.getSelectedId() - kUserPresetId;
    if (index < 0 || index >= (int) userPresets.size())
        return;
    const auto file = userPresets[(size_t) index];
    juce::Component::SafePointer<InstrumentPage> safeThis (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Delete preset")
                                      .withMessage ("Delete your preset \"" + file.getFileNameWithoutExtension() + "\"? The next preset in the list "
                                                    "is loaded.")
                                      .withButton ("Delete", 1)
                                      .withButton ("Cancel", 0)
                                      .withAssociatedComponent (this),
                                  [safeThis, file] (int result)
    {
        if (result != 1 || safeThis == nullptr)
            return;
        auto& page = *safeThis;
        if (file.moveToTrash() || file.deleteFile())
        {
            // Go on to the next preset of the player's (or the one before, if it was the last), or the first
            // built-in one if none are left.
            const auto deletedName = file.getFileNameWithoutExtension();
            const int position = (int) std::distance (page.userPresets.begin(), std::find (page.userPresets.begin(), page.userPresets.end(), file));
            page.refreshPresetList();
            if (! page.userPresets.empty())
                page.presetBox.setSelectedId (kUserPresetId + std::min (position, (int) page.userPresets.size() - 1), juce::sendNotificationSync);
            else
                page.presetBox.setSelectedId (1, juce::sendNotificationSync);
            page.tuneStatus.setText ("Deleted preset " + deletedName, juce::dontSendNotification);
        }
        else
        {
            page.tuneStatus.setText ("Couldn't delete " + file.getFullPathName(), juce::dontSendNotification);
        }
    });
}

void InstrumentPage::saveToFile()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser> ("Save whistle", folder.getChildFile (juce::File::createLegalFileName (geometry.name) + ".whistle"),
                                                      "*.whistle");
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;
        file = file.withFileExtension ("whistle");

        geometry.name = file.getFileNameWithoutExtension();
        if (file.replaceWithText (whistle::toJson (getInstrument())))
        {
            presetBox.setText (geometry.name, juce::dontSendNotification);
            tuneStatus.setText ("Saved " + file.getFileName(), juce::dontSendNotification);
        }
        else
        {
            tuneStatus.setText ("Couldn't write " + file.getFileName(), juce::dontSendNotification);
        }
    });
}

void InstrumentPage::loadFromFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load whistle", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.whistle");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (file == juce::File())
            return;

        juce::String error;
        if (auto instrument = whistle::fromJson (file.loadFileAsString(), error))
        {
            setInstrument (*instrument);
            tuneStatus.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
        }
        else
        {
            tuneStatus.setText (file.getFileName() + ": " + error, juce::dontSendNotification);
        }
    });
}

double InstrumentPage::lowestNoteHz() const
{
    // Lowest note in octave 5 (high whistles: C5..B5) or 4 (low whistles).
    const int midi = (octaveBox.getSelectedId() == 1 ? 72 : 60) + keyBox.getSelectedId() - 1;
    return geometry.referenceA * std::pow (2.0, (midi - 69) / 12.0);
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
    presetBox.setBounds (choose.removeFromLeft (250).reduced (2));
    savePresetButton.setBounds (choose.removeFromLeft (110).reduced (2));
    deleteButton.setBounds (choose.removeFromLeft (64).reduced (2));
    exportButton.setBounds (choose.removeFromLeft (68).reduced (2));
    importButton.setBounds (choose.reduced (2));
    auto tuneRow = left.removeFromTop (28);
    tuneButton.setBounds (tuneRow.removeFromLeft (100).reduced (2));
    keyBox.setBounds (tuneRow.removeFromLeft (70).reduced (2));
    octaveBox.setBounds (tuneRow.removeFromLeft (110).reduced (2));
    referenceLabel.setBounds (tuneRow.removeFromLeft (34));
    referenceBox.setBounds (tuneRow.removeFromLeft (122).reduced (2));
    tuneStatus.setBounds (tuneRow);
    crossButton.setBounds (left.removeFromTop (24));
    auto chartRow = left.removeFromTop (28);
    chartBox.setBounds (chartRow.removeFromLeft (260).reduced (2));
    materialBox.setBounds (chartRow.removeFromLeft (220).reduced (2));
    left.removeFromTop (4);

    // Rows: holes get two sliders side by side to save space.
    const int rowHeight = 20;
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
            for (int i = 0; i < whistle::kNumHoles; ++i)
                if (holeRow[(size_t) i] == rowIndex)
                    holeToggles[(size_t) i]->setBounds (line.removeFromLeft (26));
            row.label->setBounds (line.removeFromLeft (30));
            row.slider->setBounds (line.removeFromLeft (258));
            diameter.label->setBounds ({});
            diameter.slider->setBounds (line);

            // The bell isn't a hole (no position or size) but is switched on and off with them, after R4.
            if (rowIndex == holeRow[whistle::kNumHoles - 1])
                bellButton.setBounds (left.removeFromTop (rowHeight));
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
    enableButton.setBounds (controls.removeFromLeft (110));
    scaleButton.setBounds (controls.removeFromLeft (100).reduced (2));
    manualButton.setBounds (controls.removeFromLeft (150));
    manualBreathLabel.setBounds (controls.removeFromLeft (55));
    manualBreath.setBounds (controls);

    whistleView.setBounds (area.removeFromTop (190));
    liveLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    tableTitle.setBounds (area.removeFromTop (22));
    tuningTable.setBounds (area);  // The rest: a recorder's chart has 22 notes
}
