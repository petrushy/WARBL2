// Checks the host decoder against the firmware's packing (BitWriter mirrors SysExBitWriter in Sensor_stream.ino).

#include "StreamProtocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace warbl;

static int failures = 0;

#define CHECK(condition)                                                    \
    do                                                                      \
    {                                                                       \
        if (! (condition))                                                  \
        {                                                                   \
            std::printf ("FAILED line %d: %s\n", __LINE__, #condition);     \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

static std::vector<uint8_t> withHeader (uint8_t type, const std::vector<uint8_t>& payload, uint8_t version = kProtocolVersion)
{
    std::vector<uint8_t> data { kSysExId1, kSysExId2, version, type };
    data.insert (data.end(), payload.begin(), payload.end());
    return data;
}

int main()
{
    // A frame packed in the firmware's field order, including extreme and negative values.
    {
        BitWriter w;
        w.put (16383, 14);
        w.put (0x1FFFFF, 21);
        const uint16_t holes[9] = { 0, 1023, 512, 1, 2, 3, 4, 5, 1000 };
        for (auto h : holes)
            w.put (h, 10);
        w.put (16383, 14);
        const int16_t imu[6] = { -2048, 100, -1, 0, 32767, -32768 };
        for (auto v : imu)
            w.put ((uint16_t) v, 16);
        w.put (0x155, 9);
        w.put (1, 7);

        CHECK (w.bytes.size() == 36);  // As in the firmware.
        for (auto b : w.bytes)
            CHECK (b < 0x80);

        const auto data = withHeader (kFrame, w.bytes);
        const auto f = decodeFrame (data.data(), data.size());
        CHECK (f.has_value());
        if (f)
        {
            CHECK (f->seq == 16383);
            CHECK (f->timestamp == 0x1FFFFF);
            for (int i = 0; i < 9; ++i)
                CHECK (f->holes[(size_t) i] == holes[i]);
            CHECK (f->pressure == 16383);
            CHECK (f->accel[0] == -2048 && f->accel[1] == 100 && f->accel[2] == -1);
            CHECK (f->gyro[0] == 0 && f->gyro[1] == 32767 && f->gyro[2] == -32768);
            CHECK (f->holeCovered == 0x155);
            CHECK (f->holesUpdated());
        }

        // Version 1 (12-bit pressure): decoded at 14 bits.
        {
            BitWriter v1;
            v1.put (5, 14);
            v1.put (6, 21);
            for (int i = 0; i < 9; ++i)
                v1.put (100, 10);
            v1.put (1000, 12);
            for (int i = 0; i < 6; ++i)
                v1.put (0, 16);
            v1.put (0, 9);
            v1.put (1, 7);
            const auto old = withHeader (kFrame, v1.bytes, 1);
            const auto f1 = decodeFrame (old.data(), old.size());
            CHECK (f1 && f1->pressure == 4000 && f1->holes[8] == 100 && f1->holesUpdated());
        }

        // Truncated or foreign messages are rejected.
        CHECK (! decodeFrame (data.data(), data.size() - 1).has_value());
        auto foreign = data;
        foreign[1] = 0x00;
        CHECK (! decodeFrame (foreign.data(), foreign.size()).has_value());
    }

    // Random round trips.
    {
        std::srand (1);
        for (int t = 0; t < 10000; ++t)
        {
            SensorFrame in;
            in.seq = (uint16_t) (std::rand() & 0x3FFF);
            in.timestamp = (uint32_t) std::rand() & 0x1FFFFF;
            for (auto& h : in.holes)
                h = (uint16_t) (std::rand() & 0x3FF);
            in.pressure = (uint16_t) (std::rand() & 0x3FFF);
            for (auto& a : in.accel)
                a = (int16_t) std::rand();
            for (auto& g : in.gyro)
                g = (int16_t) std::rand();
            in.holeCovered = (uint16_t) (std::rand() & 0x1FF);
            in.flags = (uint8_t) (std::rand() & 0x7F);

            BitWriter w;
            w.put (in.seq, 14);
            w.put (in.timestamp, 21);
            for (auto h : in.holes)
                w.put (h, 10);
            w.put (in.pressure, 14);
            for (auto a : in.accel)
                w.put ((uint16_t) a, 16);
            for (auto g : in.gyro)
                w.put ((uint16_t) g, 16);
            w.put (in.holeCovered, 9);
            w.put (in.flags, 7);

            const auto data = withHeader (kFrame, w.bytes);
            const auto out = decodeFrame (data.data(), data.size());
            CHECK (out && out->seq == in.seq && out->timestamp == in.timestamp && out->holes == in.holes
                   && out->pressure == in.pressure && out->accel == in.accel && out->gyro == in.gyro
                   && out->holeCovered == in.holeCovered && out->flags == in.flags);
            if (failures > 0)
                break;
        }
    }

    // Info message.
    {
        BitWriter w;
        w.put (47, 16);
        for (int i = 0; i < 9; ++i)
            w.put ((uint32_t) (10 + i), 16);
        for (int i = 0; i < 9; ++i)
            w.put ((uint32_t) (500 + i), 16);
        w.put (260, 16);
        w.put (2, 16);
        w.put (0b1100, 16);
        w.put (2, 16);

        // Version 1: the 10-bit ambient only.
        const auto old = withHeader (kInfo, w.bytes, 1);
        const auto oldInfo = decodeInfo (old.data(), old.size());
        CHECK (oldInfo && oldInfo->ambientPressure == 260 * 16 && oldInfo->loopPeriodMs == 2);

        // Version 2 adds the 14-bit ambient.
        w.put (4170, 16);
        CHECK (w.bytes.size() + kHeaderSize <= 64 - 2);  // Fits the firmware's SysEx buffer.

        const auto data = withHeader (kInfo, w.bytes);
        const auto info = decodeInfo (data.data(), data.size());
        CHECK (info.has_value());
        if (info)
        {
            CHECK (info->firmwareVersion == 47);
            CHECK (info->baseline[8] == 18 && info->covered[0] == 500);
            CHECK (info->ambientPressure == 4170);
            CHECK (info->loopPeriodMs == 2);
            CHECK (info->pressureAboveAmbient (4410) == 240);
            CHECK (info->holeCoverage (0, 10) == 0.0f && info->holeCoverage (0, 500) == 1.0f);
            CHECK (std::abs (info->gyroToDps (1000) - 70.0f) < 0.001f);
            CHECK (std::abs (info->accelToG (8197) - 1.0f) < 0.001f);
        }
    }

    // Commands match what the firmware's handleSysExFromUSB() expects.
    {
        const auto config = makeConfig (true, true);
        CHECK (config.size() == 5 && config[3] == kConfig && config[4] == (kStreamOn | kSuppressNotes | kKeepAlive));
        CHECK (makeConfig (false, false)[4] == 0 && makeConfig (true, false, false)[4] == kStreamOn);
        CHECK (config[2] == 1 && makeRequestInfo()[2] == 1);  // As version 1: every firmware accepts it
        CHECK (makeRequestInfo().size() == 4);
    }

    // The simulator's encoder round-trips through the decoder.
    {
        SensorFrame in;
        in.seq = 123;
        in.timestamp = 4567;
        in.holes = { 1, 2, 3, 4, 5, 6, 7, 8, 1023 };
        in.pressure = 2000;
        in.accel = { -5, 6, -7 };
        in.gyro = { 8, -9, 10 };
        in.holeCovered = 0x1FF;
        in.flags = 1;
        const auto data = withHeader (kFrame, encodeFramePayload (in));
        const auto out = decodeFrame (data.data(), data.size());
        CHECK (out && out->holes == in.holes && out->accel == in.accel && out->gyro == in.gyro && out->seq == 123);

        DeviceInfo info;
        info.firmwareVersion = 47;
        info.covered.fill (600);
        info.gyroRange = 0b1100;
        const auto infoData = withHeader (kInfo, encodeInfoPayload (info));
        const auto decoded = decodeInfo (infoData.data(), infoData.size());
        CHECK (decoded && decoded->covered[8] == 600 && decoded->gyroRange == 0b1100);
        CHECK (out && out->pressure == 2000);
    }

    // Bluetooth batch: 8 samples (one 15 ms connection interval), round trip and size.
    {
        std::vector<SensorFrame> frames;
        for (int i = 0; i < 8; ++i)
        {
            SensorFrame f;
            f.seq = (uint16_t) (16380 + i);  // Wraps past 14 bits
            f.timestamp = (uint32_t) ((0x1FFFF0 + i * 66) & 0x1FFFFF);
            f.pressure = (uint16_t) (400 + 37 * i);
            for (int h = 0; h < kNumHoles; ++h)
                f.holes[(size_t) h] = (uint16_t) (h * 113 + i);
            f.accel = { -8000, 16000, 300 };
            f.flags = 1;
            frames.push_back (f);
        }
        const auto data = withHeader (kBatch, encodeBatchPayload (frames));
        CHECK (data.size() + 2 <= 120);  // With F0/F7: fits one BLE packet at a 123-byte MTU
        const auto decoded = decodeBatch (data.data(), data.size());
        CHECK (decoded.size() == 8);
        for (size_t i = 0; i < decoded.size() && i < frames.size(); ++i)
        {
            CHECK (decoded[i].seq == (frames[i].seq & 0x3FFF));
            CHECK (decoded[i].pressure == frames[i].pressure);
            for (int h = 0; h < kNumHoles; ++h)
                CHECK (std::abs ((int) decoded[i].holes[(size_t) h] - (int) frames[i].holes[(size_t) h]) <= 2);
            CHECK (std::abs ((int) ((decoded[i].timestamp - frames[i].timestamp) & 0x1FFFFF)) <= 1
                   || ((frames[i].timestamp - decoded[i].timestamp) & 0x1FFFFF) <= 1);
            CHECK (std::abs (decoded[i].accel[0] - frames[i].accel[0]) < 256 && std::abs (decoded[i].accel[1] - frames[i].accel[1]) < 256);
        }
        std::printf ("Batch of 8: %d bytes as SysEx\n", (int) data.size() + 2);
    }

    // Unwrapping of the 21-bit timestamp.
    {
        Unwrapper u (kTimestampBits);
        CHECK (u.next (0x1FFFF0) == 0x1FFFF0);
        CHECK (u.next (0x000010) == 0x200010);
    }

    std::printf (failures == 0 ? "All protocol tests passed\n" : "%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
