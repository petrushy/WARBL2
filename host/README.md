# WARBL2 sensor stream host

Computer-side tools for the WARBL2 sensor stream, where WARBL sends its sensor readings over USB MIDI SysEx instead of notes (see `warbl2_firmware/Sensor_stream.ino`). The plan is for a physical model on the computer to make the sound.

- **WARBL Stream** (`Source/`): a Mac test app. It finds WARBL, turns the stream on, and shows the tone holes, pressure, accelerometer and gyro. It also measures frame rate, lost frames and arrival jitter. "Click on breath" (off by default) plays a 1 ms click when the pressure crosses the threshold, for measuring the delay from breath to sound (see below). It has a room reverb (see below).
- **WARBL Simulator** (`tools/`): a virtual WARBL that plays a scale, as a separate MIDI device. The app also has this built in.
- **ReverbCheck** (`tools/`): measures the generated rooms and renders WAV files for listening.
- **WhistleCheck** (`tools/`): tunes and tabulates the whistle presets, and plays the model offline to measure what it sounds (see below).
- **StreamProtocol.h**: the protocol decoder. It has no JUCE dependency, so a synth plugin can use it too.

## Build

Needs Xcode command line tools and CMake (`brew install cmake`). JUCE 9.0.5 is downloaded the first time you configure.

```
cd host
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build          # protocol tests
open "build/WarblStream_artefacts/Release/WARBL Stream.app"
```

The WARBL needs the firmware with `Sensor_stream.ino`, connected with a USB cable. The stream only goes over USB.

## Without a WARBL

- **Simulate WARBL** (switch in the app) plays a scripted D-whistle scale with breath swells. It goes through the same path as the real stream, so the plots, click and (later) the instrument model all respond.
- **Play scale** (Monitor or Instrument tab, or the space bar) plays the current instrument through its own fingerings from the lowest note up two octaves. There's no synthesizer: the physical model is fingered and blown like a player would. The breath pressure for each note is the one at which the model plays it closest to its pitch, found by playing the model first (a few seconds; cached until the instrument changes), with its breath noise on as when the scale is played. A pressure is only chosen if the note sounds clearly (at least −40 dBFS) and also one step softer. The planner then plays the whole scale and gives any note that does not sound there 15 % more breath: near its threshold a flute note can start on its own but not after the previous note (hysteresis). Notes are tongued, about 0.5 s each. `WhistleCheck scale [preset] [file.wav]` plays the same scale offline, measures every note and can save it.
- `build/WarblSimulator_artefacts/Release/WarblSimulator` is the same simulated player as a virtual MIDI device, for testing the MIDI path or other programs.

Command-line options for testing (on `"WARBL Stream.app/Contents/MacOS/WARBL Stream"`): `--simulate`, `--play` (scale at start), `--output "<audio device>"`, and `--snapshot out.png`, which saves a picture of the window after 6 s and quits.

Note that a MacBook with the lid closed and no external display runs no audio at all: the app then shows "blocks 0".

## Whistle model (Instrument tab)

The sound comes from a time-domain simulation of a whistle (`Source/Whistle/`). There is no fingering lookup: pitch, half-holing, slides, cuts and overblowing all come out of the simulated air column.

