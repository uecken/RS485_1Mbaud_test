// RS-485 link quality test: TRX (TX + RX combined)
// Modes: CONST_00 / CONST_FF / CONST_55 / FRAME_SEQ / USB_THROUGH / SCS_TEST
//
// Serial commands:
//   b<baud>  : baud rate (9600..5000000)
//   m<0-5>   : mode (0=CONST_00, 1=FF, 2=55, 3=FRAME_SEQ, 4=USB_THROUGH, 5=SCS_TEST)
//   l<bytes> : payload length (1..240, default 54)
//   g<us>    : inter-frame gap in microseconds
//   n<count> : burst count (0 = infinite)
//   e<bps>   : expected TX bps (for link status detection)
//   I<id>    : SCS_TEST servo ID (default 2)
//   T<0-3>   : SCS_TEST sub-mode (0=PING, 1=READ, 2=WRITE, 3=WRITE+READ)
//   s        : start
//   x        : stop  (USB_THROUGH: Ctrl+X x3)
//   r        : reset stats
//   ?        : status
//   v        : verbose stats
//   h        : help
#include <M5StickC.h>
#include <esp_timer.h>
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
static const int UART_RX_BUF = 8192;

// --- TRX Mode ---
enum TrxMode {
    TRX_CONST_00    = 0,
    TRX_CONST_FF    = 1,
    TRX_CONST_55    = 2,
    TRX_FRAME_SEQ   = 3,
    TRX_USB_THROUGH = 4,
    TRX_SCS_TEST    = 5,
};
static const int TRX_MODE_COUNT = 6;
static const char *TRX_MODE_NAMES[] = {
    "CONST_00", "CONST_FF", "CONST_55", "FRAME_SEQ", "USB_THROUGH", "SCS_TEST"
};
static int trx_mode = TRX_USB_THROUGH;

// --- Direction control ---
enum TrxDir { DIR_TXRX = 0, DIR_TX = 1, DIR_RX = 2 };
static const char *DIR_NAMES[] = {"TX+RX", "TX_ONLY", "RX_ONLY"};
static int trx_dir = DIR_TXRX;

// --- 1-second report ---
static bool report_enabled = false;  // default OFF

// --- Config ---
static uint32_t rs485_baud = 1000000;
static uint16_t payload_len = FRAME_DEFAULT_PAYLOAD_LEN;
static uint32_t frame_gap_us = 0;
static uint32_t burst_count = 0;  // 0 = infinite
static uint32_t expected_tx_bps = 0;

// --- TX state ---
static uint32_t tx_seq = 1;
static uint64_t tx_frame_count = 0;
static uint64_t tx_byte_count = 0;
static uint32_t win_tx_frames = 0;
static uint64_t win_tx_bytes = 0;

// --- RX state (FRAME_SEQ) ---
enum RxState { RX_HUNT, RX_LEN, RX_BODY };
static int rx_state = RX_HUNT;
static uint8_t rx_buf[FRAME_TOTAL_MAX];
static uint16_t rx_pos = 0;
static uint16_t rx_body_need = 0;
static uint8_t magic_prev = 0;

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

static uint32_t hunt_start_ms = 0;
static uint32_t hunt_start_bytes = 0;
static const uint32_t SYNC_TIMEOUT_MS = 50;
static const uint32_t SYNC_TIMEOUT_BYTES = 4096;

// --- Reporting ---
static uint32_t win_start_ms = 0;
static uint64_t win_rx_bytes = 0;
static uint64_t win_rx_frames_ok = 0;
static float rx_bps_inst = 0;
static float rx_fps_inst = 0;

static bool running = false;
static bool uart_installed = false;

// --- Buffers ---
static uint8_t frame_buf[FRAME_TOTAL_MAX];
static uint8_t const_buf[256];
static char serial_buf[48];
static int serial_pos = 0;

// --- USB_THROUGH ---
static uint8_t usb_tx_buf[244];
static uint16_t usb_tx_pos = 0;
static uint32_t usb_tx_last_byte_ms = 0;
static uint8_t usb_ctrlx_count = 0;
static const uint32_t USB_TX_IDLE_MS = 1;

