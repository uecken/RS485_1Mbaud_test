# M5Stack Unit RS485 と Unit RS485-ISO の詳細比較

> **命名**: 本書の対象は M5Stack の 2 製品、**Unit RS485** (U094、型番 RS485 Unit / 非絶縁) と **Unit RS485-ISO** (絶縁型、CA-IS3082W 搭載)。後者は **「Unit RS485-ISO」** が正式名称 (旧称 Unit RS485-ISO を本書では統一)。
> 搭載 IC は 2026-04-10 に実機写真で物理確認済み: [img/ISO485_IC_marking.jpg](img/ISO485_IC_marking.jpg)
> - 絶縁トランシーバ: **CHIPANALOG CA-IS3082W** (10 Mbps, ±2.5 kV 絶縁)
> - 絶縁 DC-DC: **MORNSUN B0505ST16-W5** (5V→5V isolated, 1W, ロット 2442D = 2024 年 42 週)

## 結論 (先に)

| 用途 | 推奨モジュール |
|------|--------------|
| **送信側 (1 Mbaud 連続)** | **Unit RS485-ISO** (CA-IS3082W) — RS485 Unit (SP485EEN) では 500 kbaud が上限 |
| **受信側 (1 Mbaud 連続)** | **どちらでも OK** — 受信時は DE/RE 自動切替の影響を受けないため SP485EEN でも 1 Mbaud 受信可能 |
| **長距離 (>10m) / ノイズ多い環境** | **Unit RS485-ISO** — 絶縁により GND ノイズ・サージから保護 |
| **コスト優先 / 短距離 / 低速 (<500 kbaud)** | **RS485 Unit** で十分 |

## 比較表

| 項目 | M5Stack Unit RS485 (U094、非絶縁) | M5Stack Unit RS485-ISO (絶縁) |
|------|-------------------------|-------------------|
| 製品ページ | https://docs.m5stack.com/ja/unit/rs485 | https://docs.m5stack.com/ja/unit/iso485 |
| **トランシーバ IC** | **SP485EEN-L/TR** (Sipex/MaxLinear) | **CA-IS3082W** (Chipanalog、絶縁内蔵) |
| **IC 仕様データレート** | 5 Mbps | **10 Mbps** |
| **スルーレート制限** | **あり** (~30 ns 立ち上がり) | **なし** (~4 ns 程度) |
| **絶縁** | なし | **あり** (容量結合、CA-IS3082W 内蔵) |
| 絶縁耐圧 | - | 約 5 kV |
| **絶縁側電源** | 5V 直接 | **絶縁 DC-DC** (B0505ST16-W5) で 5V → VCC2_5V を生成 |
| **ホスト側信号レベル** | 5V (Grove 5V 系) | **3.3V** (内部 LDO で 5V→3.3V を生成、BSS138 でレベルシフト) |
| **ホスト側電源** | 5V (Grove VCC) | 5V (Grove VCC、内部で絶縁 DC-DC + 3.3V LDO) |
| **DE/RE 制御方式** | TX 連動自動切替 | TX 連動自動切替 |
| DE/RE 切替トランジスタ | **Q1: SS8050** (NPN BJT) | **Q1/Q2: BSS138** (N-MOS、レベルシフタ兼用) |
| 入力プルアップ抵抗 | R3 (4.7 kΩ) | R7 (4.7 kΩ) |
| 入力プルダウン抵抗 | - | R9 (15 kΩ) + R13 (100 kΩ) 系 |
| **保護素子** | TVS (SRV05-4) のみ | **TVS (PESD5V0/SRV05-4) + GDT (SMD4532-075) + PTC ヒューズ + TSS (P0080TA)** で堅牢 |
| **A/B 終端** | なし | なし |
| **A/B プルアップ/プルダウン** | あり (R1/R5 = 4.7 kΩ) | あり (R8/R10 = 4.7 kΩ) |
| Grove コネクタ | あり (Port C / UART) | あり (Port C / UART) |
| 実測 1 Mbaud 連続送信 (FRAME_SEQ) | **NG** (550 〜 700 kbaud で破綻) | **OK** (1 Mbaud で FER 0%) |
| 実測 500 kbaud 連続送信 | **OK** (FER 0%) | **OK** (FER 0%) |

## 回路上のキーポイント

### M5Stack RS485 Unit (U094) の DE/RE 制御回路

```
       +5V
        │
       R3 (4.7 kΩ)
        │
        ├─── DE/RE (pin 2,3 直結)
        │
   Q1 collector
        │
   Q1 (SS8050 NPN)
        │
       GND

TX ── R4 (1 kΩ) ── Q1 base
```

- TX = LOW → Q1 OFF → DE/RE = HIGH (R3 プルアップ) → **送信モード**
- TX = HIGH → Q1 ON → DE/RE = LOW → 受信モード

データの '1' ビットが多いほど DE/RE が頻繁に LOW に落ち、SP485EEN が送信無効になる → 高速通信で破綻。

### M5Stack Unit RS485-ISO の DE/RE 制御回路

