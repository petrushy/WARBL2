// The "Instrument" tab: the whistle's geometry and voicing, a drawing of it with the live hole openings,
// the resonances its geometry predicts for each fingering, and playing from the screen.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Whistle/Acoustics.h"

// Side view of the whistle to scale. Holes show how open they are; in manual play, clicking a hole covers or
// uncovers it, and right-clicking half-covers it.
class WhistleView : public juce::Component
{
public:
    explicit WhistleView (AudioEngine&);
    void setGeometry (const whistle::Geometry& g) { geometry = g; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> holeBounds (int hole) const;
    float xFor (double metres) const;

    AudioEngine& audio;
    whistle::Geometry geometry;
};

// Predicted resonances (from the geometry) for the plain scale fingerings.
class TuningTable : public juce::Component
{
public:
    void setResults (std::vector<std::vector<double>> r) { results = std::move (r); repaint(); }
    void paint (juce::Graphics&) override;

private:
    std::vector<std::vector<double>> results;  // Per fingering (all covered first), resonances in Hz.
};

class InstrumentPage : public juce::Component, private juce::Timer
{
public:
    explicit InstrumentPage (AudioEngine&);
    ~InstrumentPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum class RowKind { normal, holePosition, holeDiameter };  // A hole's two sliders share one line.

    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        RowKind kind = RowKind::normal;
    };

    // A labelled slider. `get`/`set` read and write the value in the slider's units.
    juce::Slider& addRow (juce::String name, double min, double max, double step, juce::String unit,
                          std::function<double()> get, std::function<void (double)> set, bool changesGeometry = true,
                          RowKind kind = RowKind::normal);
    void addHeading (juce::String name);

    void geometryChanged();
    void refreshSliders();
    void loadPreset (int index);
    void timerCallback() override;

    AudioEngine& audio;
    whistle::Geometry geometry;

    juce::ComboBox presetBox, materialBox, keyBox, octaveBox;
    juce::TextButton tuneButton { "Tune holes" };
    juce::Label tuneStatus;

    std::vector<Row> rows;
    std::vector<std::function<void()>> refreshers;  // Read each slider's value back from the geometry.
    std::vector<std::unique_ptr<juce::Label>> headings;
    std::vector<juce::Component*> leftColumn;  // In layout order.

    WhistleView whistleView { audio };
    TuningTable tuningTable;
    juce::ToggleButton enableButton { "Whistle sound on" }, manualButton { "Play from screen" };
    juce::Slider manualBreath;
    juce::Label manualBreathLabel { {}, "Breath" }, liveLabel, tableTitle;

    bool tuningDirty = true;
    int tuningCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPage)
};
