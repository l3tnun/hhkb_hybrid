# HHKB Professional HYBRID DFU Recovery Procedure

この文書は、HHKB Professional HYBRID 英語配列個体で、改造FW書き込み後に通常のUSB HID経由で認識できなくなった状態から、STM32 DFUを使って純正A048相当へ復旧した手順をまとめたもの。

## DFU を使う前に（必ず確認）

DFU は基板の R6/R85 をショートするために**キーボードの分解が必要**で、リスクを伴います。次に当てはまる場合、DFU は不要です。

- `lsusb -d 04fe:` に `04fe:0021` が表示される → HID 経由で書き込み直せます。`python3 tools/flash.py --list` で状態を確認し、キー入力が効かない `HHKB-Hybrid` なら更新モードなので `--resume` で書き込みます（[device-state.md](device-state.md)、[troubleshooting.md](troubleshooting.md)）。
- USB の認識が不安定（カーネルログに `error -71`）→ ケーブルの抜き差し、別ポートへの直結で回復することが多い既知の現象です。

DFU を検討するのは、[troubleshooting.md](troubleshooting.md) の「DFU（分解）を検討する前に」をすべて試しても `04fe:0021` が表示されない場合だけです。

- 分解手順と R6/R85 の位置を示す写真・図は、このリポジトリでは提供していません。
- `dfu-util` のコマンドは、カレントディレクトリにあるファイル（`HHKB800_FW_A048_app_08010000_64k.bin` など）を Docker に渡します。これらのファイルを置いたディレクトリ（例：`local/`）で実行してください。`tools/flash.py` のコマンドはリポジトリのルートで実行します。

> **重要：** このリポジトリの QMK を HID 経由で書き込んだ後に DFU で純正アプリを書き戻しても、ブートローダの起動判定（CRC）が QMK のままなので、**純正アプリは起動せず PFU 更新モード（`04fe:0021`、キー入力が効かない）で認識されます。** これは失敗ではありません。その場合は、下の「書き込み後の操作」の後に、HID 経由で純正 HFB を `--resume` で書き込んでください（末尾の「ブートローダーの起動判定との関係」）。

## 前提

- 対象: HHKB Professional HYBRID 英語配列
- 純正FWファイル: `HHKB800_FW_A048.hfb`
- 復旧に成功したDFUデバイス:

```text
0483:df11 STMicroelectronics STM Device in DFU Mode
```

- 使用したDFU alt setting:

```text
alt=0, name="@Internal Flash  /0x08000000/1536*128g"
```

以下の例では `dfu-util` を Docker 内で実行する（ホストにパッケージを追加せずに済むため。ホストに `dfu-util` があればそのまま使ってもよい）。USB デバイスをコンテナに渡すため `--privileged` を付けている。

## 重要な注意

`HHKB800_FW_A048.hfb` をそのままDFUで書き込まないこと。

今回の個体では、HFBファイルのoffset `4` がDFU上の `0x08010000` に対応していた。また、DFU表示は192KiB相当に見えるが、実際に `0x08020000` 以降を読み書きしようとするとデバイスが落ちた。

そのため、復旧で書き戻す対象は以下に限定する。

```text
書き込み元: HHKB800_FW_A048.hfb の offset 4 から 65536 bytes
書き込み先: 0x08010000
範囲:       0x08010000..0x0801ffff
```

触らない領域:

```text
0x08000000..0x0800ffff  ブートローダ領域
Option Bytes
DATA Memory
```

## 復旧用ファイル

生成済みファイル:

```text
HHKB800_FW_A048_app_08010000_64k.bin
size=65536
sha256=f9fab50e4194e88211e3431c888e377e69f77b41383f2f72952e1ad6ab04b1f7
```

元の純正HFB:

```text
HHKB800_FW_A048.hfb
size=282672
sha256=635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43
```

復旧用64KiBファイルを再生成する場合:

```bash
python3 - <<'PY'
from pathlib import Path
import hashlib

hfb = Path("HHKB800_FW_A048.hfb").read_bytes()
app = hfb[4:4 + 0x10000]
Path("HHKB800_FW_A048_app_08010000_64k.bin").write_bytes(app)
print(len(app), hashlib.sha256(app).hexdigest())
PY
```

期待値:

```text
65536 f9fab50e4194e88211e3431c888e377e69f77b41383f2f72952e1ad6ab04b1f7
```

## DFUモードに入る

