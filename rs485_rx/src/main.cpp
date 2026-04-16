// RS-485 link quality test: RX side  (ESP-IDF UART driver direct)
// Modes: CONST_00 / CONST_FF / CONST_55 / FRAME_SEQ (default)
//
// Frame RX state machine: HUNT -> LEN -> BODY -> CHECK -> HUNT
// Sync timeout: max(50ms, 4096 bytes) -> timeout counter++, stay in HUNT
//
// Serial commands:
//   b<baud>  : baud rate
//   m<0-3>   : mode
//   l<bytes> : payload length (must match TX)
//   t<ms>    : sync timeout in ms (default 50)
//   w<ms>    : reporting window in ms (default 1000)
//   e<bps>   : expected TX bits/s (for TX_LIMITED detection)
//   s        : start
//   x        : stop
//   r        : reset stats
//   ?        : status
//   v        : verbose stats
//   h        : help
#define ENABLE_DISPLAY 0

#include <M5StickC.h>
#include <esp_task_wdt.h>
#include "driver/uart.h"
#include "rs485_frame.h"

#ifndef PIN_TXD
#define PIN_TXD 32  // M5StickC Grove white
#endif
#ifndef PIN_RXD
#define PIN_RXD 33  // M5StickC Grove yellow
#endif

static const int RS485_TXD = PIN_TXD;
static const int RS485_RXD = PIN_RXD;
static const uart_port_t UART_NUM = UART_NUM_1;
static const int UART_RX_BUF = 8192;

static uint32_t rs485_baud = 1000000;
static int rx_mode = MODE_FRAME_SEQ;
static uint16_t payload_len = FRAME_DEFAULT_PAYLOAD_LEN;
static uint32_t sync_timeout_ms = 50;
static const uint32_t SYNC_TIMEOUT_BYTES = 4096;
static uint32_t report_window_ms = 1000;
static uint32_t expected_tx_bps = 0;

// --- Stats ---
static uint64_t rx_bytes_total = 0;
static uint64_t rx_byte_errors = 0;
static uint64_t frames_rx_ok = 0;
static uint64_t frames_crc_err = 0;
static uint64_t frames_lost = 0;
static uint64_t frames_reorder = 0;
static uint64_t frames_dup = 0;
static uint64_t frames_sync_timeouts = 0;
static uint64_t magic_candidates = 0;

static uint32_t expected_seq = 0;
static uint32_t max_seq_seen = 0;
static bool seq_initialized = false;

// --- RX state machine ---
enum RxState { RX_HUNT, RX_LEN, RX_BODY };
static int rx_state = RX_HUNT;
static uint8_t rx_buf[FRAME_TOTAL_MAX];
static uint16_t rx_pos = 0;
static uint16_t rx_body_need = 0;
static uint8_t magic_prev = 0;

// sync timeout
static uint32_t hunt_start_ms = 0;
static uint32_t hunt_start_bytes = 0;

// reporting window
static uint32_t win_start_ms = 0;
static uint64_t win_bytes = 0;
static uint64_t win_frames_ok = 0;
static float rx_bps_inst = 0;
static float rx_fps_inst = 0;
static uint8_t last_err_expected = 0;
static uint8_t last_err_actual = 0;

static bool running = false;
static bool uart_installed = false;

static char serial_buf[48];
static int serial_pos = 0;

void reset_stats() {
    rx_bytes_total = 0;
    rx_byte_errors = 0;
    frames_rx_ok = 0;
    frames_crc_err = 0;
    frames_lost = 0;
    frames_reorder = 0;
    frames_dup = 0;
    frames_sync_timeouts = 0;
    magic_candidates = 0;
    expected_seq = 0;
    max_seq_seen = 0;
    seq_initialized = false;
    rx_state = RX_HUNT;
    rx_pos = 0;
    magic_prev = 0;
    hunt_start_ms = millis();
    hunt_start_bytes = 0;
    win_start_ms = millis();
    win_bytes = 0;
    win_frames_ok = 0;
    rx_bps_inst = 0;
    rx_fps_inst = 0;
}

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
    uart_driver_install(UART_NUM, UART_RX_BUF, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &cfg);
    uart_set_pin(UART_NUM, RS485_TXD, RS485_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_installed = true;
    reset_stats();
    running = true;
    Serial.printf("RX START mode=%s baud=%lu payload=%u sync_to=%lums\n",
                  LINK_MODE_NAMES[rx_mode], rs485_baud,
                  payload_len, sync_timeout_ms);
}

static inline void seq_update(uint32_t seq) {
    if (!seq_initialized) {
        expected_seq = seq + 1;
        max_seq_seen = seq;
        seq_initialized = true;
        frames_rx_ok++;
        return;
    }
    if (seq == expected_seq) {
        frames_rx_ok++;
        expected_seq++;
        if (seq > max_seq_seen) max_seq_seen = seq;
    } else if (seq > expected_seq) {
        frames_lost += (seq - expected_seq);
        frames_rx_ok++;
        expected_seq = seq + 1;
        if (seq > max_seq_seen) max_seq_seen = seq;
    } else {
        if (seq == max_seq_seen) frames_dup++;
        else                     frames_reorder++;
    }
}

