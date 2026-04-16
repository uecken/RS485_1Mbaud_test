# RS-485 / UART 1Mbaud Communication Test Firmware

ESP32 (M5StickC / AtomS3 等) で **RS-485 および UART 直結** の最大ボーレート (1Mbaud) が
実用的に使えるかを検証するためのファームウェア群。

2026-04-09 の実測結果は [doc/202604092200_RS485-baudrate-eval-results.md](doc/202604092200_RS485-baudrate-eval-results.md) 参照。

## 主要な結論

**1Mbaud 成否の決定的要因 = IC の「スルーレート制限」の有無**

| 構成 | 1Mbaud 到達性 | 根本理由 |
|------|------------|---------|
| **ISO485 (CA-IS3082W) 搭載 M5Stack Unit RS485-ISO** | **✓ OK** | **スルーレート制限なし** (20Mbps 定格) |
| **SP485EE 搭載 M5Stack RS485 Unit** | **✗ NG** (FER 95%+) | **スルーレート制限あり** (立上り 20ns → 波形なまり + DE/RE 誤切替) |
| Grove 直結 5cm シールドなし | 250kbaud が上限 | 物理層 (容量結合・反射) |

### FW 側

| 通信パターン | Arduino `HardwareSerial` | ESP-IDF `driver/uart.h` |
|------------|------------------------|------------------------|
| 連続ストリーム (ブリッジ透過等、duty ~100%) | **NG** (byte 律速、バッファ overflow) | **OK** (4KB buf + IRAM ISR) |
| Request-Response (SCS サーボ READ 等、duty ~5%) | **OK** (十分な gap あり) | **OK** |

→ 1Mbaud は **IC 選択 + FW 設計 + 配線品質** の三条件が揃った時のみ連続動作。

## スルーレート制限とは

**RS-485 ドライバ IC が意図的に信号エッジ (立上り/立下り) を「遅くしている」設計**。

### 物理的な意味

```
理想 (立上り 0ns):  __┃‾‾‾┃___┃‾‾‾┃___      クリーンな矩形
CA-IS3082W (~5ns):  __╱‾‾‾╲___╱‾‾‾╲___      ほぼ矩形
SP485EE (~20ns):    __╱───╲__╱───╲__        台形 (頂点到達前に次が来る)
```

### なぜ「制限」するのか (設計意図)

| 目的 | 効果 |
|------|------|
| EMI (電磁放射) 低減 | 急峻エッジ = 高調波多 → ケーブルから放射。緩めて抑制 |
| 反射 (エコー) 低減 | 短いケーブル・終端なしでも安定 |
| クロストーク低減 | 隣接信号への誘導結合を抑制 |
| EMC 認証通過 | FCC/CE の放射規制をクリアしやすい |

低速用途 (500kbps 以下) には有効。**1Mbps を超える用途には足枷**になる。

### 1Mbaud で問題になる理由

| baud | 1 bit 時間 | SP485EE 立上り 20ns の占有率 |
|------|----------|---------------------------|
| 115,200 | 8.7 μs | 0.23% (問題なし) |
| 500 k | 2 μs | 1% (まだ OK) |
| **1 M** | **1 μs** | **2% + 前後テール** → **ケーブル容量が加わると頂点到達前に次ビット** |

ケーブル容量 (~25pF/m × 5m = 125pF) が加わると RC 時定数で更に遅延 → **ビット間干渉 (ISI)** → 受信コンパレータが 0/1 判定を誤る → FER 爆発。

### IC 比較

| IC | スルーレート制限 | 立上り時間 | 最大 baud | 使用モジュール |
|----|--------------|----------|----------|-------------|
| **SP485EE** | **あり** | ~20ns | **500kbps 実用上限** | M5Stack RS485 Unit |
| **CA-IS3082W** | **なし** | <5ns | **20Mbps 定格** (1Mbaud 実測 0% FER) | M5Stack Unit RS485-ISO |

詳細: [doc/202604092000_RS485-IC-module-full-comparison.md](doc/202604092000_RS485-IC-module-full-comparison.md)

## DE/RE 自動切替との関係

本プロジェクトで使う M5Stack Unit 2 種は **いずれも基板上で DE/RE 自動切替**を持つ:
- SP485EE モジュール: トランジスタ (Q1 SS8050) による RC 積分回路
- CA-IS3082W モジュール: BSS138 MOSFET による切替

両者とも自動切替だが、**主要制約は IC のスルーレート**。
DE/RE 回路の誤動作は **副次的問題** (SP485EE で `gap=2ms` を入れれば 500kbaud で FER 0% になるのがその証拠、連続送信時に切替検出が放電で誤作動するだけ)。

詳細: [doc/202604091500_RS485-DE-RE-auto-direction.md](doc/202604091500_RS485-DE-RE-auto-direction.md)

## 関連ドキュメント

| doc | 内容 |
|-----|------|
| [202604092200_RS485-baudrate-eval-results.md](doc/202604092200_RS485-baudrate-eval-results.md) | **メイン測定結果** (構成 A/B/C/D、ボーレートスイープ) |
| [202604092000_RS485-IC-module-full-comparison.md](doc/202604092000_RS485-IC-module-full-comparison.md) | **IC / モジュール全比較** (スルーレート制限の有無で切り分け) |
| [202604091500_RS485-DE-RE-auto-direction.md](doc/202604091500_RS485-DE-RE-auto-direction.md) | DE/RE 自動切替回路の解析 (副次的制約) |
| [202604092230_RS485Unit-vs-ISO485Unit-comparison.md](doc/202604092230_RS485Unit-vs-ISO485Unit-comparison.md) | M5Stack RS485 Unit vs Unit RS485-ISO 比較 |
| [202604101200_RS485-multi-vendor-interop.md](doc/202604101200_RS485-multi-vendor-interop.md) | 配線・GND・シールド (1Mbaud 成功条件) |

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

全 FW は **PlatformIO + Arduino framework (espressif32@6.9.0)** で自己完結。lib 依存なし。

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
| UART 実装 | **ESP-IDF `driver/uart.h` 直呼び出し** (連続ストリームを 1Mbaud で取りこぼしなく受けるため、4KB buffer + IRAM ISR) |

## ハードウェア構成

| パーツ | 搭載 IC | リンク | 備考 |
|-------|--------|--------|------|
| M5StickC × 2 (または M5AtomS3 × 2) | ESP32 / ESP32-S3 | [M5StickC](https://docs.m5stack.com/en/core/m5stickc) / [AtomS3](https://docs.m5stack.com/en/core/AtomS3) | MCU |
| **M5Stack RS485 Unit** | SP485EE (スルーレート制限あり) | [Switch Science](https://www.switch-science.com/products/6554) | **500kbaud 実用上限** (1Mbaud は FER 95%+) |
| **M5Stack Unit RS485-ISO** | CA-IS3082W (絶縁型、スルーレート制限なし) | [M5Stack 公式](https://docs.m5stack.com/ja/unit/iso485) | **1Mbaud 対応 (FER 0% 実測)** |
| ツイストペアシールドケーブル 5m | - | - | GND 両端接続必須、シールド片端 |

## License

[MIT License](LICENSE)

## Related

- [SO101_ESPNOW_control](https://github.com/uecken/SO101_ESPNOW_control) — 本検証結果を応用した SO-101 テレオペ
- [RS-485_wireless](https://github.com/uecken/RS-485_wireless) — 元リポジトリ (ESP-NOW bridge 等)
