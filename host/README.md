# WARBL2 sensor stream host

Computer-side tools for the WARBL2 sensor stream, where WARBL sends its sensor readings over USB MIDI SysEx instead of notes (see `warbl2_firmware/Sensor_stream.ino`). The plan is for a physical model on the computer to make the sound.

- **WARBL Stream** (`Source/`): a Mac test app. It finds WARBL, turns the stream on, and shows the tone holes, pressure, accelerometer and gyro. It also measures frame rate, lost frames and arrival jitter. "Click on breath" plays a 1 ms click when the pressure crosses the threshold, for measuring the delay from breath to sound (see below). It has a room reverb (see below).
- **WARBL Simulator** (`Tools/`): a virtual WARBL that plays a scale, as a separate MIDI device. The app also has this built in.
- **ReverbCheck** (`Tools/`): measures the generated rooms and renders WAV files for listening.
- **WhistleCheck** (`Tools/`): tunes and tabulates the whistle presets, and plays the model offline to measure what it sounds (see below).
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
- **Play test phrase** (or the space bar) plays a short synthetic whistle phrase through the reverb.
- `build/WarblSimulator_artefacts/Release/WarblSimulator` is the same simulated player as a virtual MIDI device, for testing the MIDI path or other programs.

Command-line options for testing (on `"WARBL Stream.app/Contents/MacOS/WARBL Stream"`): `--simulate`, `--play` (test phrase at start), `--output "<audio device>"`, and `--snapshot out.png`, which saves a picture of the window after 6 s and quits.

Note that a MacBook with the lid closed and no external display runs no audio at all: the app then shows "blocks 0".

## Whistle model (Instrument tab)

The sound comes from a time-domain simulation of a whistle (`Source/Whistle/`). There is no fingering lookup: pitch, half-holing, slides, cuts and overblowing all come out of the simulated air column.

- **Bore:** sound waves travel up and down the tube in short sections (digital waveguides), with wall losses, a radiating open foot, and the window opening at the top as a mass of air in series with the bore.
- **Holes:** every tone hole is a junction whose openness comes straight from its WARBL sensor (calibrated coverage). An open hole lets low frequencies escape, and a partly covered hole lets out less.
- **Jet:** breath pressure sets the jet speed (Bernoulli). The sound in the window deflects the jet, and the jet takes a pressure-dependent time to reach the edge, where it is split into and out of the pipe (jet-drive model). The jet amplifies a band of frequencies that moves up as you blow harder, so the whistle overblows to the second register. Turbulence adds breath noise.
- **Sample rate:** the model runs at twice the audio rate.

Settings, all live:

- **Bore:** length, diameter at the top and at the foot (cylindrical or tapered), wall thickness (sets the chimney height of the tone holes) and wall roughness. Choosing a material sets the last two.
- **Tone holes:** position and diameter of each hole.
- **Fipple:** windway height (jet thickness), window length (flue exit to edge), window width, and edge offset.
- **Voicing:** the jet's band (Strouhal number), how fast disturbances travel along the jet, and breath noise.
- **Player:** breath scale (multiplies the measured pressure; WARBL's sensor gives about 1.66 Pa per count) and output level.

The table on the right is a design aid. It shows the resonances the geometry gives for each scale fingering, calculated in the frequency domain with the transfer-matrix method, and how far each second register is from a true octave. **Tune holes** moves the bore length and hole positions so the scale is in tune in the chosen key, keeping the hole sizes. The played pitch differs a little from the table, because blowing harder raises it, as on a real whistle.

**Play from screen** plays the model without WARBL. Click a hole to cover or uncover it, right-click to half-cover it, and set the breath pressure with the slider. On the default D whistle the low register starts at around 400 Pa and the second register takes over above about 1000 Pa.

`WhistleCheck play [preset]` runs the model at each scale fingering and a range of pressures and reports which register it plays and how far from the predicted pitch. `WhistleCheck slide <hole> <Pa>` opens one hole gradually and reports the pitch.

## Reverb

Convolution reverb with no added latency (JUCE's non-uniform partitioned convolution). The room impulse responses are generated from room acoustics rather than recorded:

- **Early reflections:** mirror images of the source in a box-shaped room, heard by two ears 17 cm apart, up to the mixing time (√V ms).
- **Diffuse tail:** its level follows from the room volume and listener distance, and it decays with its own reverberation time at each frequency, so highs die away sooner, as in real rooms.
- **Balance:** the direct sound comes from the dry signal. At Level 0 dB the dry/reverb balance is physically right for the room's listener distance; the default is -6 dB.

Rooms: small wooden room (0.4 s), studio live room (0.6 s), chamber music hall (1.4 s), concert hall (2.0 s), stone church (3.8 s); times are at 1 kHz. `ReverbCheck <dir>` verifies each room's reverberation time per octave band (within 15 % of target) and its direct-to-reverberant ratio (within 2 dB of theory). It also writes the IRs and the test phrase through each room as WAV files.

**Load IR file...** takes any measured impulse response (WAV/AIFF/FLAC, mono or stereo). Free recordings of real halls and churches are available from the OpenAIR library (openair.hosted.york.ac.uk); check each file's licence.

## Reading the numbers

- **frames/s**: should be ~500 with the current firmware (2 ms loop). **lost** counts gaps in the sequence numbers and should stay 0.
- **Arrival jitter**: how much later than the fastest recent frame each frame reached the app. This is the part of the transport delay that varies. The constant part can't be measured from the stream alone, because the WARBL and Mac clocks aren't synchronised.
- **output latency**: audio buffer plus the delay the output device reports. On the Mac's built-in headphone output this is ~6 ms even with a 64-sample buffer. A good USB audio interface is usually lower. Use **Audio settings...** to choose the device and buffer size.
- **max frames/block**: the most frames handled in one audio block. A large number means frames are bunching up.

## Measuring breath-to-sound delay

Record two channels at once: a microphone at the mouthpiece and the app's click output (for example looped into an audio interface input). Blow sharply. The time between the start of the breath noise and the click is the full delay: sensor, firmware, USB, app and audio output.
