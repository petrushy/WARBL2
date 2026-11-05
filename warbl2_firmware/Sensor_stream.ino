// Sensor stream: sends lightly processed sensor readings to a host app over MIDI SysEx, for physical-modelling synthesis on the computer.
// The stream is turned on by the host (it isn't saved). Over USB (lowest latency) one frame goes out per loop. Over BLE MIDI
// (when the host turned it on over BLE and no USB host is connected) the samples of one connection interval go out
// together in one STREAM_BATCH, sized to fit one BLE packet.
//
// All messages: F0 7D 57 <protocol version> <type> <payload> F7. 7D is the non-commercial manufacturer ID and 57 ('W') marks WARBL.
// Payloads are bit-packed MSB-first, 7 bits per SysEx data byte, so every sensor bit gets through.
//
// Host -> WARBL:
//   STREAM_CONFIG (0x20), one byte: bit 0 stream on, bit 1 suppress notes/pitchbend/CC output while streaming.
//   STREAM_REQUEST_INFO (0x21), no payload: WARBL answers with STREAM_INFO.
// WARBL -> host:
//   STREAM_FRAME (0x01), one per loop:
//     seq 14 bits, timestamp 21 bits (RTC2 ticks, 1/32768 s, wraps every 64 s),
//     9 x 10-bit tone hole readings (ambient-light corrected by the ATmega, before smoothing and baseline subtraction),
//     12-bit raw pressure (before smoothing), 3 x 16-bit raw accelerometer, 3 x 16-bit raw gyro (signed, two's complement),
//     9-bit holeCovered (the firmware's own covered/uncovered decision, as a hint), 7 flag bits (bit 0: tone holes updated this frame).
//   STREAM_BATCH (0x02), BLE only, once per connection interval: first seq 14 bits, timestamps of the first and last
//     sample 2 x 21 bits, count 5 bits, accelerometer top 8 bits x 3 (once per batch), 7 flag bits; then per sample
//     12-bit raw pressure and 9 x 8-bit tone hole readings (the top 8 of the 10). No gyro.
//   STREAM_INFO (0x10): sequence of 16-bit values: firmware version, 9 tone hole baselines, 9 tone hole covered thresholds,
//     ambient pressure (10-bit, as used by the firmware), accelerometer range code, gyro range code (Adafruit lsm6ds range enums), loop period in ms.

#define STREAM_SYSEX_ID_1 0x7D
#define STREAM_SYSEX_ID_2 0x57
#define STREAM_PROTOCOL_VERSION 1

#define STREAM_FRAME 0x01
#define STREAM_BATCH 0x02
#define STREAM_INFO 0x10
#define STREAM_CONFIG 0x20
#define STREAM_REQUEST_INFO 0x21

#define STREAM_CONFIG_ON 0x01
#define STREAM_CONFIG_SUPPRESS_NOTES 0x02

#define STREAM_MAX_SYSEX 64  // Large enough for a frame (42 bytes) or the info message (~53 bytes).
#define STREAM_BATCH_MAX_SAMPLES 16  // 16 x 2 ms covers a 30 ms connection interval; ~212 bytes as SysEx

// Packs values MSB-first into 7-bit SysEx data bytes.
struct SysExBitWriter {
    byte* out;
    byte length = 0;
    byte bitsInByte = 0;

    void put(uint32_t value, byte bits) {
        for (int b = bits - 1; b >= 0; b--) {
            if (bitsInByte == 0) {
                out[length++] = 0;
            }
            out[length - 1] |= ((value >> b) & 1) << (6 - bitsInByte);
            bitsInByte = (bitsInByte + 1) % 7;
        }
    }
};



void sendStreamSysEx(byte type, const byte* payload, byte payloadLength) {
    // The whole message in one write, straight to TinyUSB: the MIDI library writes a SysEx byte by byte, and
    // TinyUSB starts a USB transfer after every write, so the first 4-byte packet left alone and the rest waited
    // for the host's next poll (1 ms): at most 500 frames/s. A frame is 60 bytes as USB-MIDI packets, so it
    // fits one 64-byte transfer.
    byte message[240] = { 0xF0, STREAM_SYSEX_ID_1, STREAM_SYSEX_ID_2, STREAM_PROTOCOL_VERSION, type };
    if (payloadLength + 6 > sizeof(message)) {
        return;
    }
    memcpy(message + 5, payload, payloadLength);
    message[5 + payloadLength] = 0xF7;
    if (sensorStreamOverBLE) {
        blemidi.sendSysExMessage(message, payloadLength + 6);  // In as few BLE packets as the MTU allows
    } else {
        tud_midi_stream_write(0, message, payloadLength + 6);
    }
}



