# WARBL2 (fork with sensor streaming)

WARBL2 is a MIDI wind controller. This fork adds a **sensor stream** to the firmware: raw sensor frames over USB MIDI SysEx (protocol and build steps in `SENSOR_STREAM.md`). The app that plays it, **WhistleSim** (a physical model of a whistle, plus room reverb), has its own repository, petrushy/whistlesim, usually checked out at `../whistlesim`; it was split out of this repo's `host/` folder on 2026-11-10. A change to the stream protocol goes in both: bump the version byte, and keep the app decoding older versions.

The upstream WARBL project is amowry/WARBL2. Fork-specific work lives on feature branches (`natural-vibrato`, `sensor-stream`).

## Layout

- `warbl2_firmware/`: main firmware (nRF52840, Arduino). Start with `warbl2_firmware.ino` (globals, `loop()`).
  - `Functions.ino`: nearly everything else.
  - `Defines.h`: settings indices, the MIDI CC protocol, the EEPROM map.
  - `Sensor_stream.ino`: the SysEx sensor stream (USB frames; over BLE, batches per connection interval).
  - `WarblBLEMidi.*`: a renamed copy of Adafruit's BLEMidi (BSD) that can send a whole SysEx per BLE packet.
- `ATmega_firmware/`: the tone-hole sensor co-processor. The nRF reads it over SPI and reflashes it when `ATMEGA_FIRMWARE_VERSION` in `Defines.h` changes.
- `webconfig/`: the browser Config Tool, using Web MIDI. Settings travel as CC 102–106 on channel 7.

## Build and check

Firmware (board definition `adafruit:nrf52:warbl2`; the UTF-8 locale is needed by the nrfutil packaging step):

    LC_ALL=en_US.UTF-8 arduino-cli compile --fqbn adafruit:nrf52:warbl2 warbl2_firmware

## Conventions

- **Line endings:** `Functions.ino`, `Fingering_Charts.ino` and `Battery_management.ino` use CRLF; the other firmware files use LF. Keep each file's endings. Python `open()` in text mode silently converts CRLF to LF; use `newline=''` or check `git diff --stat` afterwards.
- **Config Tool:** after changing `webconfig/js/*.js` or the CSS, regenerate the `.min` files with `webconfig/minifyall.sh` (uglify-js 3.19.3, uglifycss 0.0.29). Bump the cache name in `webconfig/service_worker.js` too.
- **New per-preset firmware settings** follow the natural-vibrato pattern (commits 9fefd8a, 6f9e5a9):
  - Use spare bits or `ED[]` entries, sent with CC 104/105 or 106 values marked unused in `Defines.h`.
  - Defaults must make existing presets behave as before.
  - The Config Tool shows a control only once WARBL reports the setting.
- Match the surrounding style: comments explain why, units are in comments, and variable names are descriptive.