- **Bore:** sound waves travel up and down the tube in short sections (digital waveguides), with wall losses, a radiating open foot, and the window opening at the top as a mass of air in series with the bore.
- **Holes:** every tone hole is a junction whose openness comes straight from its WARBL sensor (calibrated coverage). An open hole lets low frequencies escape, and a partly covered hole lets out less.
- **Windway:** the breath accelerates the air in the windway, so the jet reaches its speed (Bernoulli) in about 1 ms when blowing softly and 0.3 ms when blowing hard (ρL dU/dt = p − ½ρU²). Boundary layers grow along the windway walls (laminar, Blasius) and carry less air than the jet's core: 67–85 % of the core flow, least when blowing softly or through a long windway. With friction on, the D whistle is ~2 dB quieter, its soft notes start more slowly, its pitch rises about 20 % less as you blow harder, and at 200 Pa no low note jumps to the second register any more. Both are sliders under Voicing ("Windway air mass", "Windway friction"): 1 is the computed amount and 0 is off. WARBL's own mouthpiece chamber already smooths the measured breath a little, so the air mass may be redundant; with breath noise seeding the note it changes attacks by 0–20 ms (`WhistleCheck attack [preset] [mass=] [friction=] [windway=<mm>]`).
- **Breath gestures:** register changes depend on how the pressure moves, not only on where it is (Terrien et al. 2014). `WhistleCheck breath [preset] [wav=<prefix>]` sweeps each fingering from 100 Pa to 5 kPa and back, slowly (4 s) and fast (1 s), and lists which register sounds over which pressures in both directions. For the D whistle the octave breaks at ~950 Pa going up and falls back at ~830 Pa (low D); fast sweeps move both outwards. `WhistleCheck tongue [preset] [p=<Pa>]` repeats a note after 10, 20 and 40 ms gaps: the bore is still ringing after a short gap (−3 to −6 dB after 10 ms), so re-attacks are 2–4× faster than a fresh start. Near a note's threshold a fresh start is slow (C# at 700 Pa: ~200 ms).
- **Jet stability from the windway profile:** `WhistleCheck jet [preset]` solves Rayleigh's equation for the jet as it leaves the windway (a flat core with laminar boundary layers from the windway's length and the jet speed; `Whistle/JetStability`) and compares the growth and phase speed of disturbances with the model's current jet. It checks itself against Rayleigh's vortex-sheet jet (within 1 %). Findings so far: at the notes' fundamentals the theory gives growth ~0.35/h and phase speed ~0.4–0.5 U, close to the model's hand-set 0.38/h and effective in-band ~0.35–0.4 U. But in theory both rise with frequency (≈ +9 dB per octave of growth, phase speed 0.33 → 0.59 U), whereas the model's band-pass cuts the upper frequencies at ~12 dB per octave. Including the receptivity (the sound moves the jet sideways by v/iω before the wave grows; Fletcher, de la Cuadra), the theory's response is flat up to ωh/U ≈ 0.15 and then rises up to the jet's cutoff (ωh/U ≈ 1.3, Strouhal ≈ 0.2), ten times above the model's band. Laminar spreading over a 5 mm window changes the boundary layers only from ~0.067h to ~0.08h, too little to make a band. Tried in the model (`WhistleCheck chart 0 st=0.16 conv=0.49`), the theory's band makes most notes stop speaking: the jet favours the high resonances. Adachi and Qian (2025) found the same with a measured jet response (overblowing at too low pressures). So the model keeps its tuned band; what it stands for (the jet's development across the window, saturation at the edge) is still open. The theory's phase speed alone (0.49 U instead of 0.6) plays normally, with the octave breaks 10–20 % higher.
- **Loudness of the low notes:** the low register is limited not by the jet (more jet gain only adds odd harmonics) but by the loss in vortices shed where the sound flows round the edge (Verge's ½ρ(v/0.6)²). It keeps the low notes quiet and close to sine waves. **Edge vortex loss** (Voicing; 1 = that textbook value) sets it: at 0.5 the low notes are ~4 dB louder with a 6–8 dB stronger third harmonic, and the scale still plays. Their thresholds are set by the jet's travel time (a note speaks once the jet crosses the window fast enough for its period), so they grow roughly as the note's frequency squared. `WhistleCheck radiation [preset] [p=<Pa>] [vortex=]` lists the harmonics, from the window and from the holes and foot (for the fundamental they add: both ends breathe out together).
- **Note changes:** `WhistleCheck transition [preset] [p=<Pa>]` switches one hole within a sensor frame and tracks the pitch. It reaches the new note within 2–11 ms (a few periods). Measured flute transitions take ~10–40 ms, mostly the finger's movement while the hole is partly open; with WARBL that comes from the hole curve on the Sensors tab, so a less steep curve gives smoother changes.
- **Passive bore check:** `WhistleCheck passive [preset]` plays an impulse into the bore with the jet off and compares its resonances (frequency, Q, height) with Acoustics. They agree within ~1–4 cents and ~10 % in Q.
- **Jet:** the jet speed from the windway. The sound in the window deflects the jet, and the jet takes a pressure-dependent time to reach the edge, where it is split into and out of the pipe (jet-drive model). The jet amplifies a band of frequencies that moves up as you blow harder, so the whistle overblows to the second register.
- **Breath noise:** turbulence in a band centred on ~0.08 × jet speed ÷ windway height (about 2–3 kHz when playing), so harder blowing sounds a little brighter. At the edge it is a pressure fluctuation of order ρU², growing as U^2.5, fed into the bore at the window like the tone. (The edge-dipole law, U³, let the hiss overtake the top notes.) The bore's resonances colour it: below the playing threshold you get an airy note at the fingered pitch, peaking at the fingering's resonances. The edge noise comes in bursts as the jet sweeps across the edge ("pulsed noise", locked to the tone), so the ear hears it as breathiness in the note rather than as a separate hiss. Below the threshold the jet hardly moves and the airy noise is steady. Optionally part of it escapes straight from the window ("Breath noise heard directly", off by default). A smaller part makes the jet itself slightly unsteady.

The breath pressure from WARBL is not smoothed. The stream sends the raw ADC reading (8× hardware oversampling only), and between frames the model ramps linearly over the 2 ms frame period. The player's slow breath fluctuations (tremor, wavering, vibrato) reach the model as they are. The fast turbulence, above the 250 Hz that a 500 frames/s stream can carry, is what the model's breath noise supplies. While playing, the noise between the harmonics sits ~42–54 dB below the tone at the default setting. `WhistleCheck noise [preset]` measures this and can save a crescendo through the airy note into the tone.
- **Sample rate:** the model runs at twice the audio rate.

Settings, all live:

- **Bore:** length, diameter at the top and at the foot (cylindrical or tapered), wall thickness (sets the chimney height of the tone holes) and wall roughness. Choosing a material sets the last two.
- **Head section:** a part of the bore from the window down with its own diameter, widening (or narrowing) to the bore over its length, like the narrower head of Goldie whistles. Narrowing the bore near the window lowers the low register more than the second, which widens the octaves. In the model, a head 30 mm long and 10.5 mm at the window (12 mm bore) brings the played second register from 7–28 cents flat to close to in tune. Narrower or longer heads overshoot. Compare designs with `WhistleCheck head [preset]` (tuning) and `WhistleCheck chart [preset] headlength=<mm> headdiameter=<mm>` (played). The "D whistle, narrow head" preset uses 30 mm / 10.5 mm.

The **soprano recorder in C** preset uses baroque fingering, all eight holes, a reverse-conical bore (13 mm to 9.5 mm), 3.5 mm walls and a wider windway. Its dimensions are typical values, not a measured instrument. The chart follows Dolmetsch's baroque chart for recorders in C, up to top D, including the second- and third-octave forks; top C# closes the foot with the bell sensor. Following the maker's rule that a hole's position mostly sets its low note and its size its upper note (Philippe Bolton), the tuner sets R1's size by the F# fork, R3's by E'' and L3's by A''. The geometry then predicts every note but these within 0–16 cents: Bb' +29, G#'' −19, and the third octave about 30 cents flat. Each preset has its own voicing. The recorder's is a thinner windway (0.9 mm) and a slightly higher jet band (Strouhal 0.025). With it, every chart note sounds in the simulation: the low register from ~200 Pa, high Bb from 1.5 kPa, high B from 2.1 kPa, top C from 1.7 kPa and top D from 3.7 kPa. With the default jet band the jet couldn't drive those top notes at all (`WhistleCheck notes recorder <thumb opening>` shows this).

The thumb's opening is not fixed. When playing, it comes continuously from WARBL's thumb sensor (through the hole response curve), and how far the player opens it changes the upper notes as on a real recorder. The chart's "p" (pinched = 25 % open) is only the opening the tuning table, tuner and checks assume. In the simulation every chart note plays, including the forks and the upper octave. Low C still starts sharp (+58 cents at its threshold), as the lowest note of every preset does to a lesser degree.

The presets store their tuned dimensions: the tuner is too slow to run at startup. `WhistleCheck code` retunes them and prints the numbers for `WhistleGeometry.cpp`, and the `PresetTuningTest` test fails if they drift out of tune.
- **Tone holes:** up to eight, one per WARBL finger sensor: LT (left thumb, on the back), L1–L3 and R1–R4. Each has a position, a diameter and a switch. An unticked hole is plugged, as if it weren't drilled, and its sensor is ignored; a whistle uses L1–R3. **Bell sensor closes the foot** lets the bell sensor cover the open end (a recorder technique).
- **Sensors for a whistle:** every whistle preset comes twice, "…, holes L1-R3" and "…, holes L2-R4" (played with the lower six fingers). They are the same instrument: its holes keep their positions and sizes, only the WARBL sensors that play them differ. The whistle chart is relative (its six fingers are the holes in use, from the top), so the tuning table and tuner work either way. The built-in simulator always plays on L1–R3.
- **Fingering chart:** whistle, or baroque recorder (with the forked F, F# and Bb, and the second register by pinching the thumb hole). The chart is used by the tuning table, the tuner and the checks, never by the sound.
- **Fipple:** windway height (jet thickness), windway length, window length (flue exit to edge), window width, and edge offset.
- **Voicing:** the jet's band (Strouhal number), how fast disturbances travel along the jet, and breath noise.
- **Output level.**

## Saving

- **The app remembers its state between runs.** It saves the current instrument, the sensor response, the reverb (room or IR file, level, pre-delay), the audio device and buffer size, and the open tab to `~/Library/Application Support/WARBL Stream/WARBL Stream.settings`.
- **Instrument files:** **Save...** and **Load...** on the Instrument tab write and read whistle designs as `.whistle` files. These are JSON, with lengths in millimetres, so you can read and edit them by hand. Fields left out get the default whistle's values. A loaded design shows its tuning in the key it is in.
- **Sensor curves aren't in instrument files.** They describe the player and their WARBL rather than the instrument.

## Sensor response (Sensors tab)

How the readings become the model's controls, with live dots on each curve. Curves use the WARBL Config Tool's 0–127 scale: 64 is linear, lower is logarithmic (more response at the start), higher is exponential.

- **Breath:** WARBL's sensor gives about 1.66 Pa per count and reads up to about 5 kPa. The defaults were chosen by playing WARBL: measured 10–2500 Pa maps to 0–4930 Pa for the model, with a curve (34) that boosts soft blowing (about 2× overall, "Sensitivity" on the tab). Normal WARBL breath then reaches the whistle's second register, and the hardest blowing its upper registers. The hole defaults are an S-curve with steepness 82 and centre 0.51.
  - **Input minimum:** a threshold below which there is no breath.
  - **Input maximum:** the measured pressure that gives full output.
  - **Curve.**
  - **Output maximum:** the pressure the model then gets.
  - Sensitivity = output max ÷ input range.
- **Tone holes:** coverage is calibrated by WARBL, with 0 = open and 1 = covered at calibration.
  - **Open below:** a finger hovering above the hole has no effect.
  - **Closed at:** lower makes the hole close sooner.
  - **Shape:** an **S-curve** (the default): little change while the finger is far away and in the last bit before it's down, steep in between. Here **Curve** sets how steep the S is (64 = straight) and **Centre** where the steep part sits. Or a **power curve**: above 64 the hole stays open longer as the finger arrives. Or **finger height (physical)**: the finger's height is estimated from the sensor (reflected light falls off roughly as (d₀/(d+d₀))², d₀ = **Finger range**, 2.5 mm by default), and each hole opens as the air escaping through the ring between finger and rim lets it: that gap acts like an extra chimney length b²·ln(R/b)/(2d) (b the hole's radius, R the fingertip's). So a hovering finger still flattens the note, a big hole needs the finger higher than a small one, and the opening is gradual (a 7 mm hole is ~50 % open with the finger 1 mm above it, ~65 % at 2 mm, sealed within 0.1 mm). Curve and Centre don't apply to it.

The table on the right is a design aid. For every fingering of a standard whistle chart (including the cross-fingered C natural, oxxooo, and high D with L1 vented), it shows how far the resonance the geometry gives is from the note, in that note's register. The resonances are calculated in the frequency domain with the transfer-matrix method. **Tune holes** moves the bore length and hole positions so the scale is in tune in the chosen key. With the cross-fingering option on (the default, also used for the presets), it also sets L1's diameter: a smaller top hole makes C natural flatter, and L1's position still sets C#. Without it, C natural comes out about 55 cents sharp, as on many real whistles. The played pitch rises above the table as you blow harder (see `WhistleCheck chart`).

**Play from screen** plays the model without WARBL. Click a hole to cover or uncover it, right-click to half-cover it, and set the breath pressure with the slider. On the default D whistle the low register starts at around 400 Pa and the second register takes over above about 1000 Pa.

In the check tools, `[preset]` is a number or part of a preset's name (e.g. `recorder`, `"narrow head"`). `WhistleCheck play [preset]` runs the model at each scale fingering and a range of pressures and reports which register it plays and how far from the predicted pitch. `WhistleCheck modes [preset]` shows which register each fingering plays as the pressure rises to 8.5 kPa. Blowing harder climbs through the resonances in the model: the D whistle's low D gives the octave from ~1000 Pa, the twelfth (A6) from ~4000 Pa and the double octave from ~7000 Pa, with pitch rising (sharp) in the upper registers. The screen's breath slider goes to 9 kPa. With WARBL the default breath response reaches about 4.9 kPa; to go higher, raise **Output maximum** on the Sensors tab. `WhistleCheck slide <hole> <Pa>` opens one hole gradually and reports the pitch. `WhistleCheck chart [preset]` goes through the whole chart: the predicted tuning, the pressure range in which the model plays each note, and its pitch at the bottom and top of that range.

## Reverb

Convolution reverb with no added latency (JUCE's non-uniform partitioned convolution). The room impulse responses are generated from room acoustics rather than recorded:

- **Early reflections:** mirror images of the source in a box-shaped room, heard by two ears 17 cm apart, up to the mixing time (√V ms).
- **Diffuse tail:** its level follows from the room volume and listener distance, and it decays with its own reverberation time at each frequency, so highs die away sooner, as in real rooms.
- **Balance:** the direct sound comes from the dry signal. At Level 0 dB the dry/reverb balance is physically right for the room's listener distance; the default is -6 dB.

Rooms: small wooden room (0.4 s), studio live room (0.6 s), chamber music hall (1.4 s), concert hall (2.0 s), stone church (3.8 s); times are at 1 kHz. `ReverbCheck <dir>` verifies each room's reverberation time per octave band (within 15 % of target) and its direct-to-reverberant ratio (within 2 dB of theory). It also writes the IRs, and a short synthetic phrase through each room (`TestPhrase`, used only by this tool), as WAV files.

**Load IR file...** takes any measured impulse response (WAV/AIFF/FLAC, mono or stereo). Free recordings of real halls and churches are available from the OpenAIR library (openair.hosted.york.ac.uk); check each file's licence.

## Reading the numbers

- **frames/s**: should be ~500 with the current firmware (2 ms loop). **lost** counts gaps in the sequence numbers and should stay 0.
- **Arrival jitter**: how much later than the fastest recent frame each frame reached the app. This is the part of the transport delay that varies. The constant part can't be measured from the stream alone, because the WARBL and Mac clocks aren't synchronised.
- **output latency**: audio buffer plus the delay the output device reports. On the Mac's built-in headphone output this is ~6 ms even with a 64-sample buffer. A good USB audio interface is usually lower. Use **Audio settings...** to choose the device and buffer size.
- **max frames/block**: the most frames handled in one audio block. A large number means frames are bunching up.

## Measuring breath-to-sound delay

Record two channels at once: a microphone at the mouthpiece and the app's click output (for example looped into an audio interface input). Blow sharply. The time between the start of the breath noise and the click is the full delay: sensor, firmware, USB, app and audio output.
