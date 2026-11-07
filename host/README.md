# WARBL2 sensor stream host

Computer-side tools for the WARBL2 sensor stream, where WARBL sends its sensor readings over USB MIDI SysEx instead of notes (see `warbl2_firmware/Sensor_stream.ino`). The plan is for a physical model on the computer to make the sound.

- **WARBL Stream** (`Source/`): the app (built and tested on macOS; see Building for Linux and Windows). It finds WARBL, turns the stream on, and shows the tone holes, pressure, accelerometer and gyro. It also measures frame rate, lost frames and arrival jitter. It has a room reverb (see below).
- **WARBL Simulator** (`tools/`): a virtual WARBL that plays a scale, as a separate MIDI device. The app also has this built in.
- **ReverbCheck** (`tools/`): measures the generated rooms and renders WAV files for listening.
- **WhistleCheck** (`tools/`): tunes and tabulates the whistle presets, and plays the model offline to measure what it sounds (see below).
- **StreamProtocol.h**: the protocol decoder. It has no JUCE dependency, so a synth plugin can use it too.

## Building

Two parts: the **WARBL firmware** (with the sensor stream) goes on the instrument, and the **WARBL Stream app** runs on the computer. The app only hears WARBL if the firmware has `Sensor_stream.ino`, and the stream only goes over a USB cable.

**Platforms.** Everything here has been built and tested on **macOS** only (Apple silicon, macOS 15). The firmware builds with the cross-platform Arduino tools, so it should build the same way on Linux and Windows. The app uses only JUCE (audio, MIDI, GUI) and standard C++20, with nothing Mac-specific in its code, so it should also build on **Linux** and **Windows**, but it hasn't been tried there yet: expect to fix small compiler differences. Known limits elsewhere: the separate `WarblSimulator` tool makes virtual MIDI ports, which Windows doesn't support (the simulator built into the app works everywhere); on Windows, use WASAPI (exclusive mode) or an ASIO-capable interface for low latency, and on Linux JACK or PipeWire.

### The app

You need a C++20 compiler, CMake 3.22 or newer, and git. The first configure downloads JUCE 9.0.5 (about 100 MB), which takes a few minutes.

**macOS:** install the Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`). Then:

```
cd host
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build          # protocol, instrument file and preset tuning tests
open "build/WarblStream_artefacts/Release/WARBL Stream.app"
```

**Linux** (untested; Debian/Ubuntu package names): install the build tools and JUCE's dependencies:

```
sudo apt install build-essential cmake git pkg-config libasound2-dev libjack-jackd2-dev \
    libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev
