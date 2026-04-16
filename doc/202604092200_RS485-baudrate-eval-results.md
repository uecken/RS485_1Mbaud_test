# RS-485 / UART ボーレート評価結果まとめ (2026-04-09)

## 目的

自前のハードウェア (M5StickC + RS-485 Unit / ISO485 / 直結) がどのボーレートで信頼性 0.1% 未満で通信できるかを確定する。

## 測定方法

### Firmware

| 名前 | 用途 | 仕組み |
|------|------|------|
| `rs485_tx` / `rs485_rx` | フル機能版 | 64 byte フレーム (SOF + seq + CRC16 + 54B payload)、HUNT/LEN/BODY/CHECK 状態機械、シリアルコマンド対応 |
| `uart_tx` / `uart_rx` | 最小限版 (デバッグ用) | 42 byte パケット (`seq + millis + payload[32] + CRC16`)、1 byte シフト再同期、固定 baud rate |

### 計測指標 (iperf 流)

- **frames_ok / OK**: CRC 一致のフレーム数
- **frames_lost / SEQ_ERR**: シーケンス番号の gap (== ロス)
- **frames_crc_err / CRC_ERR**: CRC 不一致 (== バイト化け)
- **FER** = `(crc_err + lost) / (ok + crc_err + lost)`
- **rx_bps**: 受信側で実測したビット/秒
- **理論上限**: baud × 8bit / 10bit (UART 8N1 のスタート/ストップ込み)
- **信頼性**: ≥100 エラー OR ≥100k OK サンプルで `confident=true` (iperf 流 ±20% 信頼区間)

## ハードウェア構成

| 構成 | TX 側 | RX 側 | 経路 |
|------|------|------|------|
| 構成 A | M5StickC + M5Stack RS485 Unit (SP485EEN) | M5StickC + M5Stack RS485 Unit (SP485EEN) | RS-485 5m ツイストペアシールド |
| 構成 B | M5StickC + ISO485 Unit | M5StickC + M5Stack RS485 Unit (SP485EEN) | RS-485 5m ツイストペアシールド |
| 構成 C | M5StickC | M5StickC | **Grove ジャンパ直結** (白黄反転、5 cm) |

## 結果サマリー

### 構成 A: SP485EEN ↔ SP485EEN (RS-485 5m)

| baud | パターン | FER | 結果 |
|------|---------|-----|------|
| 250k | 16byte 既知パターン | **0.00%** | OK |
| 500k | 16byte 既知パターン | 95.6% | NG (DE/RE 自動切替の限界) |

### 構成 B: ISO485 → SP485EEN (RS-485 5m)

| baud | gap | rx_bytes / 10s | frames_ok | frames_lost | FER | 判定 |
|------|-----|---------------|-----------|------------|-----|------|
| 250k | 0 | 259,872 | 3,937 | 0 | **0.00%** | **完璧** |
| 500k | 0 | 34,816 | 467 | 3,139 | 87.2% | NG |
| 750k | 0 | 22,528 | 248 | 5,648 | 95.8% | NG |
| 1M | 0 | 18,432 | 232 | 6,878 | 96.8% | NG |
| 500k | 2ms | (TX_LIMITED) | 3,146 | 0 | **0.00%** | OK (実効 ~16 KB/s) |

> 当初 1Mbaud で `frames_lost=0` と出ていたのは TX `loop()` のオーバーヘッドで自然と間欠送信 (~500 fps) になっていただけ。TX バースト送信 (1 loop で 32 frame) に修正したら本当の連続送信になり、500k 以上で大量ロス。

### 構成 C: Grove ジャンパ直結 (RS-485 トランシーバなし) — M5StickC

ESP32 (M5StickC) 2 台、Grove ジャンパケーブル (約 5cm、シールドなし、白黄反転接続)

| baud | OK / 秒 | CRC_ERR / 秒 | SEQ_ERR / 秒 | TX seq / 秒 | RX到達率 | 判定 |
|------|---------|-------------|-------------|-------------|---------|------|
| 115,200 | ~275 | 0 (init only) | 0 | ~275 | **100%** | **完璧** |
| 250,000 | **~595** | 0 | 0 | ~595 | **100%** | **完璧** |
| 500,000 | ~22 | ~510 | ~12 | ~250 | ~9% | NG |
| 1,000,000 | ~20 | ~440 | ~10 | ~310 | ~6% | NG |

