// RS-485 link quality test: TX side  (ESP-IDF UART driver direct)
// Modes: CONST_00 / CONST_FF / CONST_55 / FRAME_SEQ (default)
//
// Serial commands:
//   b<baud>  : baud rate (9600..5000000)
//   m<0-3>   : mode (0=CONST_00, 1=CONST_FF, 2=CONST_55, 3=FRAME_SEQ)
//   l<bytes> : payload length (1..240, default 54)
//   g<us>    : inter-frame gap in microseconds
//   n<count> : burst count (0 = infinite)
//   s        : start
//   x        : stop
//   ?        : status
//   h        : help
#include <M5StickC.h>
#include "driver/uart.h"
#include "rs485_frame.h"

#define ENABLE_DISPLAY 0

#ifndef PIN_TXD
#define PIN_TXD 32  // M5StickC Grove white
#endif
#ifndef PIN_RXD
#define PIN_RXD 33  // M5StickC Grove yellow
#endif

static const int RS485_TXD = PIN_TXD;
static const int RS485_RXD = PIN_RXD;
static const uart_port_t UART_NUM = UART_NUM_1;

static uint32_t rs485_baud = 1000000;
static int tx_mode = MODE_FRAME_SEQ;
static uint16_t payload_len = FRAME_DEFAULT_PAYLOAD_LEN;
static uint32_t frame_gap_us = 0;
static uint32_t burst_count = 0; // 0 = infinite

static uint32_t tx_seq = 1;
static uint64_t tx_frame_count = 0;
static uint64_t tx_byte_count = 0;
static uint32_t win_frames = 0;
static uint64_t win_bytes = 0;
static uint32_t win_start_ms = 0;
static float stat_fps = 0;
static float stat_tp_kbaud = 0;
static bool running = false;

static uint8_t frame_buf[FRAME_TOTAL_MAX];
static uint8_t const_buf[256];

static char serial_buf[48];
static int serial_pos = 0;

static bool uart_installed = false;

void init_rs485() {
    if (uart_installed) {
        uart_driver_delete(UART_NUM);
        uart_installed = false;
    }
    uart_config_t cfg = {};
    cfg.baud_rate = (int)rs485_baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_APB;
    // TX ring buffer 4096; RX ring buffer 256 (we don't read here but driver requires nonzero)
    uart_driver_install(UART_NUM, 256, 4096, 0, NULL, 0);
    uart_param_config(UART_NUM, &cfg);
    uart_set_pin(UART_NUM, RS485_TXD, RS485_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_installed = true;

    tx_seq = 1;
    tx_frame_count = 0;
    tx_byte_count = 0;
    win_frames = 0;
    win_bytes = 0;
    win_start_ms = millis();
    stat_fps = 0;
    stat_tp_kbaud = 0;
    running = true;

    uint8_t fill = 0x00;
    if (tx_mode == MODE_CONST_00) fill = 0x00;
    else if (tx_mode == MODE_CONST_FF) fill = 0xFF;
    else if (tx_mode == MODE_CONST_55) fill = 0x55;
    memset(const_buf, fill, sizeof(const_buf));

    Serial.printf("TX START mode=%s baud=%lu payload=%u gap=%luus burst=%lu\n",
                  LINK_MODE_NAMES[tx_mode], rs485_baud,
                  payload_len, frame_gap_us, burst_count);
}

void show_status() {
    Serial.printf("STATUS: baud=%lu mode=%s payload=%u gap=%lu running=%d frames=%llu bytes=%llu fps=%.0f tp=%.1fkbaud\n",
                  rs485_baud, LINK_MODE_NAMES[tx_mode], payload_len, frame_gap_us,
                  running, tx_frame_count, tx_byte_count, stat_fps, stat_tp_kbaud);
}

void print_help() {
    Serial.println("=== RS485-TX Commands ===");
    Serial.println("  b<baud>  : baud rate");
    Serial.println("  m<0-3>   : mode (0=CONST_00 1=CONST_FF 2=CONST_55 3=FRAME_SEQ)");
    Serial.println("  l<bytes> : payload length (1..240)");
    Serial.println("  g<us>    : inter-frame gap (us)");
    Serial.println("  n<count> : burst count (0=infinite)");
    Serial.println("  s        : start");
    Serial.println("  x        : stop");
    Serial.println("  ?        : status");
    Serial.println("  h        : help");
}

void handle_serial_command(const char *cmd) {
    if (cmd[0] == 'b') {
        uint32_t v = strtoul(cmd + 1, NULL, 10);
        if (v >= 9600 && v <= 5000000) {
            rs485_baud = v;
            Serial.printf("Baud=%lu\n", rs485_baud);
            if (running) init_rs485();
        }
    } else if (cmd[0] == 'm') {
        int m = cmd[1] - '0';
        if (m >= 0 && m < LINK_MODE_COUNT) {
            tx_mode = m;
            Serial.printf("Mode=%s\n", LINK_MODE_NAMES[tx_mode]);
            if (running) init_rs485();
        }
    } else if (cmd[0] == 'l') {
        uint32_t v = strtoul(cmd + 1, NULL, 10);
        if (v >= 1 && v <= FRAME_MAX_PAYLOAD_LEN) {
            payload_len = (uint16_t)v;
            Serial.printf("Payload=%u\n", payload_len);
        }
    } else if (cmd[0] == 'g') {
        frame_gap_us = strtoul(cmd + 1, NULL, 10);
        Serial.printf("Gap=%luus\n", frame_gap_us);
    } else if (cmd[0] == 'n') {
        burst_count = strtoul(cmd + 1, NULL, 10);
        Serial.printf("Burst=%lu\n", burst_count);
    } else if (cmd[0] == 's') {
        init_rs485();
    } else if (cmd[0] == 'x') {
        running = false;
        Serial.println("Stopped.");
    } else if (cmd[0] == '?') {
        show_status();
    } else if (cmd[0] == 'h') {
        print_help();
    } else {
        Serial.printf("Unknown: %s\n", cmd);
        print_help();
    }
}

void poll_serial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serial_pos > 0) {
                serial_buf[serial_pos] = 0;
                handle_serial_command(serial_buf);
                serial_pos = 0;
            }
        } else if (serial_pos < (int)sizeof(serial_buf) - 1) {
            serial_buf[serial_pos++] = c;
        }
    }
}

