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
    float cents = 0.0f;     // How far from the note the model plays it at that pressure
};

// The scale's timing: each note sounds for kScaleNoteSeconds, tongued (attack, hold, release) with a short
// gap in which the fingers move to the next note.
constexpr double kScaleNoteSeconds = 0.5, kScaleAttack = 0.03, kScaleRelease = 0.04, kScaleGap = 0.05;

// The model's controls at time t (seconds from the start of the scale), and which step that is (-1 once the
// scale has ended, with the breath off).
struct Controls;
int scaleControlsAt (const std::vector<ScaleStep>&, double t, Controls&);

// Takes a second or so (it plays every candidate pressure through the model); call it off the audio thread.
// Notes the model doesn't play within 60 cents of their pitch are left out.
std::vector<ScaleStep> planScale (const Geometry&, const Voicing&, double lowestHz, int semitones = 24);

} // namespace whistle