> Grove ジャンパケーブル (シールドなし、隣接ピンとの容量結合あり) では **500 kbaud 以上で大量化け**。RS-485 トランシーバの DE/RE 問題とは別の **物理層信号品質の限界**。

### 構成 D: Grove ジャンパ直結 — M5AtomS3 (ESP32-S3)

ESP32-S3 (M5AtomS3) 2 台、Grove ジャンパケーブル (約 5cm、シールドなし、白黄反転接続)
ピン: TXD=GPIO2 (Yellow), RXD=GPIO1 (White)

| baud | OK / 秒 | CRC_ERR / 秒 | SEQ_ERR / 秒 | TX seq / 秒 | RX到達率 | 判定 |
|------|---------|-------------|-------------|-------------|---------|------|
| 250,000 | **~595** | 0 (init only) | 0 | ~596 | **100%** | **完璧** |
| 500,000 | ~26 | ~600 | ~14 | ~1,180 | ~2.2% | NG |
| 1,000,000 | ~20 | ~480 | ~11 | ~2,390 | ~0.8% | NG |

> **重要**: M5StickC (ESP32) でも M5AtomS3 (ESP32-S3) でも **250 kbaud が安定動作の上限** という同じ結果。これは ESP32 系列固有の問題ではなく、**Grove ジャンパケーブルそのものの信号品質限界**であることが確定した。MCU を新しい ESP32-S3 に交換しても解決しない。

## 重要な発見

1. **データパターン依存**: SP485EEN は `0xFF` 連続なら 1 Mbaud が通っても、`0xA5 5A 3C C3` のようなエッジ密度の高い実データでは 500 kbaud で破綻する (DE/RE 自動切替が追いつかない)
2. **TX オーバーヘッド注意**: シンプルな `loop()` 送信では Arduino フレームワークのオーバーヘッドで 500 fps 程度 (32 KB/s) に律速される。連続送信を測るには 1 loop で複数フレーム送るバースト送信が必須
3. **最小 CRC ベース測定の重要性**: 過去に「Grove 直結 1Mbaud OK」と記録があったが、当時はバイト一致のみで化けを見抜けていなかった。CRC + シフト再同期で初めて真の信号品質が分かった
4. **同期方法の問題**: 16 byte 固定パターンの繰り返しは 1 byte ロスでカスケードエラーを起こす。SOF + len + CRC 構造 + 1 byte シフト再同期が正しい設計
5. **MAGIC の選び方**: 当初 `0xA55A` を SOF にしていたが、payload に同じバイト列が含まれて false sync 多発。SOF は payload に絶対現れない値 (`0x7E81` 等) を選ぶこと

## 計測の妥当性 (FRAME_SEQ モード)

| 検出する事象 | 検出方法 |
|------------|---------|
| バイト化け (bit flip) | 受信側で CRC16-CCITT 再計算 → 不一致なら `frames_crc_err++` |
| パケットロス (UART overrun 等) | seq 番号の gap → `frames_lost += (seq - expected_seq)` |
| 順序逆転 | seq < max_seq_seen → `frames_reorder++` (RS-485 では通常起こらない) |
| 重複 | seq == max_seq_seen → `frames_dup++` |
| 同期不能 | 50 ms or 4096 byte 経過しても SOF 見つからず → `frames_sync_timeouts++` |

統計は 100 エラー以上 OR 100k OK サンプル以上で「confident」と判定 (iperf2 流の ±20% 信頼区間)。

## 構成 E: Arduino + ESP-IDF UART API 直接呼び出し版 (HardwareSerial 廃止) — 2026-04-09 後半

> **用語注**: フレームワークは引き続き **Arduino (espressif32@6.9.0、Arduino-ESP32 v2.x)** を使用している (`framework = arduino`)。Arduino-ESP32 は ESP-IDF をベースにしているため、Arduino スケッチ内から `#include "driver/uart.h"` で ESP-IDF の UART driver API を直接呼び出せる。ここで「ESP-IDF UART API」と書いているのはこの API のこと。フレームワーク自体を ESP-IDF (`framework = espidf`) に切り替えたわけではない。