// --- SCS_TEST mode ---
// SCS protocol: [FF FF] [ID] [LEN] [INST] [PARAMS...] [CHKSUM]
// Sub-modes:
enum ScsSubMode { SCS_PING = 0, SCS_READ = 1, SCS_WRITE = 2, SCS_WRITE_READ = 3 };
static const char *SCS_SUB_NAMES[] = {"PING", "READ", "WRITE", "WRITE+READ"};
static int scs_sub_mode = SCS_PING;
static uint8_t scs_servo_id = 2;

// SCS_TEST stats
static uint64_t scs_tx_count = 0;
static uint64_t scs_rx_ok = 0;
static uint64_t scs_rx_fail = 0;
static uint64_t scs_rtt_sum_us = 0;
static uint32_t scs_rtt_min_us = UINT32_MAX;
static uint32_t scs_rtt_max_us = 0;
static uint32_t scs_rtt_count = 0;
// Histogram: <5ms, 5-10, 10-15, 15-20, 20-30, 30-50, >=50
static const int SCS_HIST_BINS = 7;
static uint32_t scs_hist[7] = {};
static const uint32_t SCS_HIST_EDGES_MS[] = {5, 10, 15, 20, 30, 50};

static uint16_t scs_write_target = 1024;  // alternates 1024 <-> 3072

static uint8_t scs_pkt_buf[64];

static uint8_t scs_checksum(const uint8_t *pkt, int len) {
    // pkt[2]=ID, pkt[3]=LEN, pkt[4]=INST, pkt[5..]=params
    uint8_t sum = 0;
    for (int i = 2; i < len - 1; i++) sum += pkt[i];
    return (~sum) & 0xFF;
}

static int scs_build_ping(uint8_t *buf, uint8_t id) {
    buf[0] = 0xFF; buf[1] = 0xFF;
    buf[2] = id; buf[3] = 0x02; buf[4] = 0x01;
    buf[5] = scs_checksum(buf, 6);
    return 6;
}

static int scs_build_read(uint8_t *buf, uint8_t id, uint8_t addr, uint8_t len) {
    buf[0] = 0xFF; buf[1] = 0xFF;
    buf[2] = id; buf[3] = 0x04; buf[4] = 0x02;
    buf[5] = addr; buf[6] = len;
    buf[7] = scs_checksum(buf, 8);
    return 8;
}

static int scs_build_write_pos(uint8_t *buf, uint8_t id, uint16_t pos, uint16_t speed) {
    buf[0] = 0xFF; buf[1] = 0xFF;
    buf[2] = id; buf[3] = 0x09; buf[4] = 0x03;
    buf[5] = 0x2A;  // Goal Position addr
    buf[6] = pos & 0xFF; buf[7] = (pos >> 8) & 0xFF;
    buf[8] = 0; buf[9] = 0;  // goal time
    buf[10] = speed & 0xFF; buf[11] = (speed >> 8) & 0xFF;
    buf[12] = scs_checksum(buf, 13);
    return 13;
}

// Receive SCS response with tight UART polling
// Returns: number of bytes received, or 0 on timeout
static int scs_recv_response(uint8_t *buf, int max_len, uint32_t timeout_us) {
    int pos = 0;
    int64_t deadline = esp_timer_get_time() + timeout_us;
    while (esp_timer_get_time() < deadline) {
        int n = uart_read_bytes(UART_NUM, buf + pos, max_len - pos, 0);
        if (n > 0) {
            pos += n;
            // Check if we have a complete SCS packet
            if (pos >= 6) {
                int idx = -1;
                for (int i = 0; i <= pos - 2; i++) {
                    if (buf[i] == 0xFF && buf[i+1] == 0xFF) { idx = i; break; }
                }
                if (idx >= 0 && idx + 3 < pos) {
                    int pkt_len = buf[idx + 3] + 4;  // header(2) + id(1) + len_field(1) + len bytes
                    if (idx + pkt_len <= pos) return pos;  // complete packet
                }
            }
        }
    }
    return pos;
}

