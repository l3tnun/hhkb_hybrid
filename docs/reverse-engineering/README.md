# 解析記録について

このフォルダは、純正ファームウェアと基板の解析記録です。移植の根拠として残しています。

- 利用手順ではありません。ビルドや書き込みの手順は [../quickstart.md](../quickstart.md) から始まる docs を参照してください。
- 解析に使った中間生成物（純正 FW のダンプ、逆アセンブルの出力、作業用フォルダなど。文中の `hhkb_stock_128k.bin` や `build/...` など）は公開していません。純正ファームウェアや PFU 由来のバイナリを含むためです。
- 文中の「B-4a」などの記号は、移植作業中の段階を示すもので、手順とは関係ありません。
- 各記述の確度は、文書ごとに [確定] / [推定] などで示しています。

| ファイル | 内容 |
|---|---|
| [stm32-analysis.md](stm32-analysis.md) | STM32（MCU）側の解析。EEPROM の使い方と、純正キーマップの読み書き |
| [implementation.md](implementation.md) | HFB 形式、HID 更新プロトコル（E0〜E3、QMK 独自の F0）、更新モードでの USB エラー |
| [bluetooth-protocol.md](bluetooth-protocol.md) | STM32 と nRF52832（Bluetooth）の SPI 通信 |
| [nrf-image.md](nrf-image.md) | HFB に含まれる nRF ファームの解析 |
| [power-management.md](power-management.md) | 電源ボタン・スリープ・電池 |
| [led-indication.md](led-indication.md) | 純正 FW の LED 表示 |