// Build and send one frame with the latest readings. Called every loop while streaming.
// Size of a batch of `samples` as one BLE MIDI packet: BLE header and two timestamps, F0, 4 header bytes, the payload
// (7 bits per byte), F7.
uint16_t batchPacketBytes(byte samples) {
    const uint32_t bits = 14 + 21 + 21 + 5 + 24 + 7 + (12 + 9 * 8) * (uint32_t)samples;
    return 3 + 1 + 4 + (bits + 6) / 7 + 1;
}

// BLE: collect this loop's readings, and send them with the others of this connection interval.
void addBatchSample(uint16_t& seq) {
    static byte holes[STREAM_BATCH_MAX_SAMPLES][9];
    static uint16_t pressures[STREAM_BATCH_MAX_SAMPLES];
    static byte count = 0;
    static uint16_t firstSeq = 0;
    static uint32_t firstTime = 0, lastTime = 0;
    static uint8_t flags = 0;
    static unsigned long lastSendMs = 0;

    if (connIntvl == 0 || !blemidi.notifyEnabled()) {
        count = 0;
        return;
    }

    if (count == 0) {
        firstSeq = seq;
        firstTime = NRF_RTC2->COUNTER & 0x1FFFFF;
        flags = 0;
    }
    lastTime = NRF_RTC2->COUNTER & 0x1FFFFF;
    for (byte i = 0; i < 9; i++) {
        holes[count][i] = (byte)(constrain(toneholeRaw[i], 0, 1023) >> 2);
    }
    pressures[count] = twelveBitPressure & 0xFFF;
    flags |= (toneholeRawUpdated ? 1 : 0) | (toneholeRawRejected ? 2 : 0);
    toneholeRawUpdated = false;
    toneholeRawRejected = false;
    seq++;
    count++;

    // Send once per connection interval, or earlier if one more sample wouldn't fit one BLE packet: a batch always
    // goes in a single packet, so if it can't be sent it is lost whole, never cut off in the middle of a SysEx (which
    // would leave the host's BLE MIDI parser waiting for its end, swallowing the notes that follow).
    const unsigned long now = millis();
    const bool full = count >= STREAM_BATCH_MAX_SAMPLES || batchPacketBytes(count + 1) > blemidi.maxPacketLength();
    if (!full && (now - lastSendMs) < (unsigned long)connIntvl) {
        return;
    }
    if (batchPacketBytes(count) > blemidi.maxPacketLength()) {  // A tiny MTU: even this doesn't fit one packet
        count = 0;
        return;
    }
    lastSendMs = now;

    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;
    sox.getEvent(&accel, &gyro, &temp);  // Fills sox.rawAccX etc.

    byte payload[220] = { 0 };
    SysExBitWriter w;
    w.out = payload;
    w.put(firstSeq & 0x3FFF, 14);
    w.put(firstTime, 21);
    w.put(lastTime, 21);
    w.put(count, 5);
    w.put((uint8_t)(sox.rawAccX >> 8), 8);  // Top 8 bits (signed), enough for tilt
    w.put((uint8_t)(sox.rawAccY >> 8), 8);
    w.put((uint8_t)(sox.rawAccZ >> 8), 8);
    w.put(flags & 0x7F, 7);
    for (byte s = 0; s < count; s++) {
        w.put(pressures[s], 12);
        for (byte i = 0; i < 9; i++) {
            w.put(holes[s][i], 8);
        }
    }
    sendStreamSysEx(STREAM_BATCH, payload, w.length);
    count = 0;
}



