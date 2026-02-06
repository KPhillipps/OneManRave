#pragma once
#include <Arduino.h>

namespace Proto {
static const uint8_t TYPE_FFT = 0x01;
static const uint8_t TYPE_CMD = 0x02;

static const uint8_t FFT_PAYLOAD_LEN = 68;  // 12 floats + vocal + spdif + hpsPitchClass[12] + pitch + strength + sustain
static const uint8_t CMD_PAYLOAD_LEN = FFT_PAYLOAD_LEN;   // Fixed-length frames for both FFT and CMD

inline uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

inline bool encodeFrame(uint8_t type, uint8_t seq,
                        const uint8_t* payload, uint8_t payloadLen,
                        uint8_t* out, size_t outCap, size_t& outLen) {
    size_t needed = 1 + 1 + 1 + 1 + payloadLen + 2 + 1;  // SOF+type+seq+len+payload+crc16+EOF
    if (outCap < needed) return false;

    out[0] = 0xAA;
    out[1] = type;
    out[2] = seq;
    out[3] = payloadLen;
    memcpy(&out[4], payload, payloadLen);

    uint16_t crc = crc16_ccitt(&out[1], 3 + payloadLen);  // type..payload
    out[4 + payloadLen] = crc & 0xFF;
    out[5 + payloadLen] = crc >> 8;
    out[6 + payloadLen] = 0xBB;

    outLen = needed;
    return true;
}
}  // namespace Proto