void process_frame() {
    FrameHeader *h = (FrameHeader *)rx_buf;
    uint16_t pl = h->len;
    uint16_t total = FRAME_HEADER_BYTES + pl + FRAME_CRC_BYTES;
    uint16_t crc_calc = crc16_ccitt(rx_buf, FRAME_HEADER_BYTES + pl);
    uint16_t crc_rx = (uint16_t)rx_buf[total - 2] | ((uint16_t)rx_buf[total - 1] << 8);
    if (crc_calc == crc_rx) {
        seq_update(h->seq);
        win_frames_ok++;
    } else {
        frames_crc_err++;
    }
}

inline void process_byte_frame_mode(uint8_t b, uint32_t now) {
    rx_bytes_total++;
    win_bytes++;
    switch (rx_state) {
    case RX_HUNT: {
        // little-endian magic 0x7E81: low byte 0x81 then high byte 0x7E
        if (magic_prev == 0x81 && b == 0x7E) {
            magic_candidates++;
            rx_buf[0] = 0x81;
            rx_buf[1] = 0x7E;
            rx_pos = 2;
            rx_state = RX_LEN;
            magic_prev = 0;
            hunt_start_ms = now;
            hunt_start_bytes = (uint32_t)rx_bytes_total;
        } else {
            magic_prev = b;
            if ((now - hunt_start_ms) >= sync_timeout_ms ||
                (uint32_t)(rx_bytes_total - hunt_start_bytes) >= SYNC_TIMEOUT_BYTES) {
                frames_sync_timeouts++;
                hunt_start_ms = now;
                hunt_start_bytes = (uint32_t)rx_bytes_total;
            }
        }
        break;
    }
    case RX_LEN: {
        rx_buf[rx_pos++] = b;
        if (rx_pos == 4) {
            FrameHeader *h = (FrameHeader *)rx_buf;
            if (h->len == 0 || h->len > FRAME_MAX_PAYLOAD_LEN) {
                frames_crc_err++;
                rx_state = RX_HUNT;
                rx_pos = 0;
                magic_prev = 0;
            } else {
                rx_body_need = 6 + h->len + FRAME_CRC_BYTES;
                rx_state = RX_BODY;
            }
        }
        break;
    }
    case RX_BODY: {
        rx_buf[rx_pos++] = b;
        if (rx_pos >= 4 + rx_body_need) {
            process_frame();
            rx_state = RX_HUNT;
            rx_pos = 0;
            magic_prev = 0;
            hunt_start_ms = now;
            hunt_start_bytes = (uint32_t)rx_bytes_total;
        }
        break;
    }
    }
}

inline void process_byte_const_mode(uint8_t b) {
    rx_bytes_total++;
    win_bytes++;
    uint8_t exp = 0x00;
    if (rx_mode == MODE_CONST_FF) exp = 0xFF;
    else if (rx_mode == MODE_CONST_55) exp = 0x55;
    if (b != exp) {
        rx_byte_errors++;
        last_err_expected = exp;
        last_err_actual = b;
    }
}

bool confident() {
    return (frames_crc_err + frames_lost) >= 100 || frames_rx_ok >= 100000;
}

double fer() {
    uint64_t denom = frames_rx_ok + frames_crc_err + frames_lost;
    if (denom == 0) return 0.0;
    return (double)(frames_crc_err + frames_lost) / (double)denom;
}

const char *link_status() {
    if (expected_tx_bps == 0) return "N/A";
    double f = fer();
    double rx_bps_d = rx_bps_inst;
    if (rx_bps_d < 0.95 * expected_tx_bps) {
        return (f >= 0.001) ? "LINK_LOSS" : "TX_LIMITED";
    }
    return "LINE_RATE";
}

void show_status() {
    double f = fer();
    Serial.printf("STATUS: baud=%lu mode=%s running=%d bytes=%llu ok=%llu crc=%llu lost=%llu reorder=%llu dup=%llu sync_to=%llu fer=%.5f bps=%.0f fps=%.0f conf=%d\n",
                  rs485_baud, LINK_MODE_NAMES[rx_mode], running,
                  rx_bytes_total, frames_rx_ok, frames_crc_err, frames_lost,
                  frames_reorder, frames_dup, frames_sync_timeouts,
                  f, rx_bps_inst, rx_fps_inst, (int)confident());
}