```
HOST_3V3                     CA-IS3082W (U1)
   │                           ┌──────────┐
  R11(100k)                  │ RO  (1)  │── UART_Rx
   │                          │ RE  (4)  │┐ (DE/RE 結線)
HOST_3V3 ─ R7(4.7k) ──────────│ DE  (5)  │┘
   │                          │ DI  (3)  │── UART_Tx (経由 Q2)
  R13(100k)                   │ VCC1     │── HOST_3V3
   │                          │ GND1     │── HOST_GND
   │                          └──────────┘
   ↓                                絶縁壁
Q1 (BSS138)                       ┌──────────┐
                                   │ A   (8)  │── 485_A (R8 4.7k pullup)
HOST_3V3 ── R12(100k) ── Q2 gate   │ B   (9)  │── 485_B (R10 4.7k pulldown)
                                   │ VCC2     │── VCC_5V (絶縁 DC-DC)
                                   │ GND2     │── GND
                                   └──────────┘
```

- DE/RE は **HOST 側** (3.3V 系) で制御され、CA-IS3082W 内部で絶縁壁を越えてトランシーバ側に伝達
- BSS138 (N-MOS) は SS8050 (NPN BJT) より**スイッチング時間が短く** (Vth 1.5V、tr/tf nsec オーダー)
- 結果として DE/RE 自動切替の応答が高速

## 1 Mbaud 通る理由 (確度順)

### 1. ★最有力 — CA-IS3082W のスルーレート制限なし

- SP485EEN は IC 自身のデータシート上 **5 Mbps 規格** で内部にスルーレート抑制が入っている (EMI 抑制目的)
- CA-IS3082W は **10 Mbps 規格、スルーレート抑制なし** (高速版)
- 1 Mbaud のビット幅 = 1 µs に対し、SP485EEN の遷移時間 ~30 ns は 3% 占める。データの '1'→'0'/'0'→'1' エッジが連続するとマージンが消える
- CA-IS3082W は ~4 ns 程度で遷移するため、1 Mbaud でも信号が綺麗に出る

### 2. DE/RE 切替トランジスタの差 (BSS138 vs SS8050)

- SS8050 (NPN BJT、Vbe ~0.7V、tr ~100 ns 程度) → 切替が比較的遅い
- BSS138 (N-MOSFET、Vth 1.5V、tr/tf 数 ns) → 高速切替
- TX のエッジに対する DE/RE 追従が ISO485 の方が速い

### 3. USB 電源系の AC ノイズ切り離し (副次効果)

- ISO485 は **絶縁 DC-DC (B0505ST16-W5)** で送信側電源を作る
- ホスト USB の GND に乗っている AC 漏れ電流 (商用電源の容量結合で混入) が CA-IS3082W の差動受信に影響しなくなる
- 実測でも、AtomS3 + RS485 Unit を AC アダプタ近くに置くと 500 kbaud で 30% 程度に劣化、AC アダプタから離すと 100% に戻る現象を確認 → 絶縁すれば常に 100%

### 4. 3.3V 信号レベルでの BSS138 レベルシフト

- 3.3V 駆動の BSS138 は 5V 駆動より低電圧で確実にスイッチ
- 立ち上がり / 立ち下がりが急峻になる傾向

### 5. 保護素子 (副次)

- ISO485 は GDT・PTC・TVS・TSS など多重保護
- 信号品質改善というより耐サージ用だが、間接的にノイズ環境での安定性向上

## 注意点

- **どちらも DE/RE は自動切替**で、明示的な GPIO 制御ではない
- **半二重通信 (Modbus RTU 等) が前提**。両側同時送信や送信→受信切替の高速応答が必要なプロトコル (Dynamixel 1Mbaud 等) では、自動切替方式は理論的にギリギリ
- **CA-IS3082W は 10 Mbps 仕様** だが、自動 DE/RE 制御回路があるため、実用的な上限は 1〜2 Mbaud 程度と推定
- 本当に高速・高信頼を狙うなら、**DE/RE を MCU GPIO で明示的に制御するモジュール**を使うか自作するのが最良

## 参考資料

- 公式回路図 (Unit RS485): https://static-cdn.m5stack.com/resource/docs/products/unit/rs485/rs485_sch_01.webp
- 公式回路図 (Unit RS485-ISO): `docs/img/ISO485_回路.jpg` (M5Stack 公式 schematic v1.1 から抜粋)
- **実機 IC 刻印写真 (Unit RS485-ISO)**: [img/ISO485_IC_marking.jpg](img/ISO485_IC_marking.jpg) — CHIPANALOG CA-IS3082W (絶縁トランシーバ) と MORNSUN B0505ST16-W5 (絶縁 DC-DC) を物理確認 (2026-04-10)
- 詳細経緯: [202604091500_RS485-DE-RE-auto-direction.md](202604091500_RS485-DE-RE-auto-direction.md)
- 全モジュール比較: [202604092000_RS485-IC-module-full-comparison.md](202604092000_RS485-IC-module-full-comparison.md)
- ベンチ測定結果: [202604092200_RS485-baudrate-eval-results.md](202604092200_RS485-baudrate-eval-results.md)
