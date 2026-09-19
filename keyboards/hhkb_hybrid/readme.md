# HHKB Professional Hybrid (QMK)

HHKB Professional Hybrid（`PD-KB800` 系, 英語配列）の純正ファームウェアを置き換える QMK ボード定義。

- MCU: STM32L072RBT6（Cortex-M0+, flash 128 KiB / RAM 20 KiB / 内蔵 EEPROM）
- 配置: `0x08010000` 起点、64 KiB 以内（PFU ブートローダ `0x08000000..0x0800FFFF` は不変）
- Bluetooth: nRF52832（EYSHCNZWZ）経由。USB / BT 両対応
- USB ID: `04fe:0021`（純正と同じ）。QMK は製品名 "HHKB Hybrid QMK" / bcdDevice 0.20 で判別（純正アプリ・PFU 更新モードは "HHKB-Hybrid" / 0.01）

## 更新モードへの移行 / 純正への復帰

- **bootmagic**: Esc を押しながら接続すると PFU ブートローダの更新モードに入る（純正 FW への復帰に使用）。QMK の設定を初期化し、`via` keymap では純正キーマップ表 B（VIA の保存領域）もゼロに戻す。
- 動作中の QMK からは `tools/flash.py` が書き込み開始時に自動で更新モードへ遷移する（QMK の HFB なら `AA AA E0`、純正 HFB なら表 B を消す `AA AA F0`）。
- `Fn+Esc` による更新モード移行は誤爆防止のため無効化済み。VIA で `QK_BOOT` キーを割り当てると更新モードに入れるが、表 B を消さないため**純正への復帰には使わない**こと。
- 更新モードへ移る前に USB を切断して 500 ms 待つ（E0 / F0 / QK_BOOT / bootmagic 共通）。

## keymap

- `default`: VIA 非対応（EEPROM は transient）。
- `via`: VIA 対応。設定は `0x08080700–0x08080CFF`（純正キーマップ表 B）に専用ドライバ `eeprom_hhkb.c` で保存。詳細は `docs/via.md`。

## 起動の仕組み

PFU ブートローダは EEPROM の起動フラグと CRC16（= HFB header64）が一致したときだけアプリへジャンプする。DFU での生書き込みでは CRC が更新されないため、**必ず HID 更新（E0〜E3）で書き込む**。詳細はリポジトリ直下 `docs/hardware.md`。

## ビルドと書き込み

リポジトリ直下から：

```sh
./tools/build.sh            # または ./tools/build.sh via
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_default.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_default.hfb
python3 tools/flash.py --list
python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_default.hfb
```

手順の詳細：`docs/quickstart.md`（全体の流れ）、`docs/setup.md`, `docs/build.md`, `docs/flashing.md`, `docs/restore-stock.md`, `docs/via.md`。

## フェイルセーフ / 起動時の注意

- 起動直後に IWDG（約 7 秒）を開始し、メインループで給餌する。ハング / HardFault で IWDG リセットされると、次回起動時に更新モードへ戻る。
- PFU ブートローダは SysTick を動かしたまま割込み禁止でジャンプし、クロック・NVIC も自前設定のまま渡す。`boards/HHKB_HYBRID/board.c` の `__early_init` で SysTick/PendSV の pending クリア、NVIC クリア、PLL→MSI 切り戻しを行ってから ChibiOS のクロック初期化に入る。これが無いと `chSysInit` の割込み許可直後に未登録の SysTick 例外で停止する。
- STOP 中も IWDG は止まらないため、STOP 中は LPTIM1 で定期起床して給餌する（`power.c`）。

## 参考

- Duncaen `qmk_firmware` branch `hhkb_classic`（`keyboards/hhkb/classic`）— マトリクス・ADC 実装のベース
