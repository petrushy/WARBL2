// The "Instrument" tab: the whistle's geometry and voicing, a drawing of it with the live hole openings,
// the resonances its geometry predicts for each fingering, and playing from the screen.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Whistle/Acoustics.h"
#include "Whistle/InstrumentFile.h"

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
    juce::Rectangle<float> footBounds() const;
    float xFor (double metres) const;

    AudioEngine& audio;
    whistle::Geometry geometry;
};

// Every fingering of a standard whistle chart, with how far the geometry's resonance is from its note.
class TuningTable : public juce::Component
{
public:
    struct Entry
    {
        juce::String note;
        juce::String pattern;
        double cents = 0.0;
    };

    void setEntries (std::vector<Entry> e) { entries = std::move (e); repaint(); }
    void paint (juce::Graphics&) override;

private:
    std::vector<Entry> entries;
};

class InstrumentPage : public juce::Component, private juce::Timer
{
public:
    explicit InstrumentPage (AudioEngine&);
    ~InstrumentPage() override;

    void loadPreset (int index);

    // The whole design (geometry and voicing), e.g. for saving the app's state.
    whistle::Instrument getInstrument() const;
    void setInstrument (const whistle::Instrument&);

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
    void timerCallback() override;

    AudioEngine& audio;
    whistle::Geometry geometry;

    juce::ComboBox presetBox, materialBox, keyBox, octaveBox;
    juce::TextButton tuneButton { "Tune holes" }, saveButton { "Save..." }, loadButton { "Load..." };
    std::unique_ptr<juce::FileChooser> fileChooser;
    void saveToFile();
    void loadFromFile();
    juce::ToggleButton crossButton { "Also tune cross-fingerings with hole size (whistle: C natural sets L1)" };
    juce::ToggleButton bellButton { "Bell   (the bell sensor closes the open foot)" };
    juce::ComboBox chartBox;
    std::array<std::unique_ptr<juce::ToggleButton>, whistle::kNumHoles> holeToggles;
    std::array<size_t, whistle::kNumHoles> holeRow {};  // Index in rows of each hole's position slider.
    double lowestNoteHz() const;
    void showKeyOfInstrument();
    void setHoleEnabled (int hole, bool enabled);
    juce::Label tuneStatus;

    std::vector<Row> rows;
    std::vector<std::function<void()>> refreshers;  // Read each slider's value back from the geometry.
    std::vector<std::unique_ptr<juce::Label>> headings;
    std::vector<juce::Component*> leftColumn;  // In layout order.

    WhistleView whistleView { audio };
    TuningTable tuningTable;
    juce::ToggleButton enableButton { "Sound on" }, manualButton { "Play from screen" };
    juce::TextButton scaleButton { "Play scale" };
    juce::Slider manualBreath;
    juce::Label manualBreathLabel { {}, "Breath" }, liveLabel, tableTitle;

    bool tuningDirty = true;
    int tuningCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentPage)
};