1. USBを抜く
2. 電池を外す
3. 電源が入っていないことを確認する（電源ボタンは押さない）
4. R6/R85をショートする
5. ショートしたままUSBを接続する
6. `lsusb` でDFU認識を確認する

確認コマンド:

```bash
lsusb | grep -i -E '0483|dfu|stm'
```

期待例:

```text
Bus 001 Device 039: ID 0483:df11 STMicroelectronics STM Device in DFU Mode
```

## dfu-utilで認識確認

Docker上で `dfu-util -l` を実行する。

```bash
docker run --rm --privileged \
  -v "$PWD:/work" -w /work \
  debian:bookworm bash -lc '
    apt-get update >/dev/null &&
    apt-get install -y dfu-util >/dev/null &&
    dfu-util -l
  '
```

期待例:

```text
Found DFU: [0483:df11] ver=2200, devnum=39, cfg=1, intf=0, path="1-2", alt=2, name="@DATA Memory /0x08080000/2*3Ke", serial="..."
Found DFU: [0483:df11] ver=2200, devnum=39, cfg=1, intf=0, path="1-2", alt=1, name="@Option Bytes  /0x1FF80000/01*032 e", serial="..."
Found DFU: [0483:df11] ver=2200, devnum=39, cfg=1, intf=0, path="1-2", alt=0, name="@Internal Flash  /0x08000000/1536*128g", serial="..."
```

## 書き戻し

R6/R85をショートしたまま、以下を実行する。

```bash
docker run --rm --privileged \
  -v "$PWD:/work" -w /work \
  debian:bookworm bash -lc '
    apt-get update >/dev/null &&
    apt-get install -y dfu-util >/dev/null &&
    dfu-util -a 0 -s 0x08010000:leave -D HHKB800_FW_A048_app_08010000_64k.bin
  '
```

成功時の重要な出力:

```text
Downloading element to address = 0x08010000, size = 65536
Erase    done.
Download done.
File downloaded successfully
Submitting leave request...
Transitioning to dfuMANIFEST state
```

## 書き込み後の操作

1. R6/R85のショートを外す
2. USBを抜く
3. 電池も外す
4. 10秒待つ
5. USB接続する（必要なら電池を戻す）

その後、以下を確認する。

- USB入力できること
- Bluetooth入力できること
- LEDが通常どおり動作すること
- 電源ボタンの挙動が通常どおりであること

今回の復旧では、上記4項目すべてOKだった。

`python3 tools/flash.py --list` で `HHKB-Hybrid` と表示され、キー入力が効かない場合は PFU 更新モードで起動している（冒頭の「重要」を参照）。末尾の手順で純正 HFB を HID 経由で書き込む。

## 失敗した手順

以下の128KiB書き込みは失敗した。

```text
0x08010000 に 131072 bytes を書く
```

失敗時の出力:

```text
dfu-util: Error during special command "ERASE_PAGE" get_status
```

また、`0x08020000` からの読み出しでもDFUデバイスが落ちた。

このため、この個体のDFU復旧では `0x08010000` から64KiBだけを書き戻す手順を採用する。

## ブートローダーの起動判定との関係 (2026-09-13 追記)

PFUブートローダーは、アプリ領域 (`0x08010000` から64KiB) のCRC16がEEPROM `0x08080370` の期待値と一致するときだけアプリを起動する (詳細は `hardware.md`)。

- DFUの生書き込みはこの期待値を更新しない。
- 上記の復旧で純正が起動したのは、EEPROMの期待値が純正A048のCRCのままだったため。
- 別のFW (QMK等) をHID更新で書き込んだ後にDFUで純正アプリを書き戻すと、CRCが一致せずアプリは起動しない。この場合はPFU更新モード (`04fe:0021`) で列挙されるので、HID更新で純正HFBを書き込む。

```bash
# リポジトリのルートで実行する
python3 tools/flash.py --list                                                  # 更新モードの FLASH TARGET（input2）を確認
python3 tools/flash.py --resume --device /dev/hidrawN local/HHKB800_FW_A048.hfb
```

- QMK からは bootmagic（Escを押しながら接続）で分解せずにPFU更新モードへ入れる。DFUは不要（[restore-stock.md](restore-stock.md)）。

## メモ

HID経由での復旧ができない状態でも、R6/R85ショートによりSTM32 ROM DFUへ入れる場合は、この手順で復旧できる可能性がある。

ただし、DFUで見えているメモリ表示と実際に安全にアクセスできる範囲が一致しないことがあるため、`0x08020000` 以降へアクセスしない。