cd host
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build
"build/WarblStream_artefacts/Release/WARBL Stream"
```

**Windows** (untested): install Visual Studio 2022 with the "Desktop development with C++" workload, CMake and git. In a "Developer PowerShell for VS 2022":

```
cd host
cmake -B build
cmake --build build --config Release
ctest --test-dir build -C Release
& "build\WarblStream_artefacts\Release\WARBL Stream.exe"
```

Settings and your presets are kept in `~/Library/Application Support/WARBL Stream/` on macOS, `~/.config/WARBL Stream/` on Linux and `%APPDATA%\WARBL Stream\` on Windows. The check tools (`WhistleCheck`, `ReverbCheck`, `WarblSimulator`) are built alongside, in `build/<Tool>_artefacts/Release/`.

### The firmware

The firmware is built with **arduino-cli** (or the Arduino IDE, see the repository's main README) and the Adafruit nRF52 board package, with WARBL's own board definition. These steps were used for this fork, on macOS; paths for Linux and Windows are given where they differ.

1. **Install arduino-cli** (macOS: `brew install arduino-cli`; Linux and Windows: see arduino.github.io/arduino-cli) and **Python 3** (for the UF2 converter).
2. **Install the Adafruit nRF52 boards, version 1.7.0:**
   ```
   arduino-cli config init
   arduino-cli config add board_manager.additional_urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
   arduino-cli core update-index
   arduino-cli core install adafruit:nrf52@1.7.0
   ```
   On Linux the package's upload tool also needs `pip3 install --user adafruit-nrfutil`.
3. **Add the WARBL2 board:** copy `warbl2_firmware/boards.local.txt` into the board package's folder:
   - macOS: `~/Library/Arduino15/packages/adafruit/hardware/nrf52/1.7.0/`
   - Linux: `~/.arduino15/packages/adafruit/hardware/nrf52/1.7.0/`
   - Windows: `%LOCALAPPDATA%\Arduino15\packages\adafruit\hardware\nrf52\1.7.0\`
4. **Install the libraries** (their own dependencies, Adafruit BusIO and Adafruit Unified Sensor, come with them):
   ```
   arduino-cli lib install "MIDI Library@5.0.2" "Adafruit LSM6DS@4.7.4" "ResponsiveAnalogRead@1.2.1" "SensorFusion@1.0.6"
   ```
5. **Compile**, from the repository's top folder:
   ```
   arduino-cli compile --fqbn adafruit:nrf52:warbl2 --output-dir build-firmware warbl2_firmware
   ```
   On macOS put `LC_ALL=en_US.UTF-8` in front: the package's nrfutil step fails without a UTF-8 locale. A successful build ends with "Sketch uses … bytes".
6. **Make the UF2 file** that WARBL's bootloader takes:
   ```
   python3 <board package folder>/tools/uf2conv/uf2conv.py -c -f 0xADA52840 -o warbl2-sensor-stream.uf2 build-firmware/warbl2_firmware.ino.hex
   ```
   (`<board package folder>` is the folder from step 3.)
7. **Flash it:** double-click WARBL's reset button (or hold button 1 while plugging it in). A drive called WARBLBOOT appears; copy the `.uf2` file onto it. WARBL restarts with the new firmware by itself.
8. **Check:** start the app, tick **Stream** and press **Request info**. The Monitor tab should show "loop 2 ms", about 500 frames/s, "new hole readings in ~100 % of frames" and no lost frames.

The tone-hole co-processor (`ATmega_firmware/`) is unchanged in this fork: WARBL only reprograms it when `ATMEGA_FIRMWARE_VERSION` in `Defines.h` changes, so leave that alone unless you change the ATmega code. To go back to the official firmware, flash a release `.uf2` from warbl.xyz the same way.

## Without a WARBL

- **Simulate WARBL** (switch in the app) plays a scripted D-whistle scale with breath swells. It goes through the same path as the real stream, so the plots and the instrument model respond as to WARBL.
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
- **The jet widens (jet spreading, Voicing):** a jet leaving the flue at 0.4 h half-width widens on its way across the window, much more once it is turbulent (Reynolds number U h / ν above ~1000, fully above ~3000; a turbulent plane jet's half-width grows by ~0.1 per unit distance). The model kept it at 0.4 h. A wider jet at the edge switches less abruptly and carries more flow, and a given edge offset is a smaller share of it: hard-blown notes come out louder and purer. Real whistles' second octave is both (the recorded Goldie: H2 −31 to −34 dB, 6–13 dB louder than the first octave); with the jet of constant width the model's had H2 around −17 dB and the same level as the first, whatever the edge offset, band-pass or Strouhal number. 1 = the plane-jet rate; the Goldie style uses 3 (second-octave H2 −29). Off by default for the other presets.
- **The window radiates:** like the holes, the window's outer air mass (half its equivalent radius) is in parallel with ρc/A, which takes energy out (∝ f²), in both the time-domain model and `Acoustics`. The effect is small (under 1 dB on the second octave's harmonics; the tuning moves by well under 2 cents).
- **Fullness of the low notes (the jet):** real high-D whistles' bottom D is their softest note yet the richest (H2 and H3 within 2–4 dB of the fundamental, `host/tools/Real-examples`): a softly blown jet swings far across the edge. Three Voicing settings shape that, all off by default (as before) until tried by ear:
  - **Jet band-pass stages** (2 before): the jet's preference for its Strouhal band is a band-pass whose phase shift adds to the jet's travel time. Two stages narrow each register's pressure range and leave silent pressures between registers (the bottom D sounded only from ~350 to ~650 Pa, then nothing until the octave); one stage removes the gaps and lets notes speak softly, at the cost of a faint upper-register squeak at very soft breath (< 200 Pa) on the upper notes.
  - **Jet gain** multiplies the jet's growth across the window, e^(0.38 W/h): higher swings the jet further, for a squarer, richer tone.
  - **Slow-jet growth** scales that growth's exponent as (U_ref / U)^n, U_ref being the speed whose band centre is the lowest note (thin-jet theory: n = 2/3). A fast jet grows less at every frequency (a purer second octave, as recorded); a slow one more, but only for waves at and below its band centre (boosting all frequencies made soft breath squeak).
  - One stage, gain 1.3, slow-jet growth 0.67 and edge offset 0.2 mm bring the wood preset's octave averages to H2/H3/H4/H5 −15/−15/−30/−34 dB (recorded −12/−15/−26/−23) and its second octave's centroid to 1.03 × f0 (recorded 1.01, before 1.14). `WhistleCheck inside [preset] [fingering] [edge= vortex= gain= slow= stages=]` shows the jet's swing over the edge and the harmonics of the jet flow, window flow and sound at a range of pressures; `analyse`, `scale` and `breath` take the same options.
- **Note changes:** `WhistleCheck transition [preset] [p=<Pa>]` switches one hole within a sensor frame and tracks the pitch. It reaches the new note within 2–11 ms (a few periods). Measured flute transitions take ~10–40 ms, mostly the finger's movement while the hole is partly open; with WARBL that comes from the hole curve on the Sensors tab, so a less steep curve gives smoother changes.
- **Finger gap damping** (Voicing; 0 = off, the default; 1 = physical): the open part of a partly covered hole is taken as a finger hovering at the matching height, so air leaving through the thin gap between finger and rim meets the viscous resistance of a slit (12μℓ/(w·d³)): huge when the gap is a fraction of a millimetre, negligible once the hole is more than about a quarter open. At 1 a finger just above a hole leaks almost nothing (10 % open: +3 cents), as on a real whistle, but in playing that felt sluggish on top of the hole curve, so it is off by default. Fully open and closed holes are unchanged either way. `WhistleCheck plops [preset] [p=<Pa>]` checks quick hole changes for bursts in 0.8–6 kHz, with and without it (none either way at 700 Pa).
- **Chirps in fast note changes:** `WhistleCheck glide [preset] [p=<Pa>] [ms=<ramp>] [vortex=]` changes the fingering gradually and reports level dips and periods that belong to neither note. Well inside a register (D whistle at 600–700 Pa) changes are clean. Near the lower note's octave break (low D at ~900 Pa) a fast change down to D or E briefly kicks the oscillation into the other register (a chirp, up to −10 dB dips), as on a real whistle blown too hard for low D.
- **Stream rate:** 500 frames/s (the firmware's 2 ms loop). Each frame goes to TinyUSB in one write, so it leaves in one USB transfer; written byte by byte (the MIDI library's way) its tail waited for the host's next poll, 1 ms later. A 1 kHz loop was tried: the tone-hole co-processor needs ~0.9 ms per reading, so ~12 % of transfers failed and some garbled ones passed its 4-bit checksum (clicks, only partly caught by a plausibility check, which stays as a safety net). 1 kHz needs a busy flag and a stronger checksum in the ATmega firmware first. The Monitor shows the share of frames with a new hole reading and the garbled transfers rejected.
- **Sample rates:** the model runs at ~96 kHz internally: twice the device rate below 88.2 kHz (decimated with a 63-tap filter at 0.45 × the device rate), the device rate itself from 88.2 kHz up. `WhistleCheck rates [preset]` plays one note at 44.1, 48 and 96 kHz: same pitch and harmonics within 1–2 dB, no measurable aliasing. At 96 kHz a 64-sample buffer is 0.67 ms instead of 1.33 ms.
- **Passive bore check:** `WhistleCheck passive [preset]` plays an impulse into the bore with the jet off and compares its resonances (frequency, Q, height) with Acoustics. They agree within ~1–4 cents and ~10 % in Q.
- **Jet:** the jet speed from the windway. The sound in the window deflects the jet, and the jet takes a pressure-dependent time to reach the edge, where it is split into and out of the pipe (jet-drive model). The jet amplifies a band of frequencies that moves up as you blow harder, so the whistle overblows to the second register.
- **Two sources:** the window and the tube (tone holes and foot) radiate separately and are placed apart: **Spread** in the Monitor tab's reverb row (default 30°, 0 = one source) is the angle between them as you hear them, window to the left, tube to the right. Each reaches the two ears with its own small delay and level difference (spherical head) and gets its own room response from its own place in the room (generated with independent diffuse tails). An IR file has one measured position, so both sources share it. Two convolutions cost a few % more CPU.
- **Attack chiff:** after a quick (tongued) attack the jet has just started: it rolls up a starting vortex, is turbulent, and the sound in the pipe doesn't steer it yet. **Attack chiff** (Voicing; 0 = off, the default) raises the breath noise by that factor times how much the jet speed is above its ~20 ms average (high after a tongued attack, low in a slow swell) times how little the jet already swings across the edge, times how turbulent a jet of that speed can be (none below a Reynolds number U·h/ν of 1000, ~200 Pa with a 1 mm windway; fully above 3000, ~1100 Pa), so gentle attacks start clean. At 3, the noise in the first ~20 ms of a tongued attack rises by ~2–3 dB at 300 Pa, ~7 dB at 700 Pa and ~9 dB at 1200 Pa; slow swells change by 1–3 dB, held notes not at all. `WhistleCheck chiff [preset] [p=<Pa>] [wav=<prefix>]` measures it and saves tongued phrases.
- **Breath noise:** turbulence in a band centred on ~0.08 × jet speed ÷ windway height (about 2–3 kHz when playing), so harder blowing sounds a little brighter. At the edge it is a pressure fluctuation of order ρU², growing as U^2.5, fed into the bore at the window like the tone. (The edge-dipole law, U³, let the hiss overtake the top notes.) The bore's resonances colour it: below the playing threshold you get an airy note at the fingered pitch, peaking at the fingering's resonances. The edge noise comes in bursts as the jet sweeps across the edge ("pulsed noise", locked to the tone), so the ear hears it as breathiness in the note rather than as a separate hiss. Below the threshold the jet hardly moves and the airy noise is steady. Optionally part of it escapes straight from the window ("Breath noise heard directly", off by default). A smaller part makes the jet itself slightly unsteady.

The breath pressure from WARBL is not smoothed. The stream sends the raw ADC reading (8× hardware oversampling only), and between frames the model ramps linearly over the frame interval (measured from WARBL's timestamps). The player's slow breath fluctuations (tremor, wavering, vibrato) reach the model as they are. The fast turbulence, above the 500 Hz that a 1000 frames/s stream can carry, is what the model's breath noise supplies. While playing, the noise between the harmonics sits ~42–54 dB below the tone at the default setting. `WhistleCheck noise [preset]` measures this and can save a crescendo through the airy note into the tone.
- **Sample rate:** the model runs at twice the audio rate.

Settings, all live:

- **Bore:** length, diameter at the top and at the foot (cylindrical or tapered), wall thickness (sets the chimney height of the tone holes) and wall roughness. Choosing a material sets the last two.
- **Head section:** a part of the bore from the window down with its own diameter, widening (or narrowing) to the bore over its length, like the narrower head of Goldie whistles. Narrowing the bore near the window lowers the low register more than the second, which widens the octaves. In the model, a head 30 mm long and 10.5 mm at the window (12 mm bore) brings the played second register from 7–28 cents flat to close to in tune. Narrower or longer heads overshoot. Compare designs with `WhistleCheck head [preset]` (tuning) and `WhistleCheck chart [preset] headlength=<mm> headdiameter=<mm>` (played). The "D whistle, narrow head" preset uses 30 mm / 10.5 mm.

The **Härjedalspipa** preset is a Swedish wooden duct flute with a cylindrical bore and six finger holes (on L1–R3 or L2–R4), in its traditional scale on A#4: A#, C−20, D+35, D#, F, G−20, A (cents from equal temperament), with its own fingering chart ("Härjedalspipa (traditional scale)"; chart notes can carry cents). Its dimensions are typical values (bore 335 mm × 13 mm, 4 mm walls), not a measured instrument. D+35 and D# are only 65 cents apart, so R1 is small (4 mm) and R2 large (10 mm) to keep those fingers ~20 mm apart; the tuner placed the holes.

The **D whistle, Goldie style** preset is fitted to two recordings of the user's Goldie high D, a scale (`Real-examples/High-D-Goldie.wav`, laptop microphone) and a tune (`High-D-Goldie-tune.wav`, phone), which differ by 8–10 dB in breath noise and first-octave H2 (microphone, room, playing), so neither is a fixed target. Not measured from a Goldie: a 12 mm aluminium tube (1 mm wall) with a narrow head (11.5 mm over the top 30 mm), a low windway ceiling (0.87 mm, Goldie's "medium" per forum posts), a short window (4.2 × 8 mm) and the edge 0.15 mm into the pipe. Voicing: Strouhal 0.017, one band-pass stage, slow-jet growth 0.25, jet gain 1.1 (1.3 chirped through another register while a hole opened gradually: `WhistleCheck glide`), jet spreading 3 and vortex loss 0.6. Against the tune: first octave H2/H3 −23/−20 dB (tune −21/−18), second octave −29/−28 (−34/−26) and 5.8 dB louder than the first (5.6); breath noise −67/−61 dB (−59/−58, including the room). The narrower 10.5 mm head of the narrow-head preset played the second octave 20–30 cents sharp here; at 11.5 mm it is in tune when blown gently and goes sharp when pushed, as the recorded Goldie (+15 to +21 cents).

The **low D whistle** preset follows forum measurements of real low whistles: a 22 mm cylindrical bore (a Goldie low D ~21.8 mm), a 1.0 mm windway (Goldie medium blowers 0.9–1.0 mm), and a 13 mm wide, short window (5.2 mm, Overton style). The old preset's thick jet (1.6 mm windway, 9 × 15 mm window) made the pitch follow the breath steeply (up to 140 cents across a note) and its B barely spoke. With a longer window the jet grows so much that quick fingering changes flip into the octave on the way: `WhistleCheck transition "low D" p=<Pa>` switches each hole at once and counts those jumps (it was 9 of 14 at 300 Pa; now 0–1, settling in ~15 ms). A slightly wider head (24 mm over the top 60 mm) puts the second octave in tune (the plain bore played it 10–18 cents sharp), and because an in-tune octave is also easier to fall into, the jet gain is 0.75. Its voicing: jet band Strouhal 0.0095 (low notes ~170–670 Pa, the octave from ~670 Pa), one band-pass stage (the pitch follows the breath half as much as with two: 30–60 cents across a note) and edge vortex loss 0.4 for fuller low notes. Against `Real-examples/Low-D-Goldie.wav` the second octave matches (H2/H3 −26/−29 against −25/−29); the first octave is rounder (H3 −22 against −11), though a laptop microphone also under-records a low D's fundamentals.

The **soprano recorder in C** preset uses baroque fingering, all eight holes, a reverse-conical bore (13 mm to 9.5 mm) and 3.5 mm walls. Its window is a measured soprano's, 3.7 mm long and 8.8 mm wide (NMMSD), over a 1.0 mm windway; the rest are typical values, not a measured instrument. The window is short for the jet's thickness (3.7, a whistle's ~5), so the jet grows less on its way to the edge and switches less fully: a purer tone than a whistle's, and the upper notes speak at lower breath. The edge sits nearly on the jet's centre line (0.05 mm), so even harmonics are weak, as on baroque recorders, and the breath noise is half the whistles'. Across the scale the harmonics average H2 −29, H3 −24, H4 −42 dB in the first octave (spectral centroid 1.03 × f0). The chart follows Dolmetsch's baroque chart for recorders in C, up to top D, including the second- and third-octave forks; top C# closes the foot with the bell sensor. Following the maker's rule that a hole's position mostly sets its low note and its size its upper note (Philippe Bolton), the tuner sets R1's size by the F# fork, L3's by high A, the thumb hole's by high Bb and R3's by high B, all solved together (the sizes interact: the thumb hole vents every pinched note). L2's size (5 mm) puts the Bb fork in tune. The geometry then predicts the first octave and high A, Bb and B in tune, the other second-octave notes within −21 to +16 cents, and the third octave flat: top C −17, C# −25, D −37. Top C can't be tuned with hole sizes: it only adds L2 to high B's fingering, and with L2's position held by low B no hole size moves it apart from high B (`WhistleCheck grid recorder 6 7 "high B" "top C"`). As on a real recorder, open the thumb a little wider for it. The thumb's opening matters a lot for every pinched note: the chart assumes 25 % open, and each 15 % more makes high A, Bb and B about 23 cents sharper (`WhistleCheck notes recorder <opening>`); the Instrument tab's drawing shows how open WARBL reads your thumb. Each preset has its own voicing. The recorder's jet band is higher than the whistles' (Strouhal 0.03) and has one band-pass stage, so there are no silent pressures between its registers; with slow-jet growth (0.67) its low notes speak from the softest breath with no silent gap before the octave (low C over 107–360 Pa) and its second octave is purer. It plays its scale in tune from ~170 Pa (low C) through ~0.5 kPa (C6) to ~3.7 kPa (top C): about 0.4–0.6 of a whistle's pressures in the first two octaves. A recorder is blown much more gently than a whistle, but WARBL's mouthpiece feels the same for both, so each instrument has a **Breath scale** (Voicing; the share of WARBL's breath response it gets): 0.7 for the recorder, so the low notes come at a gentle breath (with the whistle settings they overblew, or fell into breath noise, at a normal breath) and top C is still in reach. Only WARBL's breath is scaled, not Play scale or the screen; the Sensors tab's readout shows the result. `WhistleCheck notes recorder <thumb opening>` shows the top notes for a given thumb opening.

The thumb's opening is not fixed. When playing, it comes continuously from WARBL's thumb sensor (through the hole response curve), and how far the player opens it changes the upper notes as on a real recorder. The chart's "p" (pinched = 25 % open) is only the opening the tuning table, tuner and checks assume. In the simulation every chart note plays, including the forks and the upper octave. Low C still starts sharp (+58 cents at its threshold), as the lowest note of every preset does to a lesser degree.

The presets store their tuned dimensions: the tuner is too slow to run at startup. `WhistleCheck code` retunes them and prints the numbers for `WhistleGeometry.cpp`, and the `PresetTuningTest` test fails if they drift out of tune.
- **Tone holes:** up to eight, one per WARBL finger sensor: LT (left thumb, on the back), L1–L3 and R1–R4. Each has a position, a diameter and a switch. An unticked hole is plugged, as if it weren't drilled, and its sensor is ignored; a whistle uses L1–R3. **Bell sensor closes the foot** lets the bell sensor cover the open end (a recorder technique).
- **Sensors for a whistle:** every whistle preset comes twice, "…, holes L1-R3" and "…, holes L2-R4" (played with the lower six fingers). They are the same instrument: its holes keep their positions and sizes, only the WARBL sensors that play them differ. The whistle chart is relative (its six fingers are the holes in use, from the top), so the tuning table and tuner work either way. The built-in simulator always plays on L1–R3.
- **Fingering chart:** whistle, or baroque recorder (with the forked F, F# and Bb, and the second register by pinching the thumb hole). The chart is used by the tuning table, the tuner and the checks, never by the sound.
- **Fipple:** windway height (jet thickness), windway length, window length (flue exit to edge), window width, and edge offset.
- **Voicing:** the jet's band (Strouhal number), how fast disturbances travel along the jet, and breath noise.
- **Output level.**

## Inside the model (Model tab)

A live look at what the model does, in the order the air goes: breath, windway, jet, edge, the sound source at the window, the bore and the sound out. Each block shows its live values and names the settings that act there.

- **The jet crossing the window (slow motion):** the jet's centre line and width from the flue to the edge, drawn from the last few milliseconds of the simulation at ~0.4 cycles per second. Disturbances leave the flue small, pushed by the sound's flow through the window (orange arrow), grow on the way, and decide at the edge how much of the jet goes into the pipe.
- **At the edge, over two periods:** the jet's centre line relative to the edge (in half-widths; beyond the shaded band it goes all one way), the jet's flow into the pipe (square when it swings far) and the sound's flow through the window. The thin line is the moment shown in slow motion.
- **Which resonance sounds:** the bore's resonances for the current fingering (admittance at the window, from `Acoustics`) and the jet's amplification against frequency at the current breath (its band-pass stages and the slow jet's extra growth). The played note and its harmonics are marked.
- **Harmonics of the sound,** H2–H10 in dB below the fundamental, next to the values measured on real whistles.
- **The air column:** the pressure's size along the bore (nodes at open holes and the foot) and its swing.

The model only records this while the tab is visible. `--model` opens the tab (with `--snapshot`, `--play` plays the scale for something to see).

## Saving

- **The app remembers its state between runs.** It saves the current instrument, the sensor response, the reverb (room or IR file, level, pre-delay), the audio device and buffer size, and the open tab to `~/Library/Application Support/WARBL Stream/WARBL Stream.settings`.
- **Reference pitch:** **A =** on the tuning row (440 Hz by default; e.g. 442, or 415 for baroque pitch) is part of the instrument and is saved with it. The tuner, the tuning table, the key shown and the test scale aim for it. Press **Tune holes** after changing it.
- **Tuning failures:** when moving holes can't put every note in tune at their sizes (a hole would have to sit past the one below it, or above the top of its range), **Tune holes** also changes the sizes of the holes that are stuck, as a maker would (smaller holes sit higher for the same note, larger lower), and lists them. When tuning to a new key it first scales the hole positions with the bore. `WhistleCheck fit` tunes every preset to every key: all succeed, a few only with resizing.
- **Your presets:** **Save preset...** on the Instrument tab stores the current whistle (geometry and voicing, not the Sensors settings) under a name; it then appears in the preset list under **My presets**, after the built-in ones. Saving under an existing name of yours updates it; **Delete** (with confirmation) moves the selected preset of yours to the Trash and loads the next one of yours (or the first built-in preset if none are left). They are `.whistle` files in `~/Library/Application Support/WARBL Stream/Presets/`.
- **Instrument files:** **Export...** and **Import...** write and read whistle designs as `.whistle` files anywhere. These are JSON, with lengths in millimetres, so you can read and edit them by hand. Fields left out get the default whistle's values. A loaded design shows its tuning in the key it is in.
- **Sensor curves aren't in instrument files.** They describe the player and their WARBL rather than the instrument.

## Sensor response (Sensors tab)

How the readings become the model's controls, with live dots on each curve. Curves use the WARBL Config Tool's 0–127 scale: 64 is linear, lower is logarithmic (more response at the start), higher is exponential.

- **Thumb hole (recorder pinch)** (Sensors tab, on by default): a recorder's pinch slides the thumb so it uncovers a strip of the hole at the rim while the thumb stays over the sensor, so WARBL still reads the hole as mostly covered (about 0.5–0.8). The fingers' response, especially the physical finger-height one, hears that as nearly closed, so the high notes came out flat unless the thumb was opened almost fully. With its own response the thumb's open area follows the coverage in a straight line (0.75 covered = 25 % open, what the recorder chart is tuned for), with its own open-below, closed-at and curve. The line under the settings shows the thumb's coverage and openness live. Each 15 % more opening makes high A, Bb and B about 23 cents sharper.
- **Tilt aims the jet** (Sensors tab, off by default): WARBL's elevation, from the accelerometer (the angle of its long axis to the horizontal, smoothed over ~100 ms, unaffected by rolling it about its axis), shifts where the jet meets the edge, as if the voicing were slightly flexible. A real fipple flute is rigid, so this is an extra control; the model still decides what it does. Into the pipe the tone gets fuller (D whistle at 900 Pa: +0.1 mm raises the 2nd harmonic by 4-6 dB), out of it hollower (−0.15 mm lowers it ~6 dB); a little pitch and the register breaks move too. Hold WARBL as you play and press **Set zero now**; then a dead zone (3°), and a straight line to the maximum shift (0.1 mm) at the range (30°). **Invert** swaps the direction.
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

## Bluetooth

The stream also works over Bluetooth MIDI (firmware from 2026-11-05). Connect WARBL as a Bluetooth MIDI device (macOS: Audio MIDI Setup → MIDI Studio → Bluetooth), unplug the USB cable (a USB host always wins), and tick **Stream**: the app sends its commands to every WARBL port, and WARBL streams back over the transport the command came on.

Over Bluetooth, WARBL can only send at each connection event, every 7.5–15 ms (WARBL asks for 7.5; macOS usually grants 11.25 or 15). So it collects the samples of one interval (every 2 ms) and sends them as one **batch**: the first sequence number and the first and last timestamps, the count, one accelerometer reading (top 8 bits per axis, enough for tilt) and flags, then per sample the 14-bit pressure and the nine holes with 8 bits (no gyro). Eight samples are about 119 bytes as SysEx, so a batch fits one BLE packet. Adafruit's BLE MIDI service sends SysEx three bytes per packet, so the firmware carries a copy of it (`warbl2_firmware/WarblBLEMidi.*`, BSD licence, same UUIDs) with one addition that sends a whole SysEx in as few packets as the connection's MTU allows.

**Breath resolution (stream protocol 2):** WARBL reads its pressure sensor at 14 bits for the stream (~0.42 Pa per count; the SAADC already averages 8 samples) and gives its own MIDI logic the same 12-bit reading as before, so WARBL's MIDI behaviour is unchanged. The ambient pressure at startup (the host's breath zero) also comes at 14 bits; with protocol 1 it was the firmware's 10-bit value, which could put every breath reading off by up to ~7 Pa, a lot for a recorder's softest notes (~50 Pa measured). The sensor's own noise is ~1–1.5 counts at 12 bits (a recording at rest: 1.6), so 14 bits mostly resolve that noise rather than reveal a quieter signal; `WhistleCheck jitter recorder 0 p=170` shows that breath noise of the measured size (~3 Pa at the model) adds under 1 dB of noise between the harmonics. The app decodes both protocol versions (pressures in 14-bit units either way), sends its commands as version 1, which every firmware accepts, and records the pressure column as `pressure14`.

The transport adds about one connection interval, as for any Bluetooth MIDI. Frames then arrive in bursts, and **Smooth Bluetooth** (Monitor tab, on by default) plays them with their own 2 ms spacing, delayed just enough to absorb the bursts (about one more interval, shown as "smoothing +… ms"); off, each burst is applied as it arrives (lower delay, steppier breath and fingers). The Monitor shows the frames per batch and the batch interval. **as Bluetooth** makes the built-in simulator send batches every 15 ms through the same encoder and decoder (`--bluetooth` on the command line).

## Comparing with real whistles

`WhistleCheck analyse <file.wav> [<preset> [edge=<mm>] [vortex=] [st=] [noise=]]` splits a recorded scale into its notes and measures each one: pitch and its wobble, level, attack, spectral centroid, harmonics 2-10 relative to the fundamental, and the noise floor between the harmonics in four bands; then averages per octave. With a preset, the model plays the same scale (D up to B in the second octave, at the pressures where it is in tune) and its sound goes through the same analysis. Recordings are in `tools/Real-examples/` (laptop microphone, so trust patterns more than absolute numbers, especially the noise).

Findings (2026-11-06, Goldie and Löfberg high D, Goldie low D):
- **Low octave: the model was thin.** The real whistles' second harmonic averages −12 to −16 dB re the fundamental (the bottom D's −2 to −3 dB), the model's −21 dB, with odd harmonics dominating (hollow). An edge offset of 0.2 mm instead of 0.1 gives −15 dB with the other harmonics still matching; every scale note still plays, the octave breaks move up ~5 %, and a short unsettled zone appears just below each break.
- **Second octave: the model was bright at its in-tune pressures** (H2/H3 about −22/−16 dB against the real −31/−31), but just above its break it is as pure as the real whistles (D at 1300 Pa: −32/−31); its odd harmonics grow quickly with breath. The model's second octave is flat when blown gently and needs more breath to reach pitch, where the real Goldie played it pure and 12-21 cents sharp. Raising the second octave's gentle-breath pitch (the jet's phase, or the octave's tuning) would let it be played purer.
- **Breath noise:** the model's is up to 5-10 dB above the recordings between the harmonics (weak evidence: the laptop microphone may suppress noise).

## Recording a session

**Record** on the Monitor tab (or `--record` on the command line) saves what you play into `~/Downloads/WARBL Stream recordings/`: the sound as you hear it (stereo WAV) and every sensor frame (CSV: device and arrival time, sequence number, the nine raw hole readings, raw pressure, flags), with the instrument, the Sensors settings and WARBL's calibration in the CSV's header. That is enough to replay a performance through the model offline and find out what made a glitch. Press it again to stop; the files are named by date and time.

`WhistleCheck replay <recording.csv> [from=<s>] [to=<s>] [st=] [outmax=<Pa>] [wav=<prefix>]` feeds the recorded frames through the same sensor mapping and model as the app, at their recorded spacing, and prints a timeline around every fingering change: model breath, which holes are covered (x), open (o) or in between (h), the pitch from the last 15 ms and the level. `st=` and `outmax=` try another jet Strouhal number or breath output maximum on the same playing. Found this way (2026-11-04): "plops" in a quick A-D-A at ~1300 Pa were low D cracking to high D (its octave break is ~950 Pa at Strouhal 0.020) and then sliding up through the octave as the fingers lifted; at Strouhal 0.014-0.016 the same playing stays in the low register.

## Reading the numbers

- **frames/s**: should be ~500 with the current firmware (2 ms loop). **lost** counts gaps in the sequence numbers and should stay 0.
- **Arrival jitter**: how much later than the fastest recent frame each frame reached the app. This is the part of the transport delay that varies. The constant part can't be measured from the stream alone, because the WARBL and Mac clocks aren't synchronised.
- **output latency**: audio buffer plus the delay the output device reports. On the Mac's built-in headphone output this is ~6 ms even with a 64-sample buffer. A good USB audio interface is usually lower. Use **Audio settings...** to choose the device and buffer size.
- **max frames/block**: the most frames handled in one audio block. A large number means frames are bunching up.