static void scs_hist_add(uint32_t rtt_ms) {
    for (int i = 0; i < SCS_HIST_BINS - 1; i++) {
        if (rtt_ms < SCS_HIST_EDGES_MS[i]) { scs_hist[i]++; return; }
    }
    scs_hist[SCS_HIST_BINS - 1]++;
}

static void scs_reset_stats() {
    scs_tx_count = 0;
    scs_rx_ok = 0;
    scs_rx_fail = 0;
    scs_rtt_sum_us = 0;
    scs_rtt_min_us = UINT32_MAX;
    scs_rtt_max_us = 0;
    scs_rtt_count = 0;
    for (int i = 0; i < SCS_HIST_BINS; i++) scs_hist[i] = 0;
    scs_write_target = 2048;
}

// Execute one SCS_TEST cycle (send + optional receive)
static void scs_test_one_cycle() {
    int pkt_len = 0;
    bool expect_response = true;

    switch (scs_sub_mode) {
    case SCS_PING:
        pkt_len = scs_build_ping(scs_pkt_buf, scs_servo_id);
        break;
    case SCS_READ:
        pkt_len = scs_build_read(scs_pkt_buf, scs_servo_id, 0x38, 2);  // Present Position
        break;
    case SCS_WRITE:
        scs_write_target = (scs_write_target >= 3072) ? 1024 : scs_write_target + 50;  // +50 steps (~4.4 deg) per cycle
        pkt_len = scs_build_write_pos(scs_pkt_buf, scs_servo_id, scs_write_target, 3400);
        expect_response = false;  // fire-and-forget
        break;
    case SCS_WRITE_READ:
        scs_write_target = (scs_write_target >= 3072) ? 1024 : scs_write_target + 50;  // +50 steps (~4.4 deg) per cycle
        pkt_len = scs_build_write_pos(scs_pkt_buf, scs_servo_id, scs_write_target, 3400);
        // Will do WRITE then READ in sequence
        break;
    }

    // Flush UART RX before sending
    { uint8_t tmp[256]; uart_read_bytes(UART_NUM, tmp, sizeof(tmp), 0); }

    int64_t t0 = esp_timer_get_time();

    // Send
    uart_write_bytes(UART_NUM, (const char *)scs_pkt_buf, pkt_len);
    uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(10));
    scs_tx_count++;
    tx_byte_count += pkt_len;

    if (scs_sub_mode == SCS_WRITE_READ) {
        // Wait briefly for WRITE to propagate, then send READ
        delayMicroseconds(500);
        int read_len = scs_build_read(scs_pkt_buf, scs_servo_id, 0x38, 2);
        uart_write_bytes(UART_NUM, (const char *)scs_pkt_buf, read_len);
        uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(10));
        scs_tx_count++;
        tx_byte_count += read_len;
        expect_response = true;
    }

    if (!expect_response) {
        // WRITE only: minimal delay and return
        if (frame_gap_us > 0) delayMicroseconds(frame_gap_us);
        else delayMicroseconds(200);
        return;
    }

    // Receive response
    uint8_t resp[32];
    int resp_len = scs_recv_response(resp, sizeof(resp), 50000);  // 50ms timeout

    int64_t t1 = esp_timer_get_time();
    uint32_t rtt_us = (uint32_t)(t1 - t0);

    if (resp_len >= 6 && resp[0] == 0xFF && resp[1] == 0xFF) {
        scs_rx_ok++;
        rx_bytes_total += resp_len;
        scs_rtt_sum_us += rtt_us;
        scs_rtt_count++;
        if (rtt_us < scs_rtt_min_us) scs_rtt_min_us = rtt_us;
        if (rtt_us > scs_rtt_max_us) scs_rtt_max_us = rtt_us;
        scs_hist_add(rtt_us / 1000);
    } else {
        scs_rx_fail++;
    }

    if (frame_gap_us > 0) delayMicroseconds(frame_gap_us);
}

// ============================================================================
// TX functions
// ============================================================================