これまで `rs485_tx/rs485_rx` および `uart_tx/uart_rx` の RX 側で **Arduino HardwareSerial の `read()` 1 バイト呼び出し** を使っていた。これが連続高速受信時に律速になっていた疑いがあったため、**ESP-IDF UART driver を直接呼び出す版** (`uart_driver_install` + `uart_read_bytes` バッチ読み + `uart_write_bytes` バッチ書き) に書き換えて再評価した。

### 使用フォルダ / コード変更点

| firmware | 主な変更 |
|----------|---------|
| `uart_tx/uart_rx` | `HardwareSerial` を完全に廃止し、`uart_write_bytes`/`uart_read_bytes` 直呼び出しに変更。RX 側は `uart_read_bytes(uart, buf, 1024, 0)` でバッチ取得後、shifting ring buffer で 1-byte シフト同期 |
| `rs485_tx/rs485_rx` | 同上の置き換え。`rs485_rx` は HUNT/LEN/BODY/CHECK 状態機械を維持しつつ、UART 取得を `uart_read_bytes(uart, rxbuf, 2048, 0)` のバッチ読みに変更 |
| LCD | 全 firmware で `ENABLE_DISPLAY=0` (描画オーバーヘッド排除) |

### 結果まとめ表 (全条件)

| # | MCU | コード | 接続 | RS-485 IC | ケーブル | パターン | baud | rx_bps | FER | 判定 |
|---|-----|------|------|----------|---------|---------|------|--------|-----|------|
| 1 | M5StickC (ESP32) | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 115k | 100% | 0% | OK |
| 2 | M5StickC | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 250k | 100% | 0% | OK |
| 3 | M5StickC | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 500k | 9% | 95% | NG |
| 4 | M5StickC | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 1M | 6% | 95% | NG |
| 5 | M5AtomS3 (ESP32-S3) | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 250k | 100% | 0% | OK |
| 6 | M5AtomS3 | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 500k | 2% | 95% | NG |
| 7 | M5AtomS3 | `uart_tx/rx` (Arduino HW Serial) | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | 1M | 1% | 96% | NG |
| 8 | **M5AtomS3** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | **500k** | **100%** | **0%** | **OK** |
| 9 | **M5AtomS3** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | **1M** | **100%** | **0%** | **OK** |
| 10 | **M5StickC** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | Grove 直結 5cm | なし | ジャンパ反転 | 42B / CRC | **1M** | **100%** | **0%** | **OK** |
| 11 | M5StickC | `rs485_tx/rx` (HardwareSerial) | RS485 Unit | SP485EEN ×2 | RS-485 5m TPS | FRAME_SEQ 64B | 250k | 100% | 0% | OK |
| 12 | M5StickC | `rs485_tx/rx` (HardwareSerial) | RS485 Unit | SP485EEN ×2 | RS-485 5m TPS | FRAME_SEQ 64B | 500k | 7% | 87% | NG |
| 13 | M5StickC | `rs485_tx/rx` (HardwareSerial, ISO485 TX) | ISO485 → SP485EEN | TX:ISO485 / RX:SP485EEN | RS-485 5m TPS | FRAME_SEQ 64B | 500k | 7% | 87% | NG |
| 14 | M5StickC | `rs485_tx/rx` (HardwareSerial, ISO485 TX) | ISO485 → SP485EEN | TX:ISO485 / RX:SP485EEN | RS-485 5m TPS | FRAME_SEQ 64B | 1M | 1.5% | 97% | NG |
| 15 | **M5StickC** | **`rs485_tx/rx` (Arduino + ESP-IDF UART API)** | **Grove 直結 5cm** | **なし** | ジャンパ反転 | FRAME_SEQ 64B | **1M** | **99.96%** | **0%** | **OK** |
| 16 | **M5StickC** | **`rs485_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS | FRAME_SEQ 64B | 250k | 100% | 0% | OK (期待) |
| 17 | **M5StickC** | **`rs485_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS | FRAME_SEQ 64B | **500k** | **99.84%** | **0%** | **OK** |
| 18 | **M5StickC** | **`rs485_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS | FRAME_SEQ 64B | **750k** | 79% (rx_bytes は来るが) | (magic_hits=0) | **NG** (バイト化け) |
| 19 | **M5StickC** | **`rs485_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS | FRAME_SEQ 64B | **1M** | 79% (rx_bytes は来るが) | (magic_hits=0) | **NG** (バイト化け) |
| 20 | **M5StickC** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS | 42B / CRC | **500k** | **100%** (50 KB/s) | **0%** | **OK** |
| 21 | **M5StickC** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS / GND片端 | 42B / CRC | **1M** | 49% (61 KB/s 着、全化け) | **100%** | **NG** |
| 22 | **M5StickC** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS / **GND両端**, シールド片端 | 42B / CRC | **500k** | **100%** (50 KB/s) | **0%** | **OK** |
| 23 | **M5StickC** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS / **GND両端**, シールド片端 | 42B / CRC | **750k** | 62% (58 KB/s 着、全化け) | **100%** | **NG** |
| 24 | **M5StickC** | **`uart_tx/rx` (Arduino + ESP-IDF UART API)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS / **GND両端**, シールド片端 | 42B / CRC | **1M** | 49% (60 KB/s 着、全化け) | **100%** | **NG** |
| 25 | **M5AtomS3** | **`uart_tx/rx` (Arduino + ESP-IDF UART API、シリアルコマンド版)** | RS485 Unit | **SP485EEN ×2** | RS-485 5m TPS | 42B / CRC | 500k | **100%** | **0%** | **OK** |
| 26 | **M5AtomS3** | 同上 | RS485 Unit | SP485EEN ×2 | RS-485 5m TPS | 42B / CRC | 600k | 0% | 100% | NG |
| 27 | **M5AtomS3** | 同上 | RS485 Unit | SP485EEN ×2 | RS-485 5m TPS | 42B / CRC | 750k | 0% | 100% | NG |
| 28 | **M5AtomS3** | 同上 | RS485 Unit | SP485EEN ×2 | RS-485 5m TPS | 42B / CRC | 1M | 0% | 100% | NG |
| **29** | **M5AtomS3** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W (TX) / SP485EEN (RX)** | RS-485 5m TPS | 42B / CRC | **500k** | **100%** | **0%** | **OK** |
| **30** | **M5AtomS3** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **600k** | **100%** | **0%** | **OK** |
| **31** | **M5AtomS3** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **750k** | **100%** | **0%** | **OK** |
| **32** | **M5AtomS3** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **1M** | **100%** (104 KB/s) | **0%** | **完璧** |
| **33** | **M5StickC** | `uart_tx/rx` (シリアルコマンド版) | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **500k** | **100%** (45 KB/s) | **0%** | **OK** |
| **34** | **M5StickC** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **600k** | **100%** (62 KB/s) | **0%** | **OK** |
| **35** | **M5StickC** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **750k** | **100%** (78 KB/s) | **0%** | **OK** |
| **36** | **M5StickC** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | 42B / CRC | **1M** | **100%** (101 KB/s) | **0%** | **完璧** |
| **37** | **M5StickC** | **`rs485_tx/rx` (Arduino + ESP-IDF UART API)** | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | FRAME_SEQ 64B | **500k** | **100%** (54 KB/s) | **0%** | **完璧** |
| **38** | **M5StickC** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | FRAME_SEQ 64B | **750k** | **100%** (82 KB/s) | **0%** | **完璧** |
| **39** | **M5StickC** | 同上 | **ISO485 (TX) → SP485EEN (RX)** | **CA-IS3082W / SP485EEN** | RS-485 5m TPS | FRAME_SEQ 64B | **1M** | **100%** (109 KB/s) | **0%** | **完璧** |