void show_verbose() {
    double f = fer();
    Serial.println("=== VERBOSE ===");
    Serial.printf("  baud        : %lu\n", rs485_baud);
    Serial.printf("  mode        : %s\n", LINK_MODE_NAMES[rx_mode]);
    Serial.printf("  rx_bytes    : %llu\n", rx_bytes_total);
    Serial.printf("  frames_ok   : %llu\n", frames_rx_ok);
    Serial.printf("  frames_crc  : %llu\n", frames_crc_err);
    Serial.printf("  frames_lost : %llu\n", frames_lost);
    Serial.printf("  frames_ro   : %llu\n", frames_reorder);
    Serial.printf("  frames_dup  : %llu\n", frames_dup);
    Serial.printf("  sync_to     : %llu\n", frames_sync_timeouts);
    Serial.printf("  magic_hits  : %llu\n", magic_candidates);
    Serial.printf("  byte_errors : %llu\n", rx_byte_errors);
    Serial.printf("  FER         : %.5f%% (%.5f)\n", f * 100.0, f);
    Serial.printf("  rx_bps      : %.0f\n", rx_bps_inst);
    Serial.printf("  rx_fps      : %.0f\n", rx_fps_inst);
    Serial.printf("  confident   : %d\n", (int)confident());
    if (expected_tx_bps > 0) {
        Serial.printf("  expected_tx : %lu bps\n", expected_tx_bps);
        Serial.printf("  status      : %s\n", link_status());
    }
}

void print_help() {
    Serial.println("=== RS485-RX Commands (ESP-IDF direct) ===");
    Serial.println("  b<baud>  : baud rate");
    Serial.println("  m<0-3>   : mode (0=CONST_00 1=CONST_FF 2=CONST_55 3=FRAME_SEQ)");
    Serial.println("  l<bytes> : payload length");
    Serial.println("  t<ms>    : sync timeout (default 50)");
    Serial.println("  w<ms>    : reporting window");
    Serial.println("  e<bps>   : expected TX bps");
    Serial.println("  s        : start / reset");
    Serial.println("  x        : stop");
    Serial.println("  r        : reset stats");
    Serial.println("  ?        : status");
    Serial.println("  v        : verbose stats");
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
            rx_mode = m;
            Serial.printf("Mode=%s\n", LINK_MODE_NAMES[rx_mode]);
            if (running) init_rs485();
        }
    } else if (cmd[0] == 'l') {
        uint32_t v = strtoul(cmd + 1, NULL, 10);
        if (v >= 1 && v <= FRAME_MAX_PAYLOAD_LEN) {
            payload_len = (uint16_t)v;
            Serial.printf("Payload=%u\n", payload_len);
        }
    } else if (cmd[0] == 't') {
        sync_timeout_ms = strtoul(cmd + 1, NULL, 10);
        Serial.printf("SyncTimeout=%lums\n", sync_timeout_ms);
    } else if (cmd[0] == 'w') {
        report_window_ms = strtoul(cmd + 1, NULL, 10);
        if (report_window_ms < 100) report_window_ms = 100;
        Serial.printf("Window=%lums\n", report_window_ms);
    } else if (cmd[0] == 'e') {
        expected_tx_bps = strtoul(cmd + 1, NULL, 10);
        Serial.printf("ExpectedTxBps=%lu\n", expected_tx_bps);
    } else if (cmd[0] == 's') {
        init_rs485();
    } else if (cmd[0] == 'x') {
        running = false;
        Serial.println("Stopped.");
    } else if (cmd[0] == 'r') {
        reset_stats();
        Serial.println("Stats reset.");
    } else if (cmd[0] == '?') {
        show_status();
    } else if (cmd[0] == 'v') {
        show_verbose();
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

void setup() {
    M5.begin();
#if !ENABLE_DISPLAY
    M5.Axp.ScreenBreath(0);
#endif
    Serial.begin(115200);
    esp_task_wdt_init(30, false);
    Serial.println("\n=== RS485-RX (ESP-IDF direct) ===");
    print_help();
}

void loop() {
    M5.update();
    poll_serial();
    uint32_t now = millis();

    if (!running) return;

    // ESP-IDF direct batch read
    static uint8_t rxbuf[2048];
    int n = uart_read_bytes(UART_NUM, rxbuf, sizeof(rxbuf), 0);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            if (rx_mode == MODE_FRAME_SEQ) {
                process_byte_frame_mode(rxbuf[i], now);
            } else {
                process_byte_const_mode(rxbuf[i]);
            }
        }
    }

    if (now - win_start_ms >= report_window_ms) {
        uint32_t dt = now - win_start_ms;
        rx_bps_inst = (float)win_bytes * 10.0f * 1000.0f / dt;
        rx_fps_inst = (float)win_frames_ok * 1000.0f / dt;
        win_start_ms = now;
        win_bytes = 0;
        win_frames_ok = 0;

        double f = fer();
        Serial.printf("RX mode=%s baud=%lu ok=%llu crc=%llu lost=%llu ro=%llu st=%llu fer=%.4f%% tp=%.0fbps fps=%.0f conf=%d\n",
                      LINK_MODE_NAMES[rx_mode], rs485_baud,
                      frames_rx_ok, frames_crc_err, frames_lost,
                      frames_reorder, frames_sync_timeouts,
                      f * 100.0, rx_bps_inst, rx_fps_inst, (int)confident());
    }
}
