// RS-485 link test shared definitions (TX/RX must match)
// Packet format:
//   +--------+--------+----------+------+-------+-----------------+-------+
//   | MAGIC  |  LEN   |   SEQ    | MODE | FLAGS |    PAYLOAD      | CRC16 |
//   | 0xA55A |  u16   |   u32    |  u8  |  u8   |   LEN bytes     |  u16  |
//   +--------+--------+----------+------+-------+-----------------+-------+
//     2       2         4         1      1         LEN (default 54)  2
// Total = 10 + LEN + 2 (default 64 bytes with LEN=54)
#pragma once
#include <stdint.h>
#include <string.h>

// NOTE: must NOT appear in FIXED_PATTERN (0xA5 5A 3C C3 12 34 56 78 9A BC DE F0 AA 55 F0 0F).
// 0x7E81 chosen because neither 0x7E nor 0x81 appears in FIXED_PATTERN.
static const uint16_t FRAME_MAGIC = 0x7E81;
static const uint16_t FRAME_DEFAULT_PAYLOAD_LEN = 54;
static const uint16_t FRAME_MAX_PAYLOAD_LEN = 240;
static const uint16_t FRAME_HEADER_BYTES = 10;  // magic2+len2+seq4+mode1+flags1
static const uint16_t FRAME_CRC_BYTES = 2;
static const uint16_t FRAME_TOTAL_MAX = FRAME_HEADER_BYTES + FRAME_MAX_PAYLOAD_LEN + FRAME_CRC_BYTES;

// 16-byte fixed test pattern (repeated to fill payload)
static const uint8_t FIXED_PATTERN[16] = {
    0xA5, 0x5A, 0x3C, 0xC3,
    0x12, 0x34, 0x56, 0x78,
    0x9A, 0xBC, 0xDE, 0xF0,
    0xAA, 0x55, 0xF0, 0x0F
};

struct __attribute__((packed)) FrameHeader {
    uint16_t magic;     // 0xA55A
    uint16_t len;       // payload length (not including header or CRC)
    uint32_t seq;       // monotonic sequence
    uint8_t  mode;      // echo of tx mode
    uint8_t  flags;     // reserved
};

// Link mode enumeration (TX and RX must match)
enum LinkMode {
    MODE_CONST_00 = 0,
    MODE_CONST_FF = 1,
    MODE_CONST_55 = 2,
    MODE_FRAME_SEQ = 3
};

static const char *LINK_MODE_NAMES[] = {"CONST_00", "CONST_FF", "CONST_55", "FRAME_SEQ"};
static const int LINK_MODE_COUNT = 4;

// CRC16-CCITT (poly=0x1021, init=0xFFFF)
static inline uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// Fill payload buffer by repeating FIXED_PATTERN
static inline void fill_payload(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = FIXED_PATTERN[i & 0x0F];
    }
}

// Build a complete frame into buf.
// Returns total bytes written (header + payload + crc).
// buf must have room for FRAME_TOTAL_MAX.
static inline size_t build_frame(uint8_t *buf, uint32_t seq, uint16_t payload_len, uint8_t mode) {
    FrameHeader *h = (FrameHeader *)buf;
    h->magic = FRAME_MAGIC;
    h->len = payload_len;
    h->seq = seq;
    h->mode = mode;
    h->flags = 0;
    fill_payload(buf + FRAME_HEADER_BYTES, payload_len);
    uint16_t crc = crc16_ccitt(buf, FRAME_HEADER_BYTES + payload_len);
    buf[FRAME_HEADER_BYTES + payload_len + 0] = (uint8_t)(crc & 0xFF);
    buf[FRAME_HEADER_BYTES + payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    return FRAME_HEADER_BYTES + payload_len + FRAME_CRC_BYTES;
}
