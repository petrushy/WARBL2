// A scale for trying an instrument without WARBL: the instrument's own chart fingerings from the lowest note
// up two octaves, each with the breath pressure at which the physical model plays it closest to its pitch.
// The pressures are found by running the model offline, so the scale suits any geometry and voicing.

#pragma once

#include "Acoustics.h"

#include <string>

namespace whistle
{

struct ScaleStep
{
    std::string name;   // Note name, e.g. "F#5"
    Openness openness;
    float pressure = 0.0f;  // Pa
    float cents = 0.0f;     // How far from the note the model plays it at that pressure, within the scale
    double targetHz = 0.0;
};

// The scale's timing: each note sounds for kScaleNoteSeconds, tongued (attack, hold, release) with a short
// gap in which the fingers move to the next note.
constexpr double kScaleNoteSeconds = 0.5, kScaleAttack = 0.03, kScaleRelease = 0.04, kScaleGap = 0.05;

// The model's controls at time t (seconds from the start of the scale), and which step that is (-1 once the
// scale has ended, with the breath off).
struct Controls;
int scaleControlsAt (const std::vector<ScaleStep>&, double t, Controls&);

// Takes a few seconds (it plays every candidate pressure through the model); call it off the audio thread.
// Notes the model doesn't play within 60 cents of their pitch are left out. The plan is checked by playing
// the whole scale: near its threshold a note may sound on its own but not after the previous one (flutes
// have hysteresis there), so such notes get more breath.
std::vector<ScaleStep> planScale (const Geometry&, const Voicing&, double lowestHz, int semitones = 24);

// Plays the scale as the app does (one model, 64-sample blocks at `rate`) and returns the pitch of each note's
// steady part (0 if it doesn't sound clearly). Keeps the sound in `audio` if given.
std::vector<double> playScale (const Geometry&, const Voicing&, const std::vector<ScaleStep>&, double rate,
                               std::vector<float>* audio = nullptr);

} // namespace whistle
