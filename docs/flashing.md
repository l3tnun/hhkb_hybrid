# 書き込み手順（HID 経由）

純正の更新と同じ HID 更新経路（E0/E1/E2/E3）で、STM32 のアプリ領域にファームを書き込みます。**分解は不要**です。初めての場合は [quickstart.md](quickstart.md) の流れに沿って進めてください。

## 前提

- [setup.md](setup.md) の準備が済んでいること（Linux、udev ルール、純正 HFB を `local/` に配置）
- 書き込むキーボード 1 台だけを USB 接続していること
- VIA のページ（ブラウザのタブ）を閉じていること
- キーボードが純正 FW・QMK・更新モードのいずれかで USB に認識されていること（[device-state.md](device-state.md)）

## 1. ビルドと変換

```sh
./tools/build.sh via
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
```

- default 版は `via` を `default` に読み替えます。詳細は [build.md](build.md)。
- `template: HHKB800_FW_A048.hfb (A0.48) (sha256 ok)` と `stored_crc=0x.... match=True` が出ることを確認します。
- `match=True` は、変換したファイルのファイル CRC が正しく設定されたことを示すだけです（ビルドの再現性の確認ではありません。[build.md](build.md) の「再現性」を参照）。

## 2. 書き込み先の確認

```sh
python3 tools/flash.py --list
```

`FLASH TARGET` の行の `/dev/hidrawN` を使います。見方と、純正 FW と更新モードの見分け方は [device-state.md](device-state.md)。

| 状態 | 書き込み先 | レポートディスクリプタの先頭 |
|---|---|---|
| 純正 FW / 更新モード | `input2`（ベンダー定義 raw HID、64 バイトレポート） | `06 00 ff` |
| QMK | `input1`（QMK の Raw HID、32 バイトレポート） | `06 60 ff` |

## 3. 書き込み

```sh
# 純正 FW または QMK が動いている場合
python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb

# すでに更新モードに入っている場合（キー入力が効かない HHKB-Hybrid）
python3 tools/flash.py --resume --device /dev/hidrawN local/hhkb_hybrid_via.hfb
```

`flash.py` の動き：

1. HFB のファイル CRC を検査します（不一致なら中止）。
2. HHKB が 1 台だけ接続されていること、`--device` が書き込み先インターフェースであることを検査します。
3. （`--resume` でなければ）更新モードへの移行コマンドを送ります。
   - 純正 FW・QMK へは `AA AA E0`。QMK の VIA 対応版の設定は残ります。
   - **動作中の QMK に純正 HFB を書き込む場合は `AA AA F0`。** VIA 対応版は、設定を保存している純正キーマップ領域をゼロに戻してから更新モードに入ります（[restore-stock.md](restore-stock.md)）。
4. 10 秒待ち、再認識された更新モードの書き込み先（`input2`、`06 00 ff`）を自動で選び直します。見つからなければ、他のインターフェースは使わず、USB の抜き差しを案内して待ちます。
5. E1（開始）→ E2（57 バイトずつ 4,960 パケット）→ E3（終了）を送り、`done` で完了です。

正常時の出力例と所要時間（約 3 分）は [quickstart.md](quickstart.md#正常時の出力qmk-から-qmk-へ書き込む例) を参照してください。

## オプション

| オプション | 用途 |
|---|---|
| `--list` | 接続中の HHKB の状態と書き込み先を表示する（HFB の指定は不要。キーボードには何も送らない） |
| `--device /dev/hidrawN` | 書き込み先。省略すると、書き込み先がちょうど 1 つのときだけ自動で選ぶ（省略できますが、`--list` で確認して明示することをおすすめします） |
| `--dry-run` | HFB の CRC、種類（`hfb_kind=QMK` / `hfb_kind=stock (no QMK)`）、パケット数を表示して終了する（キーボードには触れない） |
| `--resume` | すでに更新モードのときに使う。移行コマンドを送らず E1 から始める（QMK が動いていると中止） |
| `--trace-log <ファイル>` | 送受信の記録を JSON Lines で追記する（例：`--trace-log local/flash_$(date +%Y%m%d_%H%M).jsonl`） |
| `--replug-timeout <秒>` | 更新モードの書き込み先が現れないときに、USB の抜き差しを待つ時間（既定 180） |
| `--allow-crc-mismatch` | 診断用。**通常の書き込みでは使わないでください** |

## 4. 書き込み後の確認

- `python3 tools/flash.py --list` で、QMK なら `product='HHKB Hybrid QMK' bcdDevice=0020`、純正 FW なら `product='HHKB-Hybrid' bcdDevice=0001` と表示されること
- USB でキー入力できること、USB を抜いて Bluetooth で入力できること
- VIA 対応版なら、[via.md](via.md) の読み出しで `protocol 0x000d` と表示されること（VIA に接続できること）

## 途中で止まった場合

- 更新モードに入った後に止まっても、キーボードは更新モードのまま待機しています。**`--resume` で何度でもやり直せます。**
- 更新モードへの再接続時に USB の認識が失敗することがあります（カーネルログに `error -71`）。このとき `flash.py` は `unplug and replug the USB cable` と表示して待つので、**USB ケーブルを抜き差し**してください。認識されれば自動で書き込みを続けます。
- エラーメッセージ別の対処は [troubleshooting.md](troubleshooting.md) にまとめています。

## 注意

- 書き込むのは STM32 のアプリ領域（`0x08010000`〜、64 KiB）と、純正の更新処理も書き込む EEPROM のアドレスだけです。PFU ブートローダは変更しません。
- HFB には純正の Bluetooth（nRF）ファームも含まれ、書き込みのたびに一緒に送られます。
- 書き込みの仕組み（HFB 形式、E0〜E3、F0）は [reverse-engineering/implementation.md](reverse-engineering/implementation.md)。
- USB に `04fe:0021` がまったく表示されなくなった場合の最終手段は DFU（分解が必要）です（[recovery.md](recovery.md)）。その前に [troubleshooting.md](troubleshooting.md) のチェックを試してください。