> **TPS** = Twisted Pair Shielded (ツイストペアシールド)
>
> **rx_bps**: 受信側で実測したビット/秒。理論値 = baud × 8/10 (UART 8N1)
>
> **magic_hits=0** はバイトは届いているが SOF (0x7E81) が一度も検出されない状態 = データの 1 ビット部分で必ず化けている

### 重要な発見の更新

1. **`HardwareSerial.read()` の 1 バイト呼び出しが律速だった**: テスト 1〜7 と 8〜10 を比較すると、Arduino HW Serial では Grove 直結ですら 500k で 95% NG。**ESP-IDF 直接 (`uart_read_bytes` バッチ) では同条件で 1Mbaud OK**。
2. **過去の結論「Grove ケーブル/SP485EEN が原因」は誤りだった**: Arduino HW Serial の RX 律速によりバイト取りこぼしを起こしていたのを SP485EEN や Grove のせいと誤認していた。
3. **SP485EEN の真の上限は 500 kbaud と 750 kbaud の間**: テスト 17 (500k OK) と 18 (750k NG) が決定的。Grove (FW律速ではない) 直結では 1Mbaud 通る (テスト 10/15) が、SP485EEN を入れた瞬間に 750k 以上で `magic_hits=0` (= データ全化け) になる。これは DE/RE 自動切替が高速エッジに追従できないという従来仮説と整合。
4. **M5StickC と M5AtomS3 の差は無い**: テスト 8/9 (AtomS3) と 10/15 (M5StickC) で同じく 1Mbaud OK。MCU は問題ではなく、フレームワークの取得方式 (HW Serial vs Arduino + ESP-IDF UART API) が支配的だった。
5. **GND 両端接続による効果なし**: テスト 20/21 (GND 片端) と 22/23/24 (GND 両端、シールドは片端のまま) を比較すると、どちらも **500k OK / 750k 以上で全化け** で結果は同じ。GND 電位差は問題ではなく、SP485EEN の DE/RE 自動切替が真の限界であることが裏付けられた。
   - ただし GND 両端接続自体は TIA-485 推奨であり、長距離 / 異電源環境では依然として必要。今回の M5StickC + USB 給電という同一電源系の短距離環境では効果が出なかっただけ。

