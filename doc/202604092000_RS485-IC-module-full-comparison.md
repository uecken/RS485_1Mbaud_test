# RS-485 トランシーバ IC・モジュール 全比較表

本プロジェクト (RS-485 1Mbps 無線化) で検討した全 RS-485 トランシーバ IC・モジュールの比較資料。

---

## 1. トランシーバ IC 詳細比較表

> **注**: SP485EE と SP485EEN は同じファミリ。"N" は SOIC ナローボディ等のパッケージサフィックス。電気特性は同等で、本資料では SP485EE と表記。
> M5Stack RS485 Unit (Grove)・M5StickC RS485 HAT のいずれも **SP485EE** を搭載。

| 項目 | MAX485 | SP485EE | MAX13487E | MAX13488E | **MAX3485** | **SP3485** | **SN65HVD75** | LTC1485 |
|------|--------|----------|-----------|-----------|-------------|------------|---------------|---------|
| **メーカー** | Analog Devices | MaxLinear | Analog Devices | Analog Devices | Analog Devices | MaxLinear | Texas Instruments | Analog Devices |
| **電源電圧** | 5V | 5V | 5V | 5V | **3.3V** | **3.3V** | **3.3V** | 5V |
| **最大データレート** | 2.5 Mbps | 10 Mbps | **500 kbps** | 16 Mbps | 10 Mbps | 10 Mbps | **20 Mbps** | 10 Mbps |
| **スルーレート制限** | あり | あり | あり | なし | **なし** | **なし** | **なし** | なし |
| **立上り/立下り時間** | ~30ns | ~20ns | ~800ns 以上 | ~4ns | ~4ns | 5-20ns | ~4ns | ~7ns |
| **差動出力電圧 V_OD (54Ω 負荷)** | ≥2.0V | ≥1.5V | ≥1.5V | ≥1.5V | ≥1.5V | ≥1.5V | ≥1.5V | ≥2.0V |
| **方向制御 (DE/RE)** | 手動 | 自動 (HAT 回路) | **自動** | **自動** | 手動 | 手動 | 手動 | 手動 |
| **ホットスワップ対応** | なし | なし | あり | あり | なし | なし | あり | なし |
| **ESD 保護** | 通常 | 通常 | **±15kV** | **±15kV** | 通常 | 通常 | **±15kV (IEC)** | 通常 |
| **受信負荷 (Unit Load)** | 1/8 | 1/8 | **1/4 (128台接続可)** | **1/4** | 1/8 | 1/8 | 1/8 | 1/2 |
| **バス上の最大接続数** | 32 | 32 | **128** | **128** | 32 | 32 | 32 | 32 |
| **パッケージ** | DIP8 / SOIC8 | SOIC8 | SO8 | SO8 | DIP8 / SOIC8 | DIP8 / SOIC8 | SOIC8 | DIP8 / SOIC8 |
| **本プロジェクト適合** | × | × | × | ○ | **◎** | **◎** | **◎◎** | ○ |
| **データシート** | [PDF](https://www.analog.com/media/en/technical-documentation/data-sheets/max1487-max491.pdf) | [PDF (M5Stack)](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/hat/SP485EEN_en.pdf) ※SP485EE/EEN 共通 | [PDF](https://www.analog.com/media/en/technical-documentation/data-sheets/max13487e-max13488e.pdf) | [PDF](https://www.analog.com/media/en/technical-documentation/data-sheets/max13487e-max13488e.pdf) | [Product Page](https://www.analog.com/en/products/max3485.html) / [PDF](https://www.farnell.com/datasheets/2002060.pdf) | [PDF](https://www.maxlinear.com/ds/sp3485.pdf) | [Product Page](https://www.ti.com/product/SN65HVD75) | [Product Page](https://www.analog.com/en/products/ltc1485.html) |
| **適合理由** | スルーレート制限 + 2.5Mbps 上限 | **M5Stack RS485 Unit / HAT 共通搭載**。スルーレート制限あり | 500kbps 上限 (名前と裏腹に低速) | 16Mbps 対応だが 5V 動作 | 3.3V 直結可 | 3.3V 直結可 | 最高速度 20Mbps + 高ESD | 国内入手容易 (秋月) |

---

## 1.5 絶縁型 RS-485 トランシーバ IC

> 上記の非絶縁 IC とは別カテゴリ。バス両端の GND 電位差・サージ・グラウンドループ対策を必要とする場面で必須。本プロジェクトでは **送信側 1 Mbaud 連続送信の安定動作** に CA-IS3082W が決定的な役割を果たした。

| 項目 | **CA-IS3082W** (採用) | ADM2587E | ADM2483 | ISO1500 | MAX14855 |
|------|---------------------|----------|---------|---------|----------|
| **メーカー** | **CHIPANALOG (中国)** | Analog Devices | Analog Devices | Texas Instruments | Analog Devices |
| **電源電圧 (VCC1 / VCC2)** | **2.375–5.5 V / 3–5.5 V** | 3.3/5 V / 3.3/5 V | 5 V / 5 V | 3.3/5 V / 3.3/5 V | 3.3/5 V / 3.3/5 V |
| **絶縁方式** | **容量結合 (SiO₂ バリア)** | iCoupler (磁気) | iCoupler | キャパシタ | iCoupler |
| **絶縁耐圧 VISO** | **5000 Vrms (60s)** ※参考値 | 2500 Vrms | 2500 Vrms | 5700 Vpk | 3750 Vrms |
| **CMTI** | **150 kV/μs typ** | 25 kV/μs | 25 kV/μs | 100 kV/μs | 75 kV/μs |
| **絶縁 DC-DC 内蔵** | **なし (外部 B0505 等が必要)** | **あり** (isoPower) | なし | なし | なし |
| **最大データレート** | **20 Mbps** (CA-IS308x ファミリで 500 k/10 M/20 M 選択可、実機測定 1 Mbaud OK) | 500 kbps | 500 kbps | 1 Mbps | 25 Mbps |
| **スルーレート制限** | **なし** | あり | あり | なし | なし |
| **バスコモンモード範囲** | **−7 V 〜 +12 V** | -7V〜+12V | -7V〜+12V | -7V〜+12V | -7V〜+12V |
| **バス最大ノード数 (Unit Load)** | **256 (1/8 UL)** | 256 | 256 | 320 | 320 |
| **半二重 / 全二重** | **半二重** (CA-IS3080/86 は全二重版) | 半二重 | 半二重 | 半二重 | 半二重 |
| **方向制御 (DE/RE)** | 手動 GPIO (IC ピン) ※モジュールでは自動切替に配線 | 手動 | 手動 | 手動 | 手動 |
| **動作温度** | **−40 °C 〜 +125 °C** | -40〜+85 | -40〜+85 | -40〜+125 | -40〜+125 |
| **パッケージ** | **SOIC-16 Wide (10.3×7.5 mm)** | SOIC-20 Wide | SOIC-16 Wide | SOIC-16 | SOIC-16 |
| **採用例 (本プロジェクト)** | **M5Stack Unit RS485-ISO** | — | — | — | — |
| **本プロジェクト適合** | **◎ (実機 1 Mbaud 動作確認済み)** | × 速度不足 | × 速度不足 | △ 1 Mbps 上限 | ○ |
| **データシート** | [PDF (LCSC)](https://www.lcsc.com/datasheet/C528766.pdf) / [Chipanalog 製品ページ](https://e.chipanalog.com/products/interface/isolated/rs/731) | [Product Page](https://www.analog.com/en/products/adm2587e.html) | [Product Page](https://www.analog.com/en/products/adm2483.html) | [Product Page](https://www.ti.com/product/ISO1500) | [Product Page](https://www.analog.com/en/products/max14855.html) |

> **注**: CA-IS3082W の行以外はカタログ値ベースの参考値。本プロジェクトで実機評価したのは **CA-IS3082W のみ**。他の絶縁 IC を使う場合は必ずデータシートで最新値を確認すること。

### CA-IS3082W (採用済) 詳細

- **搭載モジュール**: M5Stack **Unit RS485-ISO** (実機 IC 刻印で物理確認: [img/ISO485_IC_marking.jpg](img/ISO485_IC_marking.jpg))
- **絶縁側電源**: **MORNSUN B0505ST16-W5** (5V 入力・5V 出力 isolated DC-DC、**0.5 W**、SOIC-16、5000 VAC 絶縁、温度 -55 °C 〜 +125 °C、2xMOPP 準拠、IEC/EN 60601 準拠) を併用。M5Stack 公式回路図 [img/ISO485_回路.jpg](img/ISO485_回路.jpg) 参照
- **スルーレート制限なし** が SP485EE 系との決定的な違い。1 Mbaud 連続送信でも波形なまりなく FER 0%
- **方向制御 (DE/RE)** は IC ピンとしては手動制御可能だが、M5Stack Unit RS485-ISO 上では **TX 連動の自動切替** (BSS138 MOSFET) に配線されている
- **バスローディング 1/8 Unit Load** → 1 バス 256 ノード まで接続可 (本プロジェクトは 2 ノードのため影響なし)
- **国内入手**: M5Stack Unit RS485-ISO 経由 (スイッチサイエンス・M5Stack 直販) が最も現実的。CA-IS3082W の IC 単体流通は LCSC / JLCPCB 経由となり国内代理店での入手は限定的
- **CA-IS308x ファミリ命名規則**: CA-IS3080/3086 = 全二重版、CA-IS3082/3088 = 半二重版。サフィックス (W/無印等) は速度グレード・パッケージ違い。本モジュールの CA-IS3082W は「半二重・Wide Body SOIC-16」の意味

---

## 2. 完成品モジュール比較表

| # | モジュール | 搭載IC | 速度 | 電圧 | 方向制御 | コネクタ | 入手先 | 価格 | 推奨度 |
|---|-----------|-------|------|------|---------|---------|--------|------|--------|
| 1 | M5Stack RS485 Unit (現用) | **SP485EE** | 500 Kbps 実測 | 5V | 手動 | Grove | [M5Stack](https://shop.m5stack.com/products/rs485-unit) | ~¥700 | × 1Mbps 不可 |
| 2 | M5StickC RS485 HAT | SP485EEN | **115200 bps** (仕様記載) | 5-12V | 自動 | HAT 直結 | [Switch Science](https://www.switch-science.com/products/6069) | ¥825 | × 仕様上限 |
| 3 | M5Stamp RS485 モジュール | 要調査 | 要調査 | 3.3V | 要調査 | Stamp | [Switch Science](https://www.switch-science.com/products/8189) | ~¥800 | △ 要調査 |
| 4 | スイッチサイエンス SP3485 モジュール | SP3485 | 10 Mbps | 3.3V | 手動 | ピンヘッダ | [Switch Science](https://www.switch-science.com/products/596) | ¥1,314 | △ 販売終了の可能性 |
| 5 | **Waveshare RS485 Board (3.3V)** | SP3485 | 10 Mbps | 3.3V | 手動 | ピンヘッダ | [Waveshare](https://www.waveshare.com/rs485-board-3.3v.htm) / [AliExpress](http://www.aliexpress.com/store/product/RS485-Board-3-3V-MAX3485-RS-485-Communication-Board-Transceiver-Evaluation-Development-Module-Kit/216233_700652432.html) | ~$6 (¥900) | **○ 推奨** |
| 6 | **SparkFun RS-485 Breakout (BOB-10124)** | SP3485 | 10 Mbps | 3.3V | 手動 | ピンヘッダ + RJ45 + ネジ端子 | [SparkFun](https://www.sparkfun.com/sparkfun-transceiver-breakout-rs-485.html) / [Amazon](https://www.amazon.com/SparkFun-Transceiver-Breakout-Half-Duplex-Interoperable/dp/B082MM58RF) / [DigiKey](https://www.digikey.com/en/products/detail/sparkfun-electronics/10124/6006043) | ~$11 (¥1,700) | **○ 推奨** |
| 7 | **Soldered RS-485 Transceiver Breakout** | **SN65HVD75** | **20 Mbps** | 3.3V | 手動 | ピンヘッダ + easyC | [Soldered](https://soldered.com/products/rs-485-transciever-breakout) / [Tindie](https://www.tindie.com/products/solderedelectronics/rs-485-transceiver-breakout/) | ~$5-8 | **◎ 最推奨** |
| 8 | 汎用 MAX3485 モジュール | MAX3485 | 10 Mbps | 3.3V/5V | 手動 | ピンヘッダ | [AliExpress 検索](https://www.aliexpress.com/w/wholesale-max3485-module-rs485.html) / [Amazon DollaTek](https://www.amazon.com/DollaTek-Development-Converter-Accessories-Replacement/dp/B08HQMDKGR) / [eBay](https://www.ebay.com/itm/225626333704) | $2-5 (¥300-800) | △ 品質ばらつき |
| 9 | DollaTek MAX3485 Module | MAX3485 | 10 Mbps | 3.3V | 手動 | ピンヘッダ | [Amazon](https://www.amazon.com/DollaTek-Development-Converter-Accessories-Replacement/dp/B08HQMDKGR) | ~$5 | △ 品質要確認 |
| 10 | **M5Stack Unit RS485-ISO (絶縁型)** | **CA-IS3082W + B0505ST16-W5** | **1 Mbaud 実測 OK (TX/RX とも)** | 5V (内部で 3.3V 生成 + 絶縁 5V 生成) | 自動 (BSS138) | Grove | [M5Stack](https://docs.m5stack.com/ja/unit/iso485) / [Switch Science](https://www.switch-science.com/products/9242) | ~¥2,800 | **◎ 送信側必須** |

> Unit RS485-ISO の搭載 IC は実機写真で物理確認済み: [img/ISO485_IC_marking.jpg](img/ISO485_IC_marking.jpg)
> 詳細比較は [202604092230_RS485Unit-vs-ISO485Unit-comparison.md](202604092230_RS485Unit-vs-ISO485Unit-comparison.md) を参照。

---

## 3. IC 単体 (国内入手可)

| IC | パッケージ | 速度 | 電圧 | 価格 | 入手先 | 備考 |
|----|----------|------|------|------|--------|------|
| MAX485EN | DIP8 | 2.5 Mbps | 5V | ¥100 | [秋月電子](https://akizukidenshi.com/catalog/g/g115806/) | 速度不足 |
| **LTC1485CN8** | DIP8 | **10 Mbps** | 5V | ¥300 | [秋月電子](https://akizukidenshi.com/catalog/g/g101869/) | 国内最速・即入手 |
| LTC4485 | DIP8 | 10 Mbps | 5V | - | [秋月電子](https://akizukidenshi.com/catalog/g/gI-02792/) | LTC1485 同等 |

---

## 4. OSHW (Open Source Hardware) 設計

自作・カスタマイズ可能な公開設計。

| プロジェクト | 搭載IC | 速度 | ライセンス | リポジトリ | 備考 |
|------------|-------|------|----------|----------|------|
| SolderedElectronics RS-485 Breakout | SN65HVD75 | 20 Mbps | OSHW | [GitHub](https://github.com/SolderedElectronics/RS-485-Transceiver-breakout-hardware-design) | BOM/Gerber/3D 公開 |
| SparkFun RS-485 Breakout | SP3485 | 10 Mbps | CC BY-SA 4.0 | [GitHub](https://github.com/sparkfun/RS-485_Breakout) | Eagle 設計 |
| KalemDevelop RS485-5V-BASIC | MAX485 | 2.5 Mbps | - | [GitHub](https://github.com/KalemDevelop/RS485-5V-BASIC) | 終端抵抗・フェイルセーフ内蔵 |

---

## 5. 最終推奨 + 購入リスト

### 並行入手推奨

| 優先 | モジュール | 数量 | 単価目安 | 入手先 | 到着目安 | 小計 |
|------|-----------|------|--------|--------|---------|------|
| **0 (本命)** | **M5Stack Unit RS485-ISO (CA-IS3082W + B0505ST16-W5)** | ×2 (送信側・両端で絶縁する場合 ×4) | ~¥2,800 | [Switch Science](https://www.switch-science.com/products/9242) / [M5Stack](https://docs.m5stack.com/ja/unit/iso485) | 即日〜2 日 | ~¥5,600〜11,200 |
| 1 | **Waveshare RS485 Board (3.3V)** | ×2 | ~¥900 | [Amazon Japan / Waveshare](https://www.waveshare.com/rs485-board-3.3v.htm) | 2-3日 | ~¥1,800 |
| 2 | **Soldered RS-485 Transceiver Breakout (SN65HVD75)** | ×2 | ~¥800-1,200 | [Tindie](https://www.tindie.com/products/solderedelectronics/rs-485-transceiver-breakout/) / [Soldered](https://soldered.com/products/rs-485-transciever-breakout) | 1-2週間 | ~¥1,600-2,400 |
| 3 (予備) | **LTC1485CN8** (IC 単体) | ×2 | ¥300 | [秋月電子](https://akizukidenshi.com/catalog/g/g101869/) | 1-2日 | ¥600 |
| 3 (予備) | 終端抵抗 120Ω 1% | ×4 | ~¥30 | [秋月電子](https://akizukidenshi.com/) | 1-2日 | ~¥120 |

**合計目安: 本命構成 (Unit RS485-ISO × 2 + Unit RS485 × 2) = ~¥7,000、HAT/素 IC 構成に置き換えれば ~¥4,000-5,000**

### 推奨理由

| 順位 | モジュール | 推奨理由 |
|------|-----------|---------|
| **0** | **M5Stack Unit RS485-ISO** | **送信側で 1 Mbaud 連続動作実測済み**、絶縁 5000 Vrms でノイズ環境耐性、Grove 接続で M5Stack 系 MCU (M5StickC/M5AtomS3) に即スタック、即日国内入手可 |
| **1** | Soldered SN65HVD75 Breakout | 20 Mbps (1Mbps の 20倍マージン)、IEC ESD ±15kV、OSHW で問題発生時に検証可能 |
| **2** | Waveshare SP3485 Board | 10 Mbps、Amazon 即入手、実績多数、~¥900 と安価 |
| **3** | SparkFun BOB-10124 | RJ45 コネクタ付きで LAN ケーブル直結可能 |

---

## 6. 検証手順

1. モジュールを ESP32 (M5StickC) と接続
2. [rs485_tx](../rs485_tx/) / [rs485_rx](../rs485_rx/) で 500Kbps / 750Kbps / 1Mbps をテスト
3. **120Ω 終端抵抗** を両端に追加してテスト
4. オシロで RS-485 A/B 線の波形を観測 (立上り時間、差動電圧)
5. パケットロス率を計測
6. 5m ケーブルで実用性確認

---

## 7. 参考リンク

### IC データシート
- [SN65HVD75 (TI)](https://www.ti.com/product/SN65HVD75)
- [MAX3485 (Analog Devices)](https://www.analog.com/en/products/max3485.html)
- [SP3485 (MaxLinear)](https://www.maxlinear.com/ds/sp3485.pdf)
- [MAX485 (Analog Devices)](https://www.analog.com/media/en/technical-documentation/data-sheets/max1487-max491.pdf)
- [SP485EEN (M5Stack OSS)](https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/hat/SP485EEN_en.pdf)
- [MAX13487E/MAX13488E (Analog Devices)](https://www.analog.com/media/en/technical-documentation/data-sheets/max13487e-max13488e.pdf)
- [LTC1485 (Analog Devices)](https://www.analog.com/en/products/ltc1485.html)
- **[CA-IS3082W (Chipanalog、LCSC ミラー PDF)](https://www.lcsc.com/datasheet/C528766.pdf)** — Unit RS485-ISO 搭載 IC
- **[CA-IS3082WX 製品ページ (Chipanalog)](https://e.chipanalog.com/products/interface/isolated/rs/731)**
- **[B0505ST16-W5 (MORNSUN)](https://www.mornsun-power.com/public/uploads/pdf/B0505ST16-W5.pdf)** — Unit RS485-ISO 搭載 絶縁 DC-DC

### 完成品モジュール購入先
- [M5Stack RS485 Unit](https://shop.m5stack.com/products/rs485-unit)
- **[M5Stack Unit RS485-ISO (絶縁型)](https://docs.m5stack.com/ja/unit/iso485) / [Switch Science](https://www.switch-science.com/products/9242)**
- [M5StickC RS485 HAT - Switch Science](https://www.switch-science.com/products/6069)
- [Waveshare RS485 Board (3.3V)](https://www.waveshare.com/rs485-board-3.3v.htm)
- [SparkFun RS-485 Breakout](https://www.sparkfun.com/sparkfun-transceiver-breakout-rs-485.html)
- [Soldered RS-485 Breakout - Tindie](https://www.tindie.com/products/solderedelectronics/rs-485-transceiver-breakout/)
- [Soldered RS-485 Breakout - 直販](https://soldered.com/products/rs-485-transciever-breakout)

### IC 単体 国内
- [秋月電子 LTC1485CN8](https://akizukidenshi.com/catalog/g/g101869/)
- [秋月電子 MAX485EN](https://akizukidenshi.com/catalog/g/g115806/)
- [秋月電子 RS422/RS485関連](https://akizukidenshi.com/catalog/c/crs422/)

### OSHW
- [SolderedElectronics RS-485 Breakout (GitHub)](https://github.com/SolderedElectronics/RS-485-Transceiver-breakout-hardware-design)
- [sparkfun/RS-485_Breakout (GitHub)](https://github.com/sparkfun/RS-485_Breakout)
- [KalemDevelop RS485-5V-BASIC (GitHub)](https://github.com/KalemDevelop/RS485-5V-BASIC)
