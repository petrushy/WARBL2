// A scripted player for working without the instrument: plays a D-whistle scale with breath swells
// and finger movements, as sensor frames. Used by the app's "Simulate WARBL" switch and by WarblSimulator.

#pragma once

#include "StreamProtocol.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace warbl
{

class SimulatedPlayer
{
public:
    static constexpr uint16_t kBaseline = 40;
    static constexpr uint16_t kCovered = 700;
    static constexpr uint16_t kAmbient14 = 4160;
    static constexpr double kNoteSeconds = 1.2;

    static DeviceInfo info()
    {
        DeviceInfo i;
        i.firmwareVersion = 47;
        i.baseline.fill (kBaseline);
        i.covered.fill (kCovered);
        i.ambientPressure = kAmbient14;
        i.accelRange = 2;      // 4 g
        i.gyroRange = 0b1100;  // 2000 dps
        i.loopPeriodMs = 2;
        return i;
    }

    // The frame at time t (seconds since start).
    SensorFrame frame (double t)
    {
        constexpr double pi = 3.14159265358979323846;

        // D E F# G A B C#: for note n the top 6 - n finger holes (L1 downwards) are closed.
        const int note = (int) (t / kNoteSeconds) % 7;
        const double phase = std::fmod (t, kNoteSeconds) / kNoteSeconds;
        const double breath = phase < 0.85 ? std::sin (phase / 0.85 * pi) : 0.0;

        SensorFrame f;
        f.seq = (uint16_t) (seq++ & ((1u << kSeqBits) - 1));
        f.timestamp = (uint32_t) (t * kDeviceTicksPerSecond) & ((1u << kTimestampBits) - 1);

        for (int hole = 0; hole < kNumHoles; ++hole)
        {
            // Holes 2..7 are R3..L1; the thumb (8) stays covered, as a recorder player's would.
            const bool closed = (hole >= 2 && hole <= 7 && (7 - hole) < 6 - note) || hole == 8;
            const double level = hole == 8 ? 1.0 : closed ? std::min (1.0, phase * 20.0) : 0.0;  // 60 ms finger movement
            f.holes[(size_t) hole] = (uint16_t) (kBaseline + level * (kCovered - kBaseline) + noise (5));
            if (level > 0.9)
                f.holeCovered |= (uint16_t) (1 << hole);
        }

        // Up to ~830 Pa (2000 14-bit counts at ~0.42 Pa each): the low register of a D whistle.
        f.pressure = (uint16_t) (kAmbient14 + breath * 2000.0 + noise (28));
        f.accel = { (int16_t) (2000 * std::sin (t)), 300, 8000 };
        f.gyro = { (int16_t) (3000 * std::cos (t * 2.0)), 0, 0 };
        f.flags = 1;
        return f;
    }

private:
    int noise (int range) { return (int) (rng() % (uint32_t) range); }

    std::minstd_rand rng { 1 };
    uint32_t seq = 0;
};

} // namespace warbl
