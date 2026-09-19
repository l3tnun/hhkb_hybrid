# ハードウェア概要

HHKB Professional HYBRID（英語配列, `PD-KB800` 系）の実機解析に基づくハードウェア情報。移植の詳細な根拠は [reverse-engineering/](reverse-engineering) を参照。

## MCU

- **STM32L072RBT6**（Cortex-M0+, ARMv6-M）
- Flash 128 KiB：
  - `0x08000000–0x0800FFFF`（64 KiB）：PFU ブートローダ（PFU 更新機能つき）。**変更しない**
  - `0x08010000–0x0801FFFF`（64 KiB）：アプリ領域。ここに QMK を書き込む
- EEPROM（データ EEPROM `0x08080000–0x080817FF`, 6 KB）：純正の起動判定・キャリブレーション・キーマップ・設定に使用。QMK が書き込むのは純正 FW も書き込むアドレスだけ
  - 更新モードへの移行：`0x08080360` / `0x08080370` / `0x08081350`
  - JIS/US 補正の保存：`0x08080520`（純正のスリープ時間設定バイトの最下位ビット）
  - VIA 対応版（`via` keymap）の設定保存：`0x08080700–0x08080CFF`（純正のキーマップ表 B）と、純正キーマップの有効マーク `0x08080300`。純正へ戻す前にこの領域をゼロに戻す（[via.md](via.md)、[restore-stock.md](restore-stock.md)）
  - キャリブレーション（`0x08081100` / `0x08081200`）は読み出しのみ
  - 純正 FW とブートローダの書き込み範囲の解析：[reverse-engineering/stm32-analysis.md](reverse-engineering/stm32-analysis.md)

### 書き換え寿命（STM32L0 データシートの値）

STM32L072RB**T6**（末尾 6 = −40〜85°C 品）の値。STM32L0x2/L0x3 のデータシート（Table「Flash memory and data EEPROM characteristics」「… endurance and retention」）より。

| 項目 | 値 |
|---|---|
| データ EEPROM の書き換え回数 | 最小 100,000 回（−40〜105°C） |
| プログラム用 Flash（アプリ領域）の書き換え回数 | 最小 10,000 回（−40〜105°C） |
| データ保持 | 上記回数の書き換え後も 85°C で 30 年 |
| 1 ワード（4 B）の消去・書き込み時間 | 標準 3.28 ms |

- ウェアレベリングではなく、**同じ場所を書き換えられる回数**の保証値（JEDEC JESD22-A117 に基づく特性評価）。このファームも純正 FW も、同じアドレスへ上書きする。
- このファームが EEPROM に書き込むのは、JIS/US 補正の切替（毎回 `0x08080520` の同じ 1 B）、VIA でのキー・設定の変更（変わったワードだけ）、設定の初期化・純正へ戻す操作（VIA 領域全体）、更新モードへの移行（起動判定の数か所）だけ。**通常の入力・起動・スリープ・Bluetooth 接続では書き込まない。**
- 目安：JIS/US 補正を 1 日 100 回切り替えると約 2.7 年、1 日 10 回なら約 27 年で最小保証回数に達する。ファームの書き込みはアプリ領域の 10,000 回が上限（毎日書いても約 27 年）。
- VIA 領域の全消去（1,536 B = 384 ワード）は約 1.3 秒。IWDG（約 5〜11 秒）に対して十分短い。

### ブートローダの起動判定

アプリが起動するのは以下がすべて成立するときのみ：

- EEPROM `0x08080360` == 1
- EEPROM `0x08081200` == 1
- CRC16(app, 0x10000) == EEPROM `0x08080370`（この CRC は HFB ヘッダ64 の `[2:4]` と一致）

DFU での生書き込みは CRC を更新しないため起動しない。HID 更新経路（E0/E1/E2/E3）は CRC まで更新するため起動する。

更新モードへの入り方：EEPROM を書き換え（`0x08080360←0`, `0x08080370←0`, `0x08081350←1`）てから `NVIC_SystemReset`。QMK も純正もこの方法。

## キーマトリクス

- Topre 静電容量方式。1 本の ADC ピン（PA1 / ADC ch1）に多重化して読む
- 行列を駆動する出力：PB0/1/2/10/12/13/14/15, PC6/7/8/14
- **キーではブートローダの STOP から起床できない**（マトリクスが 1 本の ADC に多重化されており EXTI が無い）。これは純正も同じ

## Bluetooth（nRF52832）

- STM32 = **SPI1 スレーブ**（PA15=NSS, PB3=SCK, PB4=MISO, PB5=MOSI, mode 3, 8bit）。nRF がマスタ（125 kHz）
- **PB6** = 要求パルス（Low）。**PD2** Low = nRF 有効
- フレーム：STM32 は `00 AA <cmd> <data>` で応答。nRF のポーリング：`0x02`（キーボード/consumer）, `0x03`（制御）, `0x05`（電池）, `0x06`（スロット選択）
- メディアキーは usage code ではなくビットマスクで送る
- 詳細：[reverse-engineering/bluetooth-protocol.md](reverse-engineering/bluetooth-protocol.md)

## 電源

- **PC5**（Low=押下）：電源ボタン。2 秒長押しで OFF
- **PC13**（Low=USB 給電中）：VBUS 検出
- **PC4**（Low=DIP SW6 ON）：自動スリープ無効化
- OFF/スリープは STM32 の **STOP モード**。起床は PC5（ボタン）/ PC13（USB）の立ち下がりのみ。LPTIM1（LSI, 約 2 秒）で定期起床して IWDG を更新
- 起床時は `NVIC_SystemReset`（ソフトウェアリセット）。IWDG リセットではないので更新モードには落ちない
- 詳細：[reverse-engineering/power-management.md](reverse-engineering/power-management.md)

## 電池残量

- アナログ残量計ではなく、3 本のデジタル入力による 4 段階：
  - PB9 Low → 0 %、PB8 Low → 5 %、PB7 Low → 15 %、すべて High → 100 %
  - USB 給電中は 100 % 固定
- ピンと段階の対応（PB7=15 %, PB8=5 %）は静的解析からの推定（0 % 経路のみコード確認済み）。実機では 100 % のみ観測。詳細と検証状況は [reverse-engineering/power-management.md](reverse-engineering/power-management.md)

## インジケータ LED

- **PA8** = 青（状態）、**PA9** = 橙（警告）。ともに active high
- 同時には片方のみ点灯し、橙（LED1）が優先。純正と同じ
- 詳細：[reverse-engineering/led-indication.md](reverse-engineering/led-indication.md)

## 起動時の注意（QMK 側の対処）

- ブートローダは SysTick を保留状態のまま・IRQ をマスクしたままジャンプするため、`__early_init` で PENDST/PENDSV と NVIC をクリアする
- ベクタテーブル再配置：`CRT0_VTOR_INIT=1`
- IWDG は STOP 中も止まらないため、STOP 中は LPTIM1 で定期起床して更新する
