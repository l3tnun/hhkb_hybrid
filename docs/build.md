# ビルド手順

事前に [setup.md](setup.md) の準備（git、`sudo` なしで動く Docker、`qmk_firmware` submodule の取得）を済ませてください。ARM ツールチェインをホストに入れる必要はありません。ビルドは Docker の中で完結します。

## ビルド

**リポジトリのルートで実行してください。**

```sh
./tools/build.sh                                   # keymap = default（VIA 非対応）
./tools/build.sh via                               # keymap = via（VIA 対応。docs/via.md）
./tools/build.sh via -e HHKB_JIS_US_TOGGLE=no      # 2 つ目以降の引数は qmk compile にそのまま渡る
```

終了時に次のように表示されます。

```
Built: <リポジトリ>/qmk_firmware/.build/hhkb_hybrid_via.bin
<sha256>  <リポジトリ>/qmk_firmware/.build/hhkb_hybrid_via.bin

Next: convert to an HFB and flash (see docs/quickstart.md):
  python3 tools/qmk_to_hfb.py ".../hhkb_hybrid_via.bin" local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
  python3 tools/flash.py --list
  python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb
```

### ビルドオプション

| オプション | 内容 |
|---|---|
| `-e HHKB_JIS_US_TOGGLE=no` | JIS/US 補正の機能をビルドから外す（[jis-us-toggle.md](jis-us-toggle.md)） |

## 成果物

- 成果物は `qmk_firmware/.build/hhkb_hybrid_<keymap>.bin` です。
- **`build.sh` は実行のたびに `qmk_firmware/.build` を削除します。** `default` と `via` を続けてビルドすると、先にビルドした方は消えます。HFB への変換は、直前にビルドした keymap に対して行ってください。
- ビルドオプションを付けても、出力ファイル名は同じです。
- QMK は `qmk_firmware/` 直下にも同じ `.bin` のコピーを置きますが、使わないでください（`build.sh` は実行時に古いコピーを消します）。
- 64 KiB を超えるとアプリ領域に収まらないため、`qmk_to_hfb.py` が変換を中止します。

## HFB への変換

```sh
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
```

- 純正 HFB（`local/HHKB800_FW_A048.hfb`）をテンプレートに、STM32 のアプリ領域だけを差し替えて CRC を計算し直します。
- テンプレートの sha256 が公式の A0.48 と一致しない場合は中止します（`--allow-unknown-template` で上書きできますが、使わないでください）。
- 表示の意味：

| 表示 | 意味 |
|---|---|
| `template: HHKB800_FW_A048.hfb (A0.48) (sha256 ok)` | テンプレートが公式の純正 HFB |
| `qmk size: N bytes ... sha256=...` | 変換元の `.bin` のサイズと sha256（再現性の照合に使える） |
| `header64 0x11a3->0x....` | アプリ領域の CRC（ブートローダの起動判定に使う値）。純正は `0x11a3`、右が書き込むファームの値 |
| `stored_crc=0x.... match=True` | HFB のファイル CRC が正しく設定された（変換直後の自己確認） |

## `build.sh` の動作

1. `qmk_firmware` 本体が無ければ、取得コマンドを案内して終了する
2. 必要な QMK の submodule（`lib/chibios`, `lib/chibios-contrib`, `lib/lufa`, `lib/printf`）が無ければ取得する
3. このリポジトリの `keyboards/hhkb_hybrid/` を `qmk_firmware/keyboards/` にコピーする（このリポジトリが正）
4. `qmk_firmware/.build` と直下の古い `.bin` / `.hex` を削除する
5. ダイジェストで固定した QMK 公式イメージ（`ghcr.io/qmk/qmk_cli@sha256:b7d7fa8f...`、QMK CLI 1.2.0 / arm-none-eabi-gcc 15.2.0）で `qmk compile -kb hhkb_hybrid -km <keymap> [オプション]` を実行する
6. 成果物のパスと sha256、次の手順を表示する

- `qmk_firmware/keyboards/hhkb_hybrid/` はビルドのたびに上書きされる作業用のコピーです。編集はリポジトリ直下の `keyboards/hhkb_hybrid/` で行ってください（コピーは `.gitmodules` の設定により git status に表示されません）。
- Docker で `qmk compile` を直接実行する場合は、事前に `build.sh` を実行して最新の定義をコピーしておいてください。そうしないと古い定義でビルドされます。

## 再現性

- `qmk_firmware` は submodule として特定のコミット（tag `0.34.4`）に固定されています。下位の submodule も `qmk_firmware` のコミットで固定されます。
- ビルドに使う Docker イメージもダイジェストで固定しています。
- そのため、同じコミットからは同じ `.bin` が得られるはずです。**確認するには、別の環境でビルドした `.bin` の sha256**（`build.sh` と `qmk_to_hfb.py` が表示）**を比べてください。** アプリ領域の CRC（`header64` の右側の値）でも照合できます。
- `qmk_to_hfb.py` の `match=True` は変換直後の自己確認なので、再現性の確認にはなりません。

## EEPROM 参照の検査

ビルドしたファームが、許可した EEPROM アドレス（ブートローダの起動判定、キャリブレーション、VIA の保存領域など）以外を参照していないかを確認できます。EEPROM に関わる変更をしたときは実行してください。

```sh
python3 tools/check_eeprom_refs.py qmk_firmware/.build/hhkb_hybrid_via.bin
```

最後に `result: OK` と表示されれば問題ありません。

## VIA 定義 JSON の生成

`via/hhkb_hybrid.json` は `keyboards/hhkb_hybrid/keyboard.json` から生成します。配列・独自キーコード・HHKB メニューを変えたら、再生成してコミットしてください。

```sh
python3 tools/gen_via_json.py
```

## QMK Userspace について

このリポジトリは QMK Userspace の構成（`qmk.json` + `keyboards/`）です。固定している QMK `0.34.4` は外部の Userspace にあるキーボード定義を直接解決しないため、`build.sh` がキーボード定義を本体へコピーしてからビルドします（QMK 公式の Userspace GitHub Action も同様にコピーします）。

## QMK 本体・ビルドイメージを更新する場合

再現性の基準が変わるため、意図的な更新のときだけ行ってください。

```sh
cd qmk_firmware
git fetch origin
git checkout <新しいコミット/タグ>
cd ..
git add qmk_firmware
git commit -m "Bump qmk_firmware to <version>"
```

- ビルドイメージを更新する場合は、`tools/build.sh` の `QMK_IMAGE` のダイジェストを書き換えます。
- 更新後は必ず再ビルドし、`check_eeprom_refs.py` と実機での動作を確認してください。
- QMK 本体の更新で eeconfig のサイズなど EEPROM の配置が変わる場合は、VIA 対応版の `HHKB_VIA_LAYOUT_VERSION` を上げる必要があります（[via.md](via.md) の保守メモ）。