static size_t send_one() {
    size_t n = 0;
    if (trx_mode == TRX_FRAME_SEQ) {
        n = build_frame(frame_buf, tx_seq, payload_len, (uint8_t)MODE_FRAME_SEQ);
        uart_write_bytes(UART_NUM, (const char *)frame_buf, n);
        tx_seq++;
    } else {
        n = 64;
        uart_write_bytes(UART_NUM, (const char *)const_buf, n);
    }
    tx_frame_count++;
    tx_byte_count += n;
    win_tx_frames++;
    win_tx_bytes += n;
    return n;
}

// ============================================================================
// RX functions (FRAME_SEQ)
// ============================================================================

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

static void process_frame() {
    FrameHeader *h = (FrameHeader *)rx_buf;
    uint16_t pl = h->len;
    uint16_t total = FRAME_HEADER_BYTES + pl + FRAME_CRC_BYTES;
    uint16_t crc_calc = crc16_ccitt(rx_buf, FRAME_HEADER_BYTES + pl);
    uint16_t crc_rx = (uint16_t)rx_buf[total - 2] | ((uint16_t)rx_buf[total - 1] << 8);
    if (crc_calc == crc_rx) {
        seq_update(h->seq);
        win_rx_frames_ok++;
    } else {
        frames_crc_err++;
    }
}

