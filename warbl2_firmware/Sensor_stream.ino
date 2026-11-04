// Sensor stream: sends lightly processed sensor readings to a host app over USB MIDI SysEx, for physical-modelling synthesis on the computer.
// The stream is turned on by the host (it isn't saved) and only goes over USB, where latency is lowest.
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
//   STREAM_INFO (0x10): sequence of 16-bit values: firmware version, 9 tone hole baselines, 9 tone hole covered thresholds,
//     ambient pressure (10-bit, as used by the firmware), accelerometer range code, gyro range code (Adafruit lsm6ds range enums), loop period in ms.

#define STREAM_SYSEX_ID_1 0x7D
#define STREAM_SYSEX_ID_2 0x57
#define STREAM_PROTOCOL_VERSION 1

#define STREAM_FRAME 0x01
#define STREAM_INFO 0x10
#define STREAM_CONFIG 0x20
#define STREAM_REQUEST_INFO 0x21

#define STREAM_CONFIG_ON 0x01
#define STREAM_CONFIG_SUPPRESS_NOTES 0x02

#define STREAM_MAX_SYSEX 64  // Large enough for a frame (42 bytes) or the info message (~53 bytes).

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
    byte message[STREAM_MAX_SYSEX] = { STREAM_SYSEX_ID_1, STREAM_SYSEX_ID_2, STREAM_PROTOCOL_VERSION, type };
    memcpy(message + 4, payload, payloadLength);
    MIDI.sendSysEx(payloadLength + 4, message, false);  // The library adds F0 and F7.
}



// Build and send one frame with the latest readings. Called every loop while streaming.
void sendSensorFrame() {
    static uint16_t seq = 0;

    if (!sensorStreamOn || USBstatus != USB_HOST) {  // The stream only goes over USB.
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
    w.put(toneholeRawUpdated ? 1 : 0, 7);
    toneholeRawUpdated = false;

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
    w.put(connIntvl == 0 ? 2 : 3, 16);  // Loop period in ms (see calculateDelayTime()).

    sendStreamSysEx(STREAM_INFO, payload, w.length);
}



void setSensorStream(bool on, bool suppressNotes) {

    if (on && suppressNotes && noteon) {  // Don't leave a note hanging when note output stops.
        sendMIDI(NOTE_OFF, mainMidiChannel, notePlaying, 64);
        noteon = 0;
    }

    sensorStreamOn = on;
    sensorStreamSuppressNotes = on && suppressNotes;

    if (on) {
        sox.setAccelDataRate(LSM6DS_RATE_1_66K_HZ);  // Faster than the 1 kHz stream loop, so each frame has a fresh reading.
        sox.setGyroDataRate(LSM6DS_RATE_1_66K_HZ);
    } else {
        sox.setAccelDataRate(LSM6DS_RATE_208_HZ);  // Back to the startup rate.
        loadPrefs();                               // Turns the gyro back on or off as the preset needs.
    }
}



// Incoming SysEx from USB. The array includes F0 and F7.
void handleSysExFromUSB(byte* data, unsigned size) {
    if (size < 6 || data[1] != STREAM_SYSEX_ID_1 || data[2] != STREAM_SYSEX_ID_2 || data[3] != STREAM_PROTOCOL_VERSION) {
        return;
    }

    switch (data[4]) {
        case STREAM_CONFIG:
            if (size >= 7) {
                setSensorStream(data[5] & STREAM_CONFIG_ON, data[5] & STREAM_CONFIG_SUPPRESS_NOTES);
            }
            break;

        case STREAM_REQUEST_INFO:
            sendSensorStreamInfo();
            break;
    }
}
