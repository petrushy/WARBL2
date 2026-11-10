# The sensor stream

This fork's firmware can send WARBL's raw sensor readings to a computer, instead of (or as well as) MIDI notes. A program on the computer can then make the sound itself. [WhistleSim](https://github.com/petrushy/whistlesim) does this: it plays a physical model of a whistle or recorder from the stream.

The stream is off until a host program turns it on, and it isn't saved: WARBL plays as usual otherwise. The branch also has the fork's other changes: natural vibrato, the lower holes option and the Bluetooth toggle.

## Protocol

Everything is MIDI System Exclusive: `F0 7D 57 <protocol version> <type> <payload> F7` (7D is the non-commercial manufacturer ID, 57 `W` marks WARBL). Payloads are bit-packed, most significant bit first, 7 bits per SysEx data byte.

| Type | Direction | What it carries |
|---|---|---|
| `0x20` STREAM_CONFIG | host → WARBL | Stream on, suppress the normal MIDI output, keep-alive. The host repeats it every second; WARBL stops the stream 3 s after the last one. |
| `0x21` STREAM_REQUEST_INFO | host → WARBL | Asks for STREAM_INFO. |
| `0x01` STREAM_FRAME | WARBL → host | Over USB, one per 2 ms loop: sequence number, timestamp, nine 10-bit tone-hole readings, 14-bit pressure, accelerometer, gyro, WARBL's own covered/uncovered decision, flags. |
| `0x02` STREAM_BATCH | WARBL → host | Over Bluetooth, once per connection interval: the samples of that interval (pressure and 8-bit holes), sized to fit one BLE packet. |
| `0x10` STREAM_INFO | WARBL → host | Firmware version, tone-hole baselines and thresholds, ambient pressure, sensor ranges, loop period. |

The field-by-field layout is in the comment at the top of [`warbl2_firmware/Sensor_stream.ino`](warbl2_firmware/Sensor_stream.ino), and WhistleSim's `Source/StreamProtocol.h` is a decoder and encoder for it with no dependencies. Frames are protocol version 2 (14-bit pressure, since 2026-11-06); host commands are still version 1. A change to the layout bumps the version, and hosts keep decoding the older ones.

## Building and flashing

The firmware is built with **arduino-cli** (or the Arduino IDE, see the repository's main README) and the Adafruit nRF52 board package, with WARBL's own board definition. These steps were used for this fork, on macOS, on the `sensor-stream` branch; paths for Linux and Windows are given where they differ.

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
8. **Check:** start WhistleSim, tick **Stream** and press **Request info**. The Monitor tab should show "loop 2 ms", about 500 frames/s, "new hole readings in ~100 % of frames" and no lost frames.

The tone-hole co-processor (`ATmega_firmware/`) is unchanged in this fork: WARBL only reprograms it when `ATMEGA_FIRMWARE_VERSION` in `Defines.h` changes, so leave that alone unless you change the ATmega code. To go back to the official firmware, flash a release `.uf2` from warbl.xyz the same way.