inline size_t send_one() {
    size_t n = 0;
    if (tx_mode == MODE_FRAME_SEQ) {
        n = build_frame(frame_buf, tx_seq, payload_len, (uint8_t)tx_mode);
        uart_write_bytes(UART_NUM, (const char*)frame_buf, n);
        tx_seq++;
    } else {
        n = 64;
        uart_write_bytes(UART_NUM, (const char*)const_buf, n);
    }
    tx_frame_count++;
    tx_byte_count += n;
    win_frames++;
    win_bytes += n;
    return n;
}

void setup() {
    M5.begin();
#if !ENABLE_DISPLAY
    M5.Axp.ScreenBreath(0);
#endif
    Serial.begin(115200);
    Serial.println("\n=== RS485-TX (ESP-IDF direct) ===");
    print_help();
}

void loop() {
    M5.update();
    poll_serial();
    uint32_t now = millis();

    if (running) {
        // Burst send: 32 frames per loop iteration to saturate the link
        int to_send = 1;
        if (frame_gap_us == 0 && burst_count == 0) {
            to_send = 32;
        }
        while (to_send-- > 0) {
            if (burst_count != 0 && tx_frame_count >= burst_count) break;
            send_one();
            if (frame_gap_us > 0) delayMicroseconds(frame_gap_us);
        }

        if (now - win_start_ms >= 1000) {
            uint32_t dt = now - win_start_ms;
            stat_fps = win_frames * 1000.0f / dt;
            stat_tp_kbaud = (win_bytes * 10.0f * 1000.0f / dt) / 1000.0f;
            float tp_use = stat_tp_kbaud * 1000.0f * 100.0f / rs485_baud;

            Serial.printf("TX mode=%s baud=%lu frames=%llu bytes=%llu fps=%.0f tp=%.1fkbaud (%.1f%%)\n",
                          LINK_MODE_NAMES[tx_mode], rs485_baud, tx_frame_count, tx_byte_count,
                          stat_fps, stat_tp_kbaud, tp_use);

            win_frames = 0;
            win_bytes = 0;
            win_start_ms = now;
        }
    }
}
