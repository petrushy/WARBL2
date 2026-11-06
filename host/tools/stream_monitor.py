#!/usr/bin/env python3
"""Turn on the WARBL2 sensor stream over USB MIDI and print the decoded frames.

A quick hardware check for the sensor stream (see warbl2_firmware/Sensor_stream.ino)
before the host app exists. Needs: pip install mido python-rtmidi

    python3 stream_monitor.py              # stream with normal MIDI output still on
    python3 stream_monitor.py --suppress   # stream with note/pitchbend/CC output off
"""

import argparse
import sys
import time

import mido

ID = [0x7D, 0x57]
COMMAND_VERSION = 1  # Commands are unchanged since version 1, which every firmware accepts
FRAME, INFO, CONFIG, REQUEST_INFO = 0x01, 0x10, 0x20, 0x21


def frame_fields(version):
    """Version 2 sends the pressure at 14 bits, version 1 at 12."""
    return [("seq", 14), ("time", 21)] + [(f"hole{i}", 10) for i in range(9)] + [
        ("pressure", 14 if version >= 2 else 12),
        ("ax", 16), ("ay", 16), ("az", 16),
        ("gx", 16), ("gy", 16), ("gz", 16),
        ("covered", 9), ("flags", 7),
    ]
SIGNED = {"ax", "ay", "az", "gx", "gy", "gz"}


def unpack(data, widths):
    """Read MSB-first values from 7-bit SysEx data bytes."""
    values, pos = [], 0
    for bits in widths:
        v = 0
        for _ in range(bits):
            v = (v << 1) | ((data[pos // 7] >> (6 - pos % 7)) & 1)
            pos += 1
        values.append(v)
    return values


def decode_frame(data, version):
    fields = frame_fields(version)
    frame = dict(zip([n for n, _ in fields], unpack(data, [w for _, w in fields])))
    if version < 2:
        frame["pressure"] <<= 2  # Shown at 14 bits either way
    for name in SIGNED:
        if frame[name] >= 0x8000:
            frame[name] -= 0x10000
    return frame


def find_warbl(names):
    for name in names:
        if "WARBL" in name.upper():
            return name
    sys.exit(f"No WARBL MIDI port found. Ports: {names}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suppress", action="store_true", help="turn off note/pitchbend/CC output while streaming")
    args = parser.parse_args()

    out = mido.open_output(find_warbl(mido.get_output_names()))
    inp = mido.open_input(find_warbl(mido.get_input_names()))

    def send(type_, payload=()):
        out.send(mido.Message("sysex", data=ID + [COMMAND_VERSION, type_] + list(payload)))

    send(REQUEST_INFO)
    send(CONFIG, [0x01 | (0x02 if args.suppress else 0)])

    last_seq, frames, gaps, start = None, 0, 0, time.monotonic()
    try:
        for msg in inp:
            if msg.type != "sysex" or list(msg.data[:2]) != ID or msg.data[2] not in (1, 2):
                continue
            version, kind, payload = msg.data[2], msg.data[3], msg.data[4:]
            if kind == INFO:
                v = unpack(payload, [16] * (24 if version >= 2 else 23))
                ambient = v[23] if version >= 2 else v[19] << 4
                print(f"protocol {version}  firmware {v[0]}  baselines {v[1:10]}  covered {v[10:19]}  ambient {ambient} (14-bit)  "
                      f"accel range {v[20]}  gyro range {v[21]}  loop {v[22]} ms")
            elif kind == FRAME:
                f = decode_frame(payload, version)
                if last_seq is not None and f["seq"] != (last_seq + 1) & 0x3FFF:
                    gaps += 1
                last_seq = f["seq"]
                frames += 1
                if frames % 50 == 0:  # Print about 10 times a second.
                    rate = frames / (time.monotonic() - start)
                    holes = " ".join(f"{f[f'hole{i}']:4d}" for i in range(9))
                    print(f"{rate:6.0f} fps  gaps {gaps}  p {f['pressure']:5d}  holes {holes}  "
                          f"covered {f['covered']:09b}  acc {f['ax']:6d} {f['ay']:6d} {f['az']:6d}  "
                          f"gyro {f['gx']:6d} {f['gy']:6d} {f['gz']:6d}")
    except KeyboardInterrupt:
        pass
    finally:
        send(CONFIG, [0])  # Stream off, normal MIDI output back on.


if __name__ == "__main__":
    main()