void sendSensorFrame() {
    static uint16_t seq = 0;

    if (!sensorStreamOn) {
        return;
    }
    if (sensorStreamOverBLE) {
        addBatchSample(seq);
        return;
    }
    if (USBstatus != USB_HOST) {  // Otherwise the stream only goes over USB.
        return;
    }

    sensors_event_t accel;
    sensors_event_t gyro;
    sensors_event_t temp;
    sox.getEvent(&accel, &gyro, &temp);  // Fills sox.rawAccX etc. with the latest 16-bit readings.

    byte payload[STREAM_MAX_SYSEX - 4] = { 0 };
    SysExBitWriter w;
    w.out = payload;

    w.put(seq++ & 0x3FFF, 14);
    w.put(NRF_RTC2->COUNTER & 0x1FFFFF, 21);
    for (byte i = 0; i < 9; i++) {
        w.put(toneholeRaw[i] & 0x3FF, 10);
    }
    w.put(twelveBitPressure & 0xFFF, 12);
    w.put((uint16_t)sox.rawAccX, 16);
    w.put((uint16_t)sox.rawAccY, 16);
    w.put((uint16_t)sox.rawAccZ, 16);
    w.put((uint16_t)sox.rawGyroX, 16);
    w.put((uint16_t)sox.rawGyroY, 16);
    w.put((uint16_t)sox.rawGyroZ, 16);
    w.put(holeCovered & 0x1FF, 9);
    w.put((toneholeRawUpdated ? 1 : 0) | (toneholeRawRejected ? 2 : 0), 7);  // Flags: new hole readings; a garbled transfer was rejected
    toneholeRawUpdated = false;
    toneholeRawRejected = false;

    sendStreamSysEx(STREAM_FRAME, payload, w.length);
}



void sendSensorStreamInfo() {
    byte payload[STREAM_MAX_SYSEX - 4] = { 0 };
    SysExBitWriter w;
    w.out = payload;

    w.put(VERSION, 16);
    for (byte i = 0; i < 9; i++) {
        w.put(toneholeBaseline[i], 16);
    }
    for (byte i = 0; i < 9; i++) {
        w.put(toneholeCovered[i], 16);
    }
    w.put(sensorCalibration, 16);
    w.put(sox.getAccelRange(), 16);
    w.put(sox.getGyroRange(), 16);
    w.put((connIntvl == 0 || (sensorStreamOn && USBstatus == USB_HOST)) ? 2 : 3, 16);  // Loop period in ms (see calculateDelayTime()).

    sendStreamSysEx(STREAM_INFO, payload, w.length);
}



void setSensorStream(bool on, bool suppressNotes, bool overBLE) {

    if (on && suppressNotes && noteon) {  // Don't leave a note hanging when note output stops.
        sendMIDI(NOTE_OFF, mainMidiChannel, notePlaying, 64);
        noteon = 0;
    }

    sensorStreamOn = on;
    sensorStreamSuppressNotes = on && suppressNotes;
    sensorStreamOverBLE = on && overBLE;

    if (on) {
        sox.setAccelDataRate(LSM6DS_RATE_1_66K_HZ);  // Faster than the stream loop, so each frame has a fresh reading.
        sox.setGyroDataRate(LSM6DS_RATE_1_66K_HZ);
    } else {
        sox.setAccelDataRate(LSM6DS_RATE_208_HZ);  // Back to the startup rate.
        loadPrefs();                               // Turns the gyro back on or off as the preset needs.
    }
}



// Incoming SysEx. The array includes F0 and F7. A stream turned on over BLE goes over BLE, unless a USB host is
// connected (USB is faster, so it wins).
void handleSysExFromUSB(byte* data, unsigned size) {
    handleStreamSysEx(data, size, false);
}

void handleSysExFromBLE(byte* data, unsigned size) {
    if (USBstatus == USB_HOST) {
        return;
    }
    handleStreamSysEx(data, size, true);
}

void handleStreamSysEx(byte* data, unsigned size, bool fromBLE) {
    if (size < 6 || data[1] != STREAM_SYSEX_ID_1 || data[2] != STREAM_SYSEX_ID_2 || data[3] != STREAM_PROTOCOL_VERSION) {
        return;
    }

    switch (data[4]) {
        case STREAM_CONFIG:
            if (size >= 7) {
                setSensorStream(data[5] & STREAM_CONFIG_ON, data[5] & STREAM_CONFIG_SUPPRESS_NOTES, fromBLE);
            }
            break;

        case STREAM_REQUEST_INFO:
            if (fromBLE != sensorStreamOverBLE && sensorStreamOn) {
                break;  // Answer on the transport the stream uses
            }
            {
                const bool wasBLE = sensorStreamOverBLE;
                sensorStreamOverBLE = fromBLE;  // Reply where it was asked
                sendSensorStreamInfo();
                sensorStreamOverBLE = wasBLE;
            }
            break;
    }
}