static inline void process_byte_frame(uint8_t b, uint32_t now) {
    rx_bytes_total++;
    win_rx_bytes++;
    switch (rx_state) {
    case RX_HUNT: {
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
            if ((now - hunt_start_ms) >= SYNC_TIMEOUT_MS ||
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

static inline void process_byte_const(uint8_t b) {
    rx_bytes_total++;
    win_rx_bytes++;
    uint8_t exp = 0x00;
    if (trx_mode == TRX_CONST_FF) exp = 0xFF;
    else if (trx_mode == TRX_CONST_55) exp = 0x55;
    if (b != exp) rx_byte_errors++;
}

// ============================================================================
// USB_THROUGH: USB serial <-> RS-485
// ============================================================================

static void usb_tx_flush_rs485() {
    if (usb_tx_pos == 0) return;
    uart_write_bytes(UART_NUM, (const char *)usb_tx_buf, usb_tx_pos);
    usb_tx_pos = 0;
}

// ============================================================================
// Stats / status
// ============================================================================

static void reset_stats() {
    tx_seq = 1;
    tx_frame_count = 0;
    tx_byte_count = 0;
    win_tx_frames = 0;
    win_tx_bytes = 0;
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
    win_rx_bytes = 0;
    win_rx_frames_ok = 0;
    rx_bps_inst = 0;
    rx_fps_inst = 0;
    scs_reset_stats();
}

static double fer() {
    uint64_t denom = frames_rx_ok + frames_crc_err + frames_lost;
    if (denom == 0) return 0.0;
    return (double)(frames_crc_err + frames_lost) / (double)denom;
}

static void show_status() {
    Serial.printf("STATUS: mode=%s dir=%s baud=%lu run=%d report=%d "
                  "tx_frames=%llu tx_bytes=%llu | "
                  "rx_bytes=%llu ok=%llu crc=%llu lost=%llu ro=%llu dup=%llu "
                  "fer=%.4f%% bps=%.0f fps=%.0f\n",
                  TRX_MODE_NAMES[trx_mode], DIR_NAMES[trx_dir], rs485_baud, (int)running, (int)report_enabled,
                  tx_frame_count, tx_byte_count,
                  rx_bytes_total, frames_rx_ok, frames_crc_err,
                  frames_lost, frames_reorder, frames_dup,
                  fer() * 100.0, rx_bps_inst, rx_fps_inst);
}

static void show_verbose() {
    Serial.println("=== VERBOSE ===");
    Serial.printf("  mode        : %s\n", TRX_MODE_NAMES[trx_mode]);
    Serial.printf("  baud        : %lu\n", rs485_baud);
    Serial.printf("  --- TX ---\n");
    Serial.printf("  tx_frames   : %llu\n", tx_frame_count);
    Serial.printf("  tx_bytes    : %llu\n", tx_byte_count);
    Serial.printf("  payload_len : %u\n", payload_len);
    Serial.printf("  gap         : %lu us\n", frame_gap_us);
    Serial.printf("  burst       : %lu\n", burst_count);
    Serial.printf("  --- RX ---\n");
    Serial.printf("  rx_bytes    : %llu\n", rx_bytes_total);
    Serial.printf("  ok          : %llu\n", frames_rx_ok);
    Serial.printf("  crc         : %llu\n", frames_crc_err);
    Serial.printf("  lost        : %llu\n", frames_lost);
    Serial.printf("  ro          : %llu\n", frames_reorder);
    Serial.printf("  dup         : %llu\n", frames_dup);
    Serial.printf("  sync_to     : %llu\n", frames_sync_timeouts);
    Serial.printf("  byte_errors : %llu\n", rx_byte_errors);
    Serial.printf("  FER         : %.5f%%\n", fer() * 100.0);
    Serial.printf("  rx_bps      : %.0f\n", rx_bps_inst);
    Serial.printf("  rx_fps      : %.0f\n", rx_fps_inst);
    if (trx_mode == TRX_SCS_TEST) {
        Serial.printf("  --- SCS_TEST ---\n");
        Serial.printf("  scs_sub     : %s\n", SCS_SUB_NAMES[scs_sub_mode]);
        Serial.printf("  scs_id      : %d\n", scs_servo_id);
        Serial.printf("  scs_tx      : %llu\n", scs_tx_count);
        Serial.printf("  scs_rx_ok   : %llu\n", scs_rx_ok);
        Serial.printf("  scs_rx_fail : %llu\n", scs_rx_fail);
        Serial.printf("  scs_rate    : %.1f%%\n",
                      (scs_rx_ok + scs_rx_fail) > 0
                          ? scs_rx_ok * 100.0 / (scs_rx_ok + scs_rx_fail) : 0);
        if (scs_rtt_count > 0) {
            Serial.printf("  rtt_avg_us  : %llu\n", scs_rtt_sum_us / scs_rtt_count);
            Serial.printf("  rtt_min_us  : %lu\n", scs_rtt_min_us);
            Serial.printf("  rtt_max_us  : %lu\n", scs_rtt_max_us);
            Serial.printf("  rtt_avg_ms  : %.2f\n", (double)(scs_rtt_sum_us / scs_rtt_count) / 1000.0);
        }
        Serial.printf("  RTT histogram:\n");
        const char *hlabels[] = {"<5ms", "5-10ms", "10-15ms", "15-20ms", "20-30ms", "30-50ms", ">=50ms"};
        for (int i = 0; i < SCS_HIST_BINS; i++) {
            Serial.printf("    %-8s : %lu\n", hlabels[i], scs_hist[i]);
        }
    }
}

// ============================================================================
// Serial commands
// ============================================================================

static void print_help() {
    Serial.println("=== RS485-TRX Commands ===");
    Serial.println("  b<baud>  : baud rate (9600..5000000)");
    Serial.println("  m<0-5>   : mode (0=CONST_00 1=FF 2=55 3=FRAME_SEQ 4=USB_THROUGH 5=SCS_TEST)");
    Serial.println("  d<0-2>   : direction (0=TX+RX, 1=TX_ONLY, 2=RX_ONLY)");
    Serial.println("  l<bytes> : payload length (1..240)");
    Serial.println("  g<us>    : inter-frame gap (us, also used for SCS_TEST cycle gap)");
    Serial.println("  n<count> : burst count (0=infinite)");
    Serial.println("  e<bps>   : expected TX bps");
    Serial.println("  I<id>    : SCS_TEST servo ID (default 2)");
    Serial.println("  T<0-3>   : SCS_TEST sub (0=PING 1=READ 2=WRITE 3=WRITE+READ)");
    Serial.println("  p<0|1>   : 1s report OFF/ON (default OFF)");
    Serial.println("  s        : start");
    Serial.println("  x        : stop  (USB_THROUGH: Ctrl+X x3)");
    Serial.println("  r        : reset stats");
    Serial.println("  ?        : status");
    Serial.println("  v        : verbose stats");
    Serial.println("  h        : help");
}

static void init_uart() {
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
    uart_driver_install(UART_NUM, UART_RX_BUF, 4096, 0, NULL, 0);
    uart_param_config(UART_NUM, &cfg);
    uart_set_pin(UART_NUM, RS485_TXD, RS485_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_installed = true;
}

static void handle_serial_command(const char *cmd) {
    switch (cmd[0]) {
    case 'b': {
        uint32_t v = strtoul(cmd + 1, NULL, 10);
        if (v >= 9600 && v <= 5000000) {
            rs485_baud = v;
            Serial.printf("Baud=%lu\n", rs485_baud);
            if (running) { init_uart(); }
        }
        break;
    }
    case 'm': {
        int m = atoi(cmd + 1);
        if (m >= 0 && m < TRX_MODE_COUNT) {
            trx_mode = m;
            usb_tx_pos = 0;
            usb_ctrlx_count = 0;
            Serial.printf("Mode=%s\n", TRX_MODE_NAMES[trx_mode]);
        } else {
            Serial.println("ERR: m expects 0..5");
        }
        break;
    }
    case 'I': {
        int id = atoi(cmd + 1);
        if (id >= 0 && id <= 253) {
            scs_servo_id = (uint8_t)id;
            Serial.printf("SCS ID=%d\n", scs_servo_id);
        } else {
            Serial.println("ERR: I expects 0..253");
        }
        break;
    }
    case 'T': {
        int t = atoi(cmd + 1);
        if (t >= 0 && t <= 3) {
            scs_sub_mode = t;
            Serial.printf("SCS sub=%s\n", SCS_SUB_NAMES[scs_sub_mode]);
        } else {
            Serial.println("ERR: T expects 0..3 (PING/READ/WRITE/WRITE+READ)");
        }
        break;
    }
    case 'l': {
        uint32_t v = strtoul(cmd + 1, NULL, 10);
        if (v >= 1 && v <= FRAME_MAX_PAYLOAD_LEN) {
            payload_len = (uint16_t)v;
            Serial.printf("Payload=%u\n", payload_len);
        }
        break;
    }
    case 'g':
        frame_gap_us = strtoul(cmd + 1, NULL, 10);
        Serial.printf("Gap=%luus\n", frame_gap_us);
        break;
    case 'n':
        burst_count = strtoul(cmd + 1, NULL, 10);
        Serial.printf("Burst=%lu\n", burst_count);
        break;
    case 'd': {
        int d = atoi(cmd + 1);
        if (d >= 0 && d <= 2) {
            trx_dir = d;
            Serial.printf("Dir=%s\n", DIR_NAMES[trx_dir]);
        } else {
            Serial.println("ERR: d expects 0 (TX+RX), 1 (TX_ONLY), 2 (RX_ONLY)");
        }
        break;
    }
    case 'e':
        expected_tx_bps = strtoul(cmd + 1, NULL, 10);
        Serial.printf("ExpectedTxBps=%lu\n", expected_tx_bps);
        break;
    case 'p':
        report_enabled = (cmd[1] == '1');
        Serial.printf("Report=%s\n", report_enabled ? "ON" : "OFF");
        break;
    case 's':
        init_uart();
        reset_stats();
        // Fill const buffer
        {
            uint8_t fill = 0x00;
            if (trx_mode == TRX_CONST_FF) fill = 0xFF;
            else if (trx_mode == TRX_CONST_55) fill = 0x55;
            memset(const_buf, fill, sizeof(const_buf));
        }
        running = true;
        Serial.printf("TRX START mode=%s baud=%lu payload=%u gap=%luus burst=%lu\n",
                      TRX_MODE_NAMES[trx_mode], rs485_baud,
                      payload_len, frame_gap_us, burst_count);
        // SCS_TEST: send Torque ON at start
        if (trx_mode == TRX_SCS_TEST && (scs_sub_mode == SCS_WRITE || scs_sub_mode == SCS_WRITE_READ)) {
            uint8_t torque_pkt[8];
            torque_pkt[0] = 0xFF; torque_pkt[1] = 0xFF;
            torque_pkt[2] = scs_servo_id; torque_pkt[3] = 0x04; torque_pkt[4] = 0x03;
            torque_pkt[5] = 0x28; torque_pkt[6] = 0x01;
            torque_pkt[7] = scs_checksum(torque_pkt, 8);
            uart_write_bytes(UART_NUM, (const char *)torque_pkt, 8);
            uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(10));
            delay(100);
            // Flush any response
            uint8_t tmp[64]; uart_read_bytes(UART_NUM, tmp, sizeof(tmp), 0);
            Serial.printf("SCS Torque ON sent to ID=%d\n", scs_servo_id);
        }
        break;
    case 'x':
        // SCS_TEST: send Torque OFF at stop
        if (trx_mode == TRX_SCS_TEST && uart_installed) {
            uint8_t torque_pkt[8];
            torque_pkt[0] = 0xFF; torque_pkt[1] = 0xFF;
            torque_pkt[2] = scs_servo_id; torque_pkt[3] = 0x04; torque_pkt[4] = 0x03;
            torque_pkt[5] = 0x28; torque_pkt[6] = 0x00;
            torque_pkt[7] = scs_checksum(torque_pkt, 8);
            uart_write_bytes(UART_NUM, (const char *)torque_pkt, 8);
            uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(10));
            Serial.printf("SCS Torque OFF sent to ID=%d\n", scs_servo_id);
        }
        running = false;
        Serial.println("Stopped.");
        break;
    case 'r':
        reset_stats();
        Serial.println("Stats reset.");
        break;
    case '?':
        show_status();
        break;
    case 'v':
        show_verbose();
        break;
    case 'h':
        print_help();
        break;
    default:
        Serial.printf("Unknown: %s\n", cmd);
        print_help();
        break;
    }
}

static void poll_serial() {
    while (Serial.available()) {
        char c = Serial.read();

        // USB_THROUGH mode + running: forward USB serial → RS-485 TX
        if (trx_mode == TRX_USB_THROUGH && running) {
            // Stop detection: 3 consecutive Ctrl+X (0x18)
            if ((uint8_t)c == 0x18) {
                usb_ctrlx_count++;
                usb_tx_last_byte_ms = millis();
                if (usb_ctrlx_count >= 3) {
                    usb_ctrlx_count = 0;
                    if (usb_tx_pos > 0) usb_tx_flush_rs485();
                    running = false;
                    Serial.println("\nUSB_THROUGH STOP");
                    return;
                }
                continue;
            }
            // Flush pending 0x18 as data
            while (usb_ctrlx_count > 0) {
                usb_tx_buf[usb_tx_pos++] = 0x18;
                if (usb_tx_pos >= sizeof(usb_tx_buf)) usb_tx_flush_rs485();
                usb_ctrlx_count--;
            }
            usb_tx_buf[usb_tx_pos++] = (uint8_t)c;
            if (usb_tx_pos >= sizeof(usb_tx_buf)) {
                usb_tx_flush_rs485();
            }
            usb_tx_last_byte_ms = millis();
            continue;
        }

        // Normal command handling
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

// ============================================================================
// setup / loop
// ============================================================================

void setup() {
    M5.begin();
#if !ENABLE_DISPLAY
    M5.Axp.ScreenBreath(0);
#endif
    Serial.begin(115200);
    Serial.printf("\n=== RS485-TRX [%s] ===\n", TRX_MODE_NAMES[trx_mode]);
    print_help();
}

void loop() {
    // M5.update() only when not running (SPI/I2C can block UART RX)
    if (!running) {
        M5.update();
    }
    poll_serial();
    uint32_t now = millis();

    if (!running || !uart_installed) return;

    if (trx_mode == TRX_USB_THROUGH) {
        // USB_THROUGH: flush pending escape bytes + idle flush
        while (usb_ctrlx_count > 0) {
            usb_tx_buf[usb_tx_pos++] = 0x18;
            if (usb_tx_pos >= sizeof(usb_tx_buf)) usb_tx_flush_rs485();
            usb_ctrlx_count--;
            usb_tx_last_byte_ms = now;
        }
        if (usb_tx_pos > 0 && (now - usb_tx_last_byte_ms) >= USB_TX_IDLE_MS) {
            usb_tx_flush_rs485();
        }

        // RS-485 RX → USB serial output
        static uint8_t rxbuf[1024];
        int n = uart_read_bytes(UART_NUM, rxbuf, sizeof(rxbuf), 0);
        if (n > 0) {
            Serial.write(rxbuf, n);
        }
        return;  // no reporting in USB_THROUGH
    }

    // --- SCS_TEST mode ---
    if (trx_mode == TRX_SCS_TEST) {
        if (burst_count != 0 && scs_tx_count >= burst_count) {
            // Torque OFF on burst complete
            if (scs_sub_mode == SCS_WRITE || scs_sub_mode == SCS_WRITE_READ) {
                uint8_t tp[8] = {0xFF, 0xFF, scs_servo_id, 0x04, 0x03, 0x28, 0x00, 0};
                tp[7] = scs_checksum(tp, 8);
                uart_write_bytes(UART_NUM, (const char *)tp, 8);
                uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(10));
            }
            running = false;
            Serial.printf("SCS_TEST done: tx=%llu ok=%llu fail=%llu",
                          scs_tx_count, scs_rx_ok, scs_rx_fail);
            if (scs_rtt_count > 0) {
                Serial.printf(" rtt_avg=%.2fms rtt_min=%.2fms rtt_max=%.2fms",
                              (double)(scs_rtt_sum_us / scs_rtt_count) / 1000.0,
                              scs_rtt_min_us / 1000.0,
                              scs_rtt_max_us / 1000.0);
            }
            Serial.println();
            return;
        }
        scs_test_one_cycle();
        tx_frame_count = scs_tx_count;
        frames_rx_ok = scs_rx_ok;
        frames_crc_err = scs_rx_fail;
        // Fall through to reporting below
        goto reporting;
    }

    // --- FRAME_SEQ / CONST modes ---

    // TX: send frames (skip if RX_ONLY)
    if (trx_dir != DIR_RX && (trx_mode == TRX_FRAME_SEQ || trx_mode <= TRX_CONST_55)) {
        int to_send = 1;
        if (frame_gap_us == 0 && burst_count == 0) {
            to_send = 32;  // batch send only when no gap and infinite burst
        }
        while (to_send-- > 0) {
            if (burst_count != 0 && tx_frame_count >= burst_count) break;
            send_one();
            if (frame_gap_us > 0) {
                uart_wait_tx_done(UART_NUM, pdMS_TO_TICKS(100));
                delayMicroseconds(frame_gap_us);
            }
        }
    }

    // RX: read and process (skip if TX_ONLY)
    if (trx_dir != DIR_TX) {
        static uint8_t rxbuf[2048];
        int n = uart_read_bytes(UART_NUM, rxbuf, sizeof(rxbuf), 0);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                if (trx_mode == TRX_FRAME_SEQ) {
                    process_byte_frame(rxbuf[i], now);
                } else {
                    process_byte_const(rxbuf[i]);
                }
            }
        }
    }

reporting:
    // 1-second windowed stats (TP calculation always runs; serial output only if enabled)
    if (now - win_start_ms >= 1000) {
        uint32_t dt = now - win_start_ms;
        rx_bps_inst = (float)win_rx_bytes * 10.0f * 1000.0f / dt;
        rx_fps_inst = (float)win_rx_frames_ok * 1000.0f / dt;

        if (report_enabled && Serial.availableForWrite() > 120) {
            float tx_fps = win_tx_frames * 1000.0f / dt;
            Serial.printf("TRX mode=%s baud=%lu "
                          "tx=%llu/%.0ffps | "
                          "rx ok=%llu crc=%llu lost=%llu fer=%.4f%% %.0fbps %.0ffps\n",
                          TRX_MODE_NAMES[trx_mode], rs485_baud,
                          tx_frame_count, tx_fps,
                          frames_rx_ok, frames_crc_err, frames_lost,
                          fer() * 100.0, rx_bps_inst, rx_fps_inst);
        }

        win_tx_frames = 0;
        win_tx_bytes = 0;
        win_rx_bytes = 0;
        win_rx_frames_ok = 0;
        win_start_ms = now;
    }
}
