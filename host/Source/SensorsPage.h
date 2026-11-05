// The "Sensors" tab: how breath pressure and tone hole readings become the model's breath and hole openings,
// as input range, curve and output range (like the WARBL Config Tool's pressure curves), with live readings.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"

// A response curve with the current inputs drawn on it.
class CurveView : public juce::Component
{
public:
    struct Point
    {
        float x;
        juce::String label;
        float y = std::numeric_limits<float>::quiet_NaN();  // On the curve unless given
    };

    CurveView (juce::String xLabel, juce::String yLabel);

    void setRanges (float xMax, float yMax) { xRange = xMax; yRange = yMax; }
    std::function<float (float)> curve;          // y for x
    std::function<std::vector<Point>()> points;  // Live inputs

    void paint (juce::Graphics&) override;

private:
    juce::String xLabel, yLabel;
    float xRange = 1.0f, yRange = 1.0f;
};

class SensorsPage : public juce::Component, private juce::Timer
{
public:
    explicit SensorsPage (AudioEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
    };

    juce::Slider& addRow (std::vector<Row>& rows, juce::String name, double min, double max, double step, juce::String unit,
                          std::atomic<float>& target);
    void timerCallback() override;

    AudioEngine& audio;

    juce::Label breathTitle { {}, "Breath" }, holeTitle { {}, "Tone holes" }, breathInfo, holeInfo;
    CurveView breathCurve { "Measured pressure (Pa)", "Breath to the model (Pa)" };
    CurveView holeCurve { "Coverage (1 = covered at WARBL calibration)", "How closed (0 = open, 1 = closed)" };
    std::vector<Row> breathRows, holeRows, tiltRows;
    juce::Label tiltTitle { {}, "Tilt aims the jet" }, tiltInfo;
    juce::ToggleButton tiltToggle { "On" }, tiltInvertToggle { "Invert" };
    juce::TextButton tiltZeroButton { "Set zero now" };
    juce::TextButton breathDefaults { "Defaults" }, holeDefaults { "Defaults" };
    juce::ComboBox holeShape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SensorsPage)
};
