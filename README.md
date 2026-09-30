# HHKB Professional HYBRID — QMK Firmware

HHKB Professional HYBRID（英語配列, `PD-KB800` 系, USB `04fe:0021`）の純正ファームウェアを **QMK に置き換える**プロジェクトです。純正の更新と同じ範囲（STM32 アプリ領域 64 KiB）だけを HID 経由で書き換えて QMK を動かし、**分解せず**に純正へ戻せることを前提にしています。  
このリポジトリ 1 つで、誰でも同一のファームウェアを再現できることを目標にしています（QMK 本体をコミット単位で固定）。  

> ⚠️本プロジェクトは全て生成AIを使用して作成したものになります。2026/09/19 時点に置いて`Claude Opus 5`, `DeepSeek V4.1 Flash`を使用しています。  

> ⚠️ **非公式・自己責任 — メーカー（PFU/HHKB）に問い合わせてはなりません。**  
> これは PFU/HHKB 非公式の改造です。このリポジトリのファームウェア（`default` / `via` のいずれも）を**書き込んだ時点で、メーカーの保証は対象外になります**。  
> **分解せずに純正 FW に戻した場合でも同じ**です。  
> **いかなる状態であっても、保証対応・修理の依頼・問い合わせ等は、メーカーへ行わないでください。完全に自己責任で**利用してください。

## はじめに

**初めての人は [docs/quickstart.md](docs/quickstart.md) から始めてください。** 環境構築 → ビルド → 変換 → 状態確認 → 書き込み → 確認の順に、コマンドと期待する出力をまとめています。