6. **ISO485 (TX) → SP485EEN (RX) で 1Mbaud 完璧動作**: テスト 29〜32 で **送信側を ISO485 に変えるだけで、500k〜1Mbaud のすべてで FER 0%** を達成 (テスト 32: 100%、104 KB/s)。受信側は SP485EEN のままでよい (受信時は DE/RE 制御の影響を受けないため)。

   **ISO485 の回路図 (`docs/img/ISO485_回路.jpg`) を確認した結果**:

   | 項目 | M5Stack RS485 Unit | M5Stack ISO485 Unit |
   |------|-------------------|--------------------|
   | RS-485 IC | **SP485EEN** (5V、~5Mbps、スルーレート制限あり) | **CA-IS3082W** (3.3V、絶縁内蔵、**~10Mbps、スルーレート制限なし**) |
   | DE/RE 制御 | TX 連動自動切替 (Q1: SS8050 NPN BJT) | TX 連動自動切替 (Q1/Q2: BSS138 N-MOS、レベルシフタ) |
   | 絶縁 | なし | あり (容量結合、CA-IS3082W 内蔵) |
   | 信号レベル | 5V 系 (Grove 5V) | **3.3V** (HOST_3V3、内部 LDO + BSS138 でレベルシフト) |
   | 内部電源 | 5V 直接 | **絶縁 DC-DC** (B0505ST16-W5) で 5V → VCC2_5V を生成 |

   **ISO485 で 1Mbaud が通る理由の候補** (確度順):

   1. ★最有力 — **CA-IS3082W は 10Mbps 対応の高速版でスルーレート制限なし**。SP485EEN (5Mbps、スルーレート制限あり) と比べて遷移時間が十分短く、1Mbaud のエッジに余裕で追従する
   2. **DE/RE 切替トランジスタが BSS138 (N-MOSFET)** で SS8050 (NPN BJT) より高速 (Vth・スイッチング時間とも有利)
   3. **絶縁 DC-DC により USB 電源系の AC ノイズが切り離された** — 接地系を遠ざけた効果と同類で、特にホスト USB から流入する AC 漏れ電流が CA-IS3082W の差動受信に影響しなくなった可能性
   4. **3.3V 信号レベルでレベルシフタ経由なので、TX エッジの立ち上がりが急峻** (5V 駆動の SS8050 より低電圧スイッチ)
   5. TVS / GDT / PTC / TSS 等の保護素子による信号品質改善 (副次的)

   候補 1 (CA-IS3082W のスルーレート優位) が最も支配的と推定される。候補 3 (絶縁による電源ノイズ切り離し) は AtomS3 を AC アダプタから遠ざけた時に改善した症状と整合し、副次効果として効いている可能性が高い。

