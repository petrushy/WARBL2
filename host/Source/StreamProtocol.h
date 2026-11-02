// WARBL2 sensor stream protocol (see warbl2_firmware/Sensor_stream.ino).
// No JUCE dependency, so the same decoder can be used by the test app, a synth plugin, or tests.
//
// SysEx (without F0/F7): 7D 57 <protocol version> <type> <payload>
// Payloads are bit-packed MSB-first, 7 bits per data byte.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace warbl
{

constexpr uint8_t kSysExId1 = 0x7D;
constexpr uint8_t kSysExId2 = 0x57;
constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 4;  // ID1, ID2, version, type

enum MessageType : uint8_t
{
    kFrame = 0x01,
    kInfo = 0x10,
    kConfig = 0x20,
    kRequestInfo = 0x21
};

enum ConfigFlags : uint8_t
{
    kStreamOn = 0x01,
    kSuppressNotes = 0x02
};

constexpr int kNumHoles = 9;
constexpr double kDeviceTicksPerSecond = 32768.0;  // Frame timestamps are RTC2 ticks.
constexpr uint32_t kTimestampBits = 21;            // Wraps every 64 s.
constexpr uint32_t kSeqBits = 14;

// Tone hole order as used by the firmware (holeCovered bit i is hole i).
inline const char* holeName (int index)
{
    static const char* names[kNumHoles] = { "Bell", "R4", "R3", "R2", "R1", "L3", "L2", "L1", "LT" };
    return index >= 0 && index < kNumHoles ? names[index] : "?";
}

struct SensorFrame
{
    uint16_t seq = 0;
    uint32_t timestamp = 0;                 // Device time, 1/32768 s, 21 bits.
    std::array<uint16_t, kNumHoles> holes {};  // 10-bit, ambient-light corrected, unsmoothed, no baseline subtracted.
    uint16_t pressure = 0;                  // 12-bit raw ADC.
    std::array<int16_t, 3> accel {};        // Raw LSM6DSOX counts.
    std::array<int16_t, 3> gyro {};
    uint16_t holeCovered = 0;               // Firmware's covered/uncovered decision (hint).
    uint8_t flags = 0;

    bool holesUpdated() const { return (flags & 0x01) != 0; }
};

struct DeviceInfo
{
    uint16_t firmwareVersion = 0;
    std::array<uint16_t, kNumHoles> baseline {};  // Reading with the hole open.
    std::array<uint16_t, kNumHoles> covered {};   // Reading with the hole covered (calibration).
    uint16_t ambientPressure = 0;                 // 10-bit, as used by the firmware.
    uint16_t accelRange = 0;                      // Adafruit lsm6ds_accel_range_t
    uint16_t gyroRange = 0;                       // Adafruit lsm6ds_gyro_range_t
    uint16_t loopPeriodMs = 0;

    // 0 = open, 1 = covered as at calibration. Can go a little outside 0..1.
    float holeCoverage (int hole, uint16_t raw) const
    {
        const float span = (float) covered[(size_t) hole] - (float) baseline[(size_t) hole];
        if (span <= 0.0f)
            return 0.0f;
        return ((float) raw - (float) baseline[(size_t) hole]) / span;
    }

    // 12-bit pressure relative to the ambient reading taken at startup.
    int pressureAboveAmbient (uint16_t raw) const { return (int) raw - (int) ambientPressure * 4; }

    // Raw IMU counts to physical units (LSM6DSOX sensitivities, range codes as in Adafruit_LSM6DS.h).
    float accelToG (int16_t raw) const
    {
        switch (accelRange)
        {
            case 0: return raw * 0.061e-3f;  // 2 g
            case 1: return raw * 0.488e-3f;  // 16 g
            case 2: return raw * 0.122e-3f;  // 4 g
            case 3: return raw * 0.244e-3f;  // 8 g
            default: return raw * 0.122e-3f;
        }
    }

    float gyroToDps (int16_t raw) const
    {
        switch (gyroRange)
        {
            case 0b0010: return raw * 0.004375f;  // 125 dps
            case 0b0000: return raw * 0.00875f;   // 250 dps
            case 0b0100: return raw * 0.0175f;    // 500 dps
            case 0b1000: return raw * 0.035f;     // 1000 dps
            case 0b1100: return raw * 0.070f;     // 2000 dps
            default: return raw * 0.070f;
        }
    }
};

class BitReader
{
public:
    BitReader (const uint8_t* bytes, size_t numBytes) : data (bytes), size (numBytes) {}

    bool canRead (size_t bits) const { return (pos + bits + 6) / 7 <= size; }

    uint32_t get (int bits)
    {
        uint32_t value = 0;
        for (int i = 0; i < bits; ++i, ++pos)
            value = (value << 1) | ((data[pos / 7] >> (6 - pos % 7)) & 1u);
        return value;
    }

private:
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
};

// Mirrors SysExBitWriter in the firmware; used for building commands and in tests.
class BitWriter
{
public:
    void put (uint32_t value, int bits)
    {
        for (int b = bits - 1; b >= 0; --b)
        {
            if (bitsInByte == 0)
                bytes.push_back (0);
            bytes.back() |= (uint8_t) (((value >> b) & 1u) << (6 - bitsInByte));
            bitsInByte = (bitsInByte + 1) % 7;
        }
    }

    std::vector<uint8_t> bytes;

private:
    int bitsInByte = 0;
};

constexpr size_t kFrameBits = kSeqBits + kTimestampBits + kNumHoles * 10 + 12 + 6 * 16 + 9 + 7;
constexpr size_t kInfoValues = 1 + kNumHoles * 2 + 1 + 3;

// The message type of a WARBL stream SysEx (data without F0/F7), or nothing if it isn't one.
inline std::optional<uint8_t> messageType (const uint8_t* data, size_t size)
{
    if (size < kHeaderSize || data[0] != kSysExId1 || data[1] != kSysExId2 || data[2] != kProtocolVersion)
        return std::nullopt;
    return data[3];
}

inline std::optional<SensorFrame> decodeFrame (const uint8_t* data, size_t size)
{
    if (messageType (data, size) != kFrame)
        return std::nullopt;

    BitReader r (data + kHeaderSize, size - kHeaderSize);
    if (! r.canRead (kFrameBits))
        return std::nullopt;

    SensorFrame f;
    f.seq = (uint16_t) r.get (kSeqBits);
    f.timestamp = r.get (kTimestampBits);
    for (auto& h : f.holes)
        h = (uint16_t) r.get (10);
    f.pressure = (uint16_t) r.get (12);
    for (auto& a : f.accel)
        a = (int16_t) (uint16_t) r.get (16);
    for (auto& g : f.gyro)
        g = (int16_t) (uint16_t) r.get (16);
    f.holeCovered = (uint16_t) r.get (9);
    f.flags = (uint8_t) r.get (7);
    return f;
}

inline std::optional<DeviceInfo> decodeInfo (const uint8_t* data, size_t size)
{
    if (messageType (data, size) != kInfo)
        return std::nullopt;

    BitReader r (data + kHeaderSize, size - kHeaderSize);
    if (! r.canRead (kInfoValues * 16))
        return std::nullopt;

    DeviceInfo info;
    info.firmwareVersion = (uint16_t) r.get (16);
    for (auto& b : info.baseline)
        b = (uint16_t) r.get (16);
    for (auto& c : info.covered)
        c = (uint16_t) r.get (16);
    info.ambientPressure = (uint16_t) r.get (16);
    info.accelRange = (uint16_t) r.get (16);
    info.gyroRange = (uint16_t) r.get (16);
    info.loopPeriodMs = (uint16_t) r.get (16);
    return info;
}

// Packs a frame as the firmware does (payload only, without the 4-byte header). For the simulator and tests.
inline std::vector<uint8_t> encodeFramePayload (const SensorFrame& f)
{
    BitWriter w;
    w.put (f.seq & ((1u << kSeqBits) - 1), kSeqBits);
    w.put (f.timestamp & ((1u << kTimestampBits) - 1), kTimestampBits);
    for (auto h : f.holes)
        w.put (h & 0x3FFu, 10);
    w.put (f.pressure & 0xFFFu, 12);
    for (auto a : f.accel)
        w.put ((uint16_t) a, 16);
    for (auto g : f.gyro)
        w.put ((uint16_t) g, 16);
    w.put (f.holeCovered & 0x1FFu, 9);
    w.put (f.flags & 0x7Fu, 7);
    return w.bytes;
}

inline std::vector<uint8_t> encodeInfoPayload (const DeviceInfo& info)
{
    BitWriter w;
    w.put (info.firmwareVersion, 16);
    for (auto b : info.baseline)
        w.put (b, 16);
    for (auto c : info.covered)
        w.put (c, 16);
    w.put (info.ambientPressure, 16);
    w.put (info.accelRange, 16);
    w.put (info.gyroRange, 16);
    w.put (info.loopPeriodMs, 16);
    return w.bytes;
}

// Host -> WARBL commands (SysEx data without F0/F7).
inline std::vector<uint8_t> makeConfig (bool streamOn, bool suppressNotes)
{
    return { kSysExId1, kSysExId2, kProtocolVersion, kConfig,
             (uint8_t) ((streamOn ? kStreamOn : 0) | (suppressNotes ? kSuppressNotes : 0)) };
}

inline std::vector<uint8_t> makeRequestInfo()
{
    return { kSysExId1, kSysExId2, kProtocolVersion, kRequestInfo };
}

// Extends wrapping device timestamps and sequence numbers to continuous counters.
class Unwrapper
{
public:
    explicit Unwrapper (uint32_t bits) : mask ((1u << bits) - 1) {}

    int64_t next (uint32_t value)
    {
        if (! started)
        {
            started = true;
            total = value;
        }
        else
        {
            total += (int64_t) ((value - last) & mask);
        }
        last = value;
        return total;
    }

    void reset() { started = false; }

private:
    uint32_t mask;
    uint32_t last = 0;
    int64_t total = 0;
    bool started = false;
};

} // namespace warbl
