// Minimal UART RX: ESP-IDF driver direct (uart_read_bytes batch)
// Pins are defined in platformio.ini per board:
//   M5StickC:    TXD=32 (white), RXD=33 (yellow)
//   M5AtomS3:    TXD=2  (yellow), RXD=1  (white)
//
// Serial commands (115200 baud over USB):
//   b<baud>  : change baud rate
//   s        : start receiving (and reset stats)
//   x        : stop
//   r        : reset stats only
//   ?        : status
//   h        : help
#include <Arduino.h>
#include "driver/uart.h"

#ifndef PIN_TXD
#define PIN_TXD 32
#endif
#ifndef PIN_RXD
#define PIN_RXD 33
#endif

static const int TXD = PIN_TXD;
static const int RXD = PIN_RXD;
static const uart_port_t UART_NUM = UART_NUM_1;
static const int UART_RX_BUF = 8192;

static uint32_t baud_rate = 500000;
static bool running = false;
static bool uart_installed = false;

struct Packet {
  uint32_t seq;
  uint32_t millis_sent;
  uint8_t  payload[32];
  uint16_t crc;
} __attribute__((packed));

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else              crc <<= 1;
    }
  }
  return crc;
}

// Sliding-window receiver with 1-byte shift resync on CRC failure.
static uint8_t ringbuf[sizeof(Packet)];
static int ring_filled = 0;

static uint64_t okCount = 0;
static uint64_t crcErrCount = 0;
static uint64_t seqErrCount = 0;
static uint32_t lastSeq = 0;
static bool firstPacket = true;
static uint64_t totalBytes = 0;

void reset_stats() {
  okCount = 0;
  crcErrCount = 0;
  seqErrCount = 0;
  lastSeq = 0;
  firstPacket = true;
  totalBytes = 0;
  ring_filled = 0;
}

void process_byte(uint8_t b) {
  totalBytes++;
  if (ring_filled < (int)sizeof(Packet)) {
    ringbuf[ring_filled++] = b;
  }
  while (ring_filled >= (int)sizeof(Packet)) {
    Packet pkt;
    memcpy(&pkt, ringbuf, sizeof(Packet));
    uint16_t rxCrc = pkt.crc;
    pkt.crc = 0;
    uint16_t calc = crc16_ccitt((const uint8_t*)&pkt, sizeof(pkt) - sizeof(pkt.crc));
    if (rxCrc == calc) {
      if (!firstPacket) {
        if (pkt.seq != lastSeq + 1) seqErrCount++;
      } else {
        firstPacket = false;
      }
      lastSeq = pkt.seq;
      okCount++;
      ring_filled = 0;
      break;
    } else {
      crcErrCount++;
      memmove(ringbuf, ringbuf + 1, sizeof(Packet) - 1);
      ring_filled--;
    }
  }
}

void init_uart() {
  if (uart_installed) {
    uart_driver_delete(UART_NUM);
    uart_installed = false;
  }
  uart_config_t cfg = {};
  cfg.baud_rate = (int)baud_rate;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity    = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_APB;
  uart_driver_install(UART_NUM, UART_RX_BUF, 0, 0, NULL, 0);
  uart_param_config(UART_NUM, &cfg);
  uart_set_pin(UART_NUM, TXD, RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_installed = true;
  reset_stats();
  Serial.printf("[uart] init baud=%lu txd=%d rxd=%d\n", baud_rate, TXD, RXD);
}

void print_help() {
  Serial.println("=== UART-RX commands ===");
  Serial.println("  b<baud>  : set baud (9600..5000000)");
  Serial.println("  s        : start (reset stats)");
  Serial.println("  x        : stop");
  Serial.println("  r        : reset stats");
  Serial.println("  ?        : status");
  Serial.println("  h        : help");
}

void show_status() {
  Serial.printf("STATUS: baud=%lu running=%d ok=%llu crc=%llu seq=%llu lastSeq=%lu bytes=%llu\n",
                baud_rate, running,
                (unsigned long long)okCount,
                (unsigned long long)crcErrCount,
                (unsigned long long)seqErrCount,
                (unsigned long)lastSeq,
                (unsigned long long)totalBytes);
}

static char cmd_buf[32];
static int cmd_pos = 0;

void handle_cmd(const char* cmd) {
  if (cmd[0] == 'b') {
    uint32_t v = strtoul(cmd + 1, NULL, 10);
    if (v >= 9600 && v <= 5000000) {
      baud_rate = v;
      Serial.printf("baud=%lu\n", baud_rate);
      if (running) init_uart();
    } else {
      Serial.printf("ERR: baud %lu\n", v);
    }
  } else if (cmd[0] == 's') {
    init_uart();
    running = true;
    Serial.println("[start]");
  } else if (cmd[0] == 'x') {
    running = false;
    Serial.println("[stop]");
  } else if (cmd[0] == 'r') {
    reset_stats();
    Serial.println("[stats reset]");
  } else if (cmd[0] == '?') {
    show_status();
  } else if (cmd[0] == 'h') {
    print_help();
  } else {
    Serial.printf("Unknown: %s\n", cmd);
    print_help();
  }
}

void poll_cmd() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmd_pos > 0) {
        cmd_buf[cmd_pos] = 0;
        handle_cmd(cmd_buf);
        cmd_pos = 0;
      }
    } else if (cmd_pos < (int)sizeof(cmd_buf) - 1) {
      cmd_buf[cmd_pos++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== UART-RX (ESP-IDF direct) ===");
  print_help();
}

void loop() {
  poll_cmd();
  static uint32_t lastPrint = 0;

  if (running) {
    uint8_t buf[1024];
    int n = uart_read_bytes(UART_NUM, buf, sizeof(buf), 0);
    if (n > 0) {
      for (int i = 0; i < n; i++) process_byte(buf[i]);
    }
  }

  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    if (running) {
      Serial.printf(
        "RX baud=%lu OK=%llu CRC_ERR=%llu SEQ_ERR=%llu LAST_SEQ=%lu BYTES=%llu\n",
        baud_rate,
        (unsigned long long)okCount,
        (unsigned long long)crcErrCount,
        (unsigned long long)seqErrCount,
        (unsigned long)lastSeq,
        (unsigned long long)totalBytes
      );
    }
  }
}
