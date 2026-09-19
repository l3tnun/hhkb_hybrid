# クイックスタート（ビルドから書き込みまで）

初めて書き込む人向けの全体の流れです。各手順の詳細はリンク先を参照してください。

> ⚠️ 非公式の改造です。**書き込むキーボード 1 台だけを USB 接続**し、手順を理解したうえで行ってください。分解（DFU）が必要になることは通常ありません。

## 0. 準備（最初の 1 回だけ）

[setup.md](setup.md) に従って、次を済ませます。

- リポジトリと `qmk_firmware` submodule の取得
- Docker が `sudo` なしで動くこと
- udev ルール（キーボードの hidraw へのアクセス権）
- 純正 HFB を `local/HHKB800_FW_A048.hfb` に置き、sha256 を確認

## 1. 対応機種か確認する

HHKB Professional HYBRID の英語配列（USB ID `04fe:0021`）だけが対象です。確認方法は [device-state.md](device-state.md#対応機種の確認)。

## 2. ビルドする

どちらか一方を選びます。

```sh
./tools/build.sh          # default：VIA 非対応（キー配置はファームに固定）
./tools/build.sh via      # via：VIA でキー配置や設定を変更できる版（docs/via.md）
```

- 成果物は `qmk_firmware/.build/hhkb_hybrid_<keymap>.bin` です（`<keymap>` は `default` または `via`）。
- **`build.sh` は実行のたびに `.build` を消します。** 次の変換は、直前にビルドした keymap に対して行ってください。
- 詳細（ビルドオプション、再現性）は [build.md](build.md)。

## 3. HFB に変換する

```sh
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
```

（default の場合はファイル名の `via` を `default` に読み替えてください。）

出力の確認点：

```
template: HHKB800_FW_A048.hfb (A0.48) (sha256 ok)
wrote local/hhkb_hybrid_via.hfb
qmk size: ... bytes (0x....) sha256=...
header64 0x11a3->0x....
stored_crc=0x.... match=True
```

`template: ... (sha256 ok)` と `match=True` が出れば変換できています。

### ビルド済みの HFB を書き込む場合

手順 2〜3 を省いて手元の HFB を書き込むときは、次を確認してください。

- 出どころと版が分かっていること。分からないファイル（名前だけでは版を判断できないものなど）は使わず、手順 2〜3 からやり直します。
- `python3 tools/flash.py --dry-run <HFB>` で `match=True`、および `hfb_kind=QMK`（QMK）または `hfb_kind=stock (no QMK)`（純正）が意図どおりであること。
- 純正 HFB は `sha256sum` が [setup.md](setup.md#4-純正-hfb-の用意) の値と一致すること（`--dry-run` では `header_word=0x11a3`）。

## 4. キーボードの状態と書き込み先を確認する

- VIA のページ（ブラウザのタブ）を開いている場合は閉じます。
- 書き込むキーボードをこの PC と Bluetooth でペアリングしている場合は、PC 側でペアリングを削除しておきます（転送が途中で止まることがあります。[troubleshooting.md](troubleshooting.md)）。
- 書き込むキーボード 1 台だけを USB 接続します。

```sh
python3 tools/flash.py --list
```

`state:` の行で状態を、`FLASH TARGET` の行で書き込み先の `/dev/hidrawN` を確認します。純正 FW と更新モードは表示が同じなので、キー入力が効くかで見分けます。見方は [device-state.md](device-state.md)。

## 5. 開始状態に合わせて書き込む

| いまの状態 | 書き込むもの | コマンド |
|---|---|---|
| 純正 FW（キー入力が効く） | QMK の HFB | `python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb` |
| QMK | QMK の HFB（更新） | 同上。VIA の設定は通常残ります（EEPROM の配置が変わる版を書いた場合、default 版と via 版を切り替えた場合は初期化されます） |
| QMK | 純正 HFB（純正に戻す） | `python3 tools/flash.py --device /dev/hidrawN local/HHKB800_FW_A048.hfb`（[restore-stock.md](restore-stock.md)） |
| 更新モード（キー入力が効かない） | 任意の HFB | `python3 tools/flash.py --resume --device /dev/hidrawN <HFB>` |

- `/dev/hidrawN` は手順 4 の `FLASH TARGET` です。
- 事前確認だけしたい場合は `--dry-run` を付けます（HFB の CRC・種類（`hfb_kind`）・パケット数を表示して終了し、キーボードには触れません）。
- 記録を残したい場合は `--trace-log local/flash_<日付>.jsonl` を付けます。

### 正常時の出力（QMK から QMK へ書き込む例）

```
size=282672 crc=0x.... computed_crc=0x.... match=True
header_word=0x.... e2_body_size=282670 e2_packets=4960
hfb_kind=QMK
device=/dev/hidrawN
waiting for firmware-update mode... 10s
...
waiting for firmware-update mode... 1s
device changed after entering update mode: /dev/hidrawM
sent 0/4960
sent 128/4960
...
sent 4864/4960
done
```

- 純正 HFB を QMK に書き込むときは、`device=` の次に `stock HFB on a QMK keyboard: clearing the stock keymap window (F0)` が出ます。
- 更新モードに入った後、hidraw の番号が変わらなければ `device changed ...` の行は出ません。
- `--resume` のときは `waiting for firmware-update mode` の行が出ず、すぐ `sent` に進みます。
- **所要時間は約 3 分**（更新モードへの移行を含めて約 175 秒、`--resume` なら約 160 秒）。`sent` の行は約 4 秒ごとに進みます。

### 書き込み中にやってはいけないこと

- USB ケーブルを抜く（`flash.py` が抜き差しを求めた場合を除く）
- 電池を外す、電源ボタンを長押しする
- VIA のページを開く、別のターミナルから `flash.py` を実行する
- Ctrl+C で中断する

途中で止まった場合も、キーボードは更新モードのままなので `--resume` でやり直せます。分解は不要です。対処は [troubleshooting.md](troubleshooting.md)。

## 6. 書き込み後に確認する

`done` の数秒後にキーボードが再起動します。

```sh
python3 tools/flash.py --list
```

| 書き込んだもの | 期待する表示 |
|---|---|
| QMK | `product='HHKB Hybrid QMK' bcdDevice=0020`、`state: QMK (this repository)` |
| 純正 FW | `product='HHKB-Hybrid' bcdDevice=0001`（キー入力が効くこと） |

そのうえで、実際にキー入力（USB）と、USB を抜いた電池駆動での Bluetooth 入力を確認します。VIA 対応版は [via.md](via.md) の手順で VIA に接続できることも確認してください。

## 次に読むもの

- キー操作・Bluetooth・電源：[usage.md](usage.md)
- VIA：[via.md](via.md)
- 純正へ戻す：[restore-stock.md](restore-stock.md)
- 困ったとき：[troubleshooting.md](troubleshooting.md)