- 対応機種：HHKB Professional HYBRID の**英語配列**（USB ID `04fe:0021`）。実機で確認しているのはこの機種だけです。日本語配列・Classic・Studio などは対象外です（[docs/device-state.md](docs/device-state.md#対応機種の確認)）。
- 書き込みは **Linux のみ**（`/dev/hidraw*` を使用）。ビルドには Docker を使います。
- 純正ファームの HFB（PFU 公式配布）を各自で取得する必要があります。

## ドキュメント

| 目的 | 文書 |
|---|---|
| 全体の流れ | [docs/quickstart.md](docs/quickstart.md) |
| 環境構築（Docker、udev ルール、純正 HFB の取得と検証） | [docs/setup.md](docs/setup.md) |
| キーボードの状態の見分け方・書き込み先の選び方・対応機種 | [docs/device-state.md](docs/device-state.md) |
| ビルド | [docs/build.md](docs/build.md) |
| 書き込み（`flash.py` のオプション） | [docs/flashing.md](docs/flashing.md) |
| キー操作・Bluetooth・電源・LED | [docs/usage.md](docs/usage.md) |
| VIA | [docs/via.md](docs/via.md) |
| JIS 環境で US 刻印どおりに入力 | [docs/jis-us-toggle.md](docs/jis-us-toggle.md) |
| 純正 FW への復帰 | [docs/restore-stock.md](docs/restore-stock.md) |
| 困ったとき | [docs/troubleshooting.md](docs/troubleshooting.md) |
| DFU による復旧（分解が必要な最終手段） | [docs/recovery.md](docs/recovery.md) |
| ハードウェア・EEPROM | [docs/hardware.md](docs/hardware.md) |
| 解析記録 | [docs/reverse-engineering/](docs/reverse-engineering/README.md) |
| AI エージェント向けの運用ルール | [AGENTS.md](AGENTS.md) |

## 現状（動作確認済み）

- ✅ USB キーボードとして起動・入力
- ✅ Bluetooth（nRF52832 経由）での入力・スロット切替・ペアリング
- ✅ メディアキー（音量など、BLE consumer）
- ✅ 電源ボタン（長押しで OFF）・無操作自動スリープ（STOP モード, DIP SW6 で無効化）
- ⚠️ 電池駆動時の省電力動作（4 MHz 動作、走査の間引きと Sleep、USB・読み取り回路の停止。既定で有効、`-e HHKB_LOW_POWER=no` で従来の動作に戻せる）。電池駆動でのキー入力・Bluetooth の接続と切替・電源ボタン長押しの OFF と起動・USB の抜き差しは実機で確認済み。自動スリープ・電源ボタン短押し・電池残量の表示・VIA は未確認。電池持ちの改善幅は未測定 — [docs/usage.md](docs/usage.md#電池駆動時の省電力動作)
- ✅ インジケータ LED（接続状態・電源・電池低下）
- ✅ 最後に使った BT スロットへ電源 ON 時に再接続（見つからないときは約 60 秒で打ち切り。詳細は [docs/usage.md](docs/usage.md)）
- ✅ bootmagic（Esc を押しながら接続 → 純正の更新モードに入り、純正 FW へ復帰可能）
- ⚠️ 電池残量：純正同様の 4 段階（100/15/5/0 %）。詳細と検証状況は [docs/reverse-engineering/power-management.md](docs/reverse-engineering/power-management.md) を参照
- ✅ JIS 環境で US 刻印どおりに入力する切替機能（`Ctrl+Alt+Shift+J`、VIA 版は任意のキーにも割り当て可）。ビルドオプションで有効/無効を選択可（既定は有効）— [docs/jis-us-toggle.md](docs/jis-us-toggle.md)
- ✅ VIA 対応（`via` keymap）：キーマップ・マクロ・自動スリープ時間などを VIA で変更、EEPROM に保存 — [docs/via.md](docs/via.md)

対象外：Vial、macOS/Windows 公式 Keymap Tool 経由の書き込み。

## 仕組み（安全性の要点）

- 書き込むのは STM32 アプリ領域 `0x08010000–0x0801FFFF`（64 KiB）と、純正の更新が触れる EEPROM アドレスのみ。PFU ブートローダ（`0x08000000–0x0800FFFF`）は変更しません。
- 書き込みは純正の更新と同じ **HID 更新経路（E0/E1/E2/E3）**。CRC を含む起動判定も純正どおり更新されるため、書き込み後そのまま起動します。
- **純正への復帰は分解不要**：`tools/flash.py` で純正 HFB を書き込むか、bootmagic（Esc を押しながら接続）でブートローダの更新モードに入って書き戻せます（実機で確認済み）。VIA 版は設定を純正のキーマップ領域に保存するため、どちらの経路でもその領域を消してから戻します。
- DFU（基板の R6/R85 ショート）は物理分解が必要なため、切り分け・最終手段に限ります（[docs/recovery.md](docs/recovery.md)）。

### Bluetooth は純正 nRF ファームを流用しています

QMK 化しているのは STM32 アプリ側のみです。**Bluetooth 側は自作しておらず、PFU 製の nRF52832 ファームウェアをそのまま使います**。

- HFB は「純正 HFB の STM32 アプリ領域だけを QMK に差し替え、後半の nRF(Bluetooth) ファームイメージとコンテナ（ヘッダ・フッタ）は純正のまま残す」形で生成します（`tools/qmk_to_hfb.py`）。STM32 側の QMK は、この純正 nRF ファームに SPI で話しかけているだけです（`keyboards/hhkb_hybrid/nrf_spi.c`）。
- したがって **生成される `.hfb` には PFU 製の proprietary バイナリ（nRF ファーム）が含まれ、再配布できません**。各利用者が自分で純正 `HHKB800_FW_A048.hfb` を用意し、変換のテンプレートに使う必要があります（本リポジトリが HFB を配布しない理由）。

詳細な解析の根拠は [docs/reverse-engineering/](docs/reverse-engineering/) にまとめています。

## ディレクトリ構成

```
.
├── qmk.json                     QMK Userspace マニフェスト（ビルド対象の宣言）
├── keyboards/hhkb_hybrid/       キーボード定義（このリポジトリが正）
│   └── keymaps/{default,via}/   default（VIA 非対応）/ via（VIA 対応）
├── via/hhkb_hybrid.json         VIA 定義（tools/gen_via_json.py で生成）
├── qmk_firmware/                QMK 本体（git submodule, コミット固定）
├── local/                       純正 HFB・変換した HFB などを置く場所（.gitignore 対象。clone 直後は空）
├── tools/                       ビルド・変換・書き込みツール
│   ├── build.sh                 ビルド（固定 submodule + Docker）
│   ├── qmk_to_hfb.py            QMK の .bin → 純正更新用 .hfb 変換
│   ├── flash.py                 HID 経由でキーボードへ書き込み
│   ├── fix_hhkb_hfb_header64_crc.py  HFB ヘッダ/CRC 修正（qmk_to_hfb が使用）
│   ├── hfb_crc.py               HFB の CRC 計算
│   ├── gen_via_json.py          VIA 定義 JSON の生成
│   └── check_eeprom_refs.py     ファームが参照する EEPROM アドレスの検査
└── docs/                        手順書と解析記録
```

## ビルド

前提：`git`、`sudo` なしで動く `docker`（[docs/setup.md](docs/setup.md)）。

```sh
git clone <このリポジトリの URL>
cd <clone したディレクトリ>
git submodule update --init qmk_firmware      # QMK 本体（コミット固定）
./tools/build.sh                              # → qmk_firmware/.build/hhkb_hybrid_default.bin
./tools/build.sh via                          # VIA 対応版 → qmk_firmware/.build/hhkb_hybrid_via.bin
```

`qmk_firmware` 本体の取得は必須です（未取得なら `build.sh` が取得コマンドを案内して終了します）。本体があれば、ビルドに必要な QMK の下位 submodule（chibios / chibios-contrib / lufa / printf）は `build.sh` が自動で取得します。`build.sh` は実行のたびに `.build` を消すので、変換は直前にビルドした keymap に対して行ってください。詳細は [docs/build.md](docs/build.md)。

## 書き込み

事前に、udev ルールの設定と、純正 HFB（`https://origin.pfultd.com/downloads/hhkb/HHKB800_FW_A048.hfb`）を `local/HHKB800_FW_A048.hfb` に置いて sha256 を確認しておきます（[docs/setup.md](docs/setup.md)）。

```sh
# 1. QMK の .bin を純正更新用 HFB に変換（純正 HFB をテンプレートに使う）
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_default.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_default.hfb

# 2. 状態と書き込み先（FLASH TARGET）を確認（書き込むキーボード 1 台だけを接続）
python3 tools/flash.py --list

# 3. HID 経由で書き込み（更新モードに自動遷移。約 3 分）
python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_default.hfb
```

VIA 対応版（`./tools/build.sh via`）を書き込む場合は、ファイル名の `default` を `via` に読み替えてください（`local/hhkb_hybrid_via.hfb`。手順は [docs/via.md](docs/via.md)）。

すでに更新モード（`HHKB-Hybrid` と表示され、キー入力が効かない）の場合は `--resume` を付けます。書き込み中は VIA のページを閉じてください。状態の見分け方は [docs/device-state.md](docs/device-state.md)、途中で止まったときの対処は [docs/troubleshooting.md](docs/troubleshooting.md)。

## VIA

`via` keymap を書き込み、`via/hhkb_hybrid.json` を VIA に読み込ませると、キーマップ・マクロ・キーボード固有の設定（JIS/US 補正、自動スリープ時間、純正互換の Bluetooth キー操作）を VIA で変更できます。手順と制約は [docs/via.md](docs/via.md)。

## 純正 FW への復帰（分解不要）

動作中の QMK に対して `tools/flash.py` で純正 HFB（`HHKB800_FW_A048.hfb`）を書き込むか、Esc を押しながら USB 接続（bootmagic）して更新モードに入ってから書き戻します。VIA 版では **`QK_BOOT` キーで更新モードに入って純正に戻すのは避けてください**（設定領域が残り、純正のキー入力が崩れます）。手順は [docs/restore-stock.md](docs/restore-stock.md)。

## 再現性

QMK 本体は submodule でコミット固定（tag `0.34.4`）、ビルドに使う Docker イメージもダイジェストで固定しています。同じコミットからは同じバイナリが得られるはずです。照合は `build.sh` が表示する `.bin` の sha256 で行います（[docs/build.md](docs/build.md#再現性)）。

## ライセンス

このリポジトリはファイルごとにライセンスが異なります。各ファイル先頭の `SPDX-License-Identifier` が正です。

### ソースコードのライセンス（ファイル単位）

| 対象 | ライセンス | 備考 |
|---|---|---|
| `keyboards/hhkb_hybrid/matrix.c` | **GPL-2.0-or-later** | [Duncaen の HHKB Classic port](https://github.com/Duncaen/qmk_firmware/tree/hhkb_classic) を基にした派生物（Topre 静電容量スキャン）。当方の変更は有界ウェイト・リリースヒステリシス・純正キャリブレーション連携 |
| `keyboards/hhkb_hybrid/mcuconf.h` | **Apache-2.0** | ChibiOS のテンプレート（Copyright ChibiOS / Giovanni Di Sirio）。ヘッダを保持 |
| `keyboards/hhkb_hybrid/eeprom_hhkb.c` | **GPL-2.0-or-later** | QMK の STM32L0/L1 EEPROM ドライバ（`eeprom_stm32_L0_L1.c`）の書き込み手順を基にした派生物（VIA 用の EEPROM 領域を純正キーマップ表 B に限定） |
| 上記以外の当方作成ファイル（`hhkb_hybrid.c/.h`, `nrf_spi.c/.h`, `power.c/.h`, `led_indicator.c/.h`, `boards/HHKB_HYBRID/board.c/.h`, `halconf.h`, `chconf.h`, `keymaps/default/`, `keymaps/via/`, `config.h`, `post_rules.mk`, `keyboard.json`, `via/`, `tools/`, `docs/`） | **MIT**（`LICENSE`） | 純正 FW のリバースエンジニアリングによる独自実装 |
| `qmk_firmware/`（submodule） | GPL-2.0 ほか | QMK 本体・ChibiOS(Apache-2.0)・LUFA 等。各上流のライセンスに従う |

### ビルド成果物（重要）

ビルドされる `.hfb` には **PFU 製の nRF ファーム（proprietary バイナリ）が含まれる**ため、**生成 FW（`.hfb`）は配布できません**。各自が純正 `HHKB800_FW_A048.hfb` を用意し、自分の環境で変換・書き込みしてください。

なお `.bin` を含むビルド成果物は QMK 本体（GPL-2.0）や `matrix.c`（GPL-2.0-or-later）とリンクした結合著作物でもあります（本リポジトリのソース配布は各ファイルのライセンスに従う）。ただし上記のとおり、PFU 由来バイナリを含む生成 FW 自体は配布対象外です。

### 謝辞

Topre 静電容量マトリクスのスキャン実装は [Duncaen の HHKB Classic port](https://github.com/Duncaen/qmk_firmware/tree/hhkb_classic) を基にしています（同一メイン基板）。

### PFU/HHKB 由来物について

上記 MIT / GPL / Apache のいずれも、PFU/HHKB の公式 FW、公式ツール、公式ツールの展開物、公式 FW の逆アセンブル生成物、生成済み FW、その他 PFU/HHKB 由来のバイナリや派生物に対して権利を与えるものではありません。

このリポジトリでは、PFU/HHKB の公式 FW や公式ツール、生成済み FW を配布しません。利用者は公式サイトから `HHKB800_FW_A048.hfb` を自分で取得し、自分の環境で利用してください。

このプロジェクトは PFU/HHKB 非公式であり、PFU による承認・保証・サポートを受けたものではありません。PFU/HHKB のソフトウェア使用許諾および適用される法令を確認のうえ、利用者自身の責任で扱ってください。
