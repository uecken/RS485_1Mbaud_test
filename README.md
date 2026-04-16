# RS-485 / UART 1Mbaud Communication Test Firmware

ESP32 (M5StickC / AtomS3 等) で **RS-485 および UART 直結** の最大ボーレート (1Mbaud) が
実用的に使えるかを検証するためのファームウェア群。

2026-04-09 の実測結果は [doc/202604092200_RS485-baudrate-eval-results.md](doc/202604092200_RS485-baudrate-eval-results.md) 参照。

### 関連ドキュメント

| doc | 内容 |
|-----|------|
| [202604092200_RS485-baudrate-eval-results.md](doc/202604092200_RS485-baudrate-eval-results.md) | **メイン測定結果** (構成 A/B/C/D、ボーレートスイープ) |
| [202604091500_RS485-DE-RE-auto-direction.md](doc/202604091500_RS485-DE-RE-auto-direction.md) | **DE/RE 自動切替の限界解析** (SP485EEN が 500kbaud+ で破綻する根本原因) |
| [202604092230_RS485Unit-vs-ISO485Unit-comparison.md](doc/202604092230_RS485Unit-vs-ISO485Unit-comparison.md) | **M5Stack RS485 Unit vs ISO485 Unit 比較** (ハードウェア選定) |
| [202604092000_RS485-IC-module-full-comparison.md](doc/202604092000_RS485-IC-module-full-comparison.md) | RS-485 IC / モジュール全比較 |
| [202604101200_RS485-multi-vendor-interop.md](doc/202604101200_RS485-multi-vendor-interop.md) | **配線・GND・シールド** 1Mbaud 成功条件 |

## 主要な結論 (doc より)

| 構成 | 1Mbaud 到達性 |
|------|------------|
| **ISO485 (CA-IS3082W) ↔ SP485EEN** + ESP-IDF UART driver + burst TX | **OK** (gap=2ms 以上 または payload 調整で) |
| **SP485EEN ↔ SP485EEN** (M5Stack RS485 Unit) | **NG** (FER 95%+、DE/RE 自動切替の限界) |
| **Grove 直結 5cm シールドなし** | 250kbaud が上限 (物理層限界) |
| Arduino `HardwareSerial.read()` (byte 律速) | 1Mbaud で受信不能 |

→ 1Mbaud は **IC 選択 + FW 設計 + 配線品質** の三条件が揃った時のみ成立。

## フォルダ構成

| フォルダ | 用途 | フレーム |
|---------|------|---------|
| `rs485_tx/` | フル機能版 送信機 | 64B (SOF + seq + CRC16 + 54B payload) |
| `rs485_rx/` | フル機能版 受信機 | HUNT/LEN/BODY/CHECK 状態機械、`s` コマンドで stats |
| `uart_tx/` | 最小版 送信機 | 42B (seq + millis + 32B + CRC16) |
| `uart_rx/` | 最小版 受信機 | 1 byte シフト再同期、固定 baud |
| **`rs485_trx/`** | **tx + rx 統合版** (現行推奨) | 方向制御 `d<0\|1\|2>`、payload 可変 |
| `doc/` | 測定結果レポート | |

## ビルド・アップロード

全 FW は **PlatformIO + Arduino framework (espressif32@6.9.0)** で自己完結。
lib 依存なし。

```bash
# 例: フル版 TX/RX (M5StickC ペア)
cd rs485_tx
pio run -e m5stick-c -t upload --upload-port COMxx   # TX 側

cd ../rs485_rx
pio run -e m5stick-c -t upload --upload-port COMyy   # RX 側

# または統合 trx (推奨)
cd ../rs485_trx
pio run -e m5stick-c -t upload --upload-port COMxx
pio run -e m5stick-c -t upload --upload-port COMyy
```

## シリアル操作 (115200 baud)

共通コマンド:

| cmd | 内容 |
|-----|------|
| `b<baud>` | baud 変更 (例: `b1000000`) |
| `s` | 統計表示 (ok/fail/FER/rx_bps) |
| `r` | 統計リセット |
| `?` | ステータス |

`rs485_trx` のみ:

| cmd | 内容 |
|-----|------|
| `m<0-3>` | モード (CONST / FRAME_SEQ / USB_THROUGH) |
| `d<0\|1\|2>` | 方向 (TX+RX / TX_ONLY / RX_ONLY) |
| `l<1-240>` | payload 長 (推奨 54/140/232) |
| `g<us>` | フレーム間隔 (bursty / throttled) |

## 測定手順例

1. TX: `b1000000` → `l54` → `g0` (burst)
2. RX: `b1000000` → `r` でリセット
3. 10 秒待機
4. RX: `s` → `ok=xxxx fail=yyyy FER=zz%`

## 開発環境

| 項目 | バージョン / 内容 |
|------|----------------|
| ビルドシステム | [PlatformIO](https://platformio.org/) |
| フレームワーク | Arduino framework v2 (ESP-IDF 4.4 ベース) |
| Platform | `espressif32@6.9.0` |
| UART 実装 | **ESP-IDF `driver/uart.h` 直呼び出し** (Arduino `HardwareSerial` では 1Mbaud 受信に取りこぼしあり、doc 参照) |

## ハードウェア構成

| パーツ | 搭載 IC | リンク | 備考 |
|-------|--------|--------|------|
| M5StickC × 2 (または M5AtomS3 × 2) | ESP32 / ESP32-S3 | [M5StickC](https://docs.m5stack.com/en/core/m5stickc) / [AtomS3](https://docs.m5stack.com/en/core/AtomS3) | MCU |
| **M5Stack RS485 Unit** | SP485EEN | [Switch Science](https://www.switch-science.com/products/6554) | **250kbaud まで OK、それ以上は NG** (DE/RE 自動切替の限界) |
| **M5Stack Unit RS485-ISO** | CA-IS3082W (絶縁型) | [M5Stack 公式](https://docs.m5stack.com/ja/unit/iso485) | **1Mbaud 対応** (スルーレート緩和) |
| ツイストペアシールドケーブル 5m | - | - | 250-500kbaud で実用、1Mbaud は gap 調整要 |

## License

[MIT License](LICENSE)

## Related

- [SO101_ESPNOW_control](https://github.com/uecken/SO101_ESPNOW_control) — 本検証結果を応用した SO-101 テレオペ
- [RS-485_wireless](https://github.com/uecken/RS-485_wireless) — 元リポジトリ (ESP-NOW bridge 等)