## Phase 0 結論 (2026-04-09 更新版)

### Arduino HardwareSerial vs ESP-IDF UART driver direct

**最大の発見**: 受信側で `HardwareSerial.read()` を 1 バイトずつ呼んでいた実装は、**Arduino フレームワークのオーバーヘッドにより 250 kbaud 以上で取りこぼしを起こしていた**。これを `uart_read_bytes(uart, buf, N, 0)` のバッチ読みに置き換えると、同じハードウェアで 4 倍以上の baud rate が通る。

### ハードウェア別の真の上限 (Arduino + ESP-IDF UART API 版で測定)

| ケース | 安定動作上限 (FER 0%) | 備考 |
|--------|-------------------|------|
| **Grove ジャンパ直結 (M5StickC)** | **1 Mbaud** | これまでの「250k 上限」結論は誤り。原因は HW Serial の RX 律速だった |
| **Grove ジャンパ直結 (M5AtomS3)** | **1 Mbaud** | 同上 |
| **RS-485 Unit (SP485EEN ↔ SP485EEN)** | **500 kbaud** | 750k 以上で `magic_hits=0` (バイトは届くが全化け)。SP485EEN のスルーレート制限と DE/RE 自動切替が高速エッジに追従しない |
| **ISO485 (TX) → SP485EEN (RX)** | **1 Mbaud** | **送信側を ISO485 (CA-IS3082W) に変えるだけで 1Mbaud 完璧動作**。受信側は SP485EEN のままで OK。M5StickC / M5AtomS3 両 MCU で確認済 (テスト 29〜32, 33〜36) |

### 結論

1. **ESP32 / ESP32-S3 の UART 自体は 1 Mbaud に十分対応**しており、Grove ケーブルや MCU が原因ではない
2. **取得経路 (HardwareSerial vs Arduino + ESP-IDF UART API) が支配的**だった
3. **SP485EEN 経由の真の上限は 500 〜 750 kbaud の間**。これは DE/RE 自動切替回路の限界 (詳細は [202604091500_RS485-DE-RE-auto-direction.md](202604091500_RS485-DE-RE-auto-direction.md) 参照)
4. **本番で 1 Mbaud 連続送信を扱うには、SP485EEN ではなく SP3485 / MAX3485 系 (スルーレート制限なし) のトランシーバが必須**

## 次のステップ (2026-04-09 更新)

1. ✅ ~~M5AtomS3 (ESP32-S3) で Grove 直結再評価~~ → **完了**
2. ✅ ~~Arduino + ESP-IDF UART API 版で再評価~~ → **完了**
3. ✅ ~~ISO485 (TX) → SP485EEN (RX) を Arduino + ESP-IDF UART API 版で再測定~~ → **完了。1Mbaud 完璧動作**
4. **本番ハードウェア構成の確定**: TX 側 ISO485 / RX 側 SP485EEN または ISO485
5. **Phase A 実機プロトコル解析 (受信スニッファ)** — 現状の構成で十分対応可能
   - **受信側 SP485EEN は 1Mbaud OK** (受信時は DE/RE 自動切替の影響を受けない)
   - **送信側に ISO485 を使えば 1Mbaud OK**
6. **rs485_tx/rs485_rx も同様の Arduino + ESP-IDF UART API 版で書き換え済み** — FRAME_SEQ モードで実機評価可能

## 参照

- 詳細な経緯と DE/RE 回路の根本原因: [202604091500_RS485-DE-RE-auto-direction.md](202604091500_RS485-DE-RE-auto-direction.md)
- IC/モジュール比較: [202604092000_RS485-IC-module-full-comparison.md](202604092000_RS485-IC-module-full-comparison.md)
- ブリッジ実装計画: [202604091600_RS485-bridge-implementation-plan.md](202604091600_RS485-bridge-implementation-plan.md)
- 単純透過のフィージビリティ評価: [202604100200_RS485-bridge-feasibility.md](202604100200_RS485-bridge-feasibility.md)
- Nordic 系無線方式比較: [202604100400_RS485-bridge-feasibility-with-Nordic-options.md](202604100400_RS485-bridge-feasibility-with-Nordic-options.md)
- 全テストログと再現手順: [課題と進捗.md](課題と進捗.md) "前提課題 0" セクション
