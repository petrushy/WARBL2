# WARBL2 (fork with sensor streaming and a physical-model host)

WARBL2 is a MIDI wind controller. This fork adds:
- a **sensor stream** in the firmware: raw sensor frames over USB MIDI SysEx;
- **WARBL Stream** (`host/`): a Mac app that turns the stream into sound with a physical model of a whistle, plus room reverb.

The upstream WARBL project is amowry/WARBL2. Fork-specific work lives on feature branches (`natural-vibrato`, `sensor-stream`).

## Layout

- `warbl2_firmware/`: main firmware (nRF52840, Arduino). Start with `warbl2_firmware.ino` (globals, `loop()`).
  - `Functions.ino`: nearly everything else.
  - `Defines.h`: settings indices, the MIDI CC protocol, the EEPROM map.
  - `Sensor_stream.ino`: the SysEx sensor stream.
- `ATmega_firmware/`: the tone-hole sensor co-processor. The nRF reads it over SPI and reflashes it when `ATMEGA_FIRMWARE_VERSION` in `Defines.h` changes.
- `webconfig/`: the browser Config Tool, using Web MIDI. Settings travel as CC 102–106 on channel 7.
- `host/`: the JUCE 9 / CMake Mac app and its tools. See `host/README.md`.
  - `Source/StreamProtocol.h`: protocol decoder and encoder, no JUCE.
  - `Source/Whistle/`: geometry, frequency-domain acoustics (`Acoustics`) and the time-domain model (`WhistleModel`).
  - `Source/*Page`: the Monitor, Instrument and Sensors tabs.
  - `tools/`: lowercase. WarblSimulator, ReverbCheck, WhistleCheck, stream_monitor.py.

## Build and check

Firmware (board definition `adafruit:nrf52:warbl2`; the UTF-8 locale is needed by the nrfutil packaging step):

    LC_ALL=en_US.UTF-8 arduino-cli compile --fqbn adafruit:nrf52:warbl2 warbl2_firmware

Host app:

    cd host && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
    ctest --test-dir build                                         # protocol and instrument-file tests
    build/WhistleCheck_artefacts/Release/WhistleCheck chart 0       # whistle: predicted + played tuning per fingering
    build/ReverbCheck_artefacts/Release/ReverbCheck <dir>          # reverb times / balance vs. targets, WAVs

- The first configure downloads JUCE 9.0.5. LTO is deliberately off, because linking took many minutes.
- To check the app without anyone at the screen, use `"build/WarblStream_artefacts/Release/WARBL Stream.app/Contents/MacOS/WARBL Stream" --simulate --instrument --snapshot out.png`. Other options: `--sensors`, `--play`, `--output <device>`. Read the PNG.
- App state lives in `~/Library/Application Support/WARBL Stream/WARBL Stream.settings`. If a test changes it, put it back afterwards; it's the user's real settings.
- A MacBook with its lid closed runs no audio at all: the app shows "blocks 0", and even `afplay` hangs. That isn't an app bug.

## Conventions

- **Line endings:** `Functions.ino`, `Fingering_Charts.ino` and `Battery_management.ino` use CRLF; the other firmware files use LF. Keep each file's endings. Python `open()` in text mode silently converts CRLF to LF; use `newline=''` or check `git diff --stat` afterwards.
- **Config Tool:** after changing `webconfig/js/*.js` or the CSS, regenerate the `.min` files with `webconfig/minifyall.sh` (uglify-js 3.19.3, uglifycss 0.0.29). Bump the cache name in `webconfig/service_worker.js` too.
- **New per-preset firmware settings** follow the natural-vibrato pattern (commits 9fefd8a, 6f9e5a9):
  - Use spare bits or `ED[]` entries, sent with CC 104/105 or 106 values marked unused in `Defines.h`.
  - Defaults must make existing presets behave as before.
  - The Config Tool shows a control only once WARBL reports the setting.
- **Physical model principle:** sound comes from simulating the instrument. Never map fingerings to notes. Hole sensors give continuous openness, and pitch, half-holing, slides and overblowing must come out of the simulation. `Acoustics` (transfer-matrix method) is only a design aid, for the tuning table and hole placement.
- **Audio-thread code:** no locks (try-lock only), no allocation. Data goes from the MIDI thread to the audio thread through `SpscQueue`, and settings through atomics. Geometry is handed over to the audio thread with a try-locked pending copy.
- **Preset dimensions are tuned offline.** After changing the acoustics or a preset, run `WhistleCheck code` and paste the printed values into `WhistleGeometry.cpp`; `PresetTuningTest` checks them.
- **Holes:** there are eight, one per WARBL finger sensor (LT, L1–L3, R1–R4). Each has an enabled flag, and code must skip plugged holes.
- **Verifying model changes:** run `WhistleCheck` (`play`, `chart`, `slide`) before and after. It measures what the model actually plays.
- Match the surrounding style: comments explain why, units are in comments, and variable names are descriptive.
