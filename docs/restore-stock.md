# 純正 FW への復帰（分解不要）

QMK から純正ファームウェアへ、**分解せずに**戻せます。実機で確認済みの手順です。

## 準備

- 純正 HFB `local/HHKB800_FW_A048.hfb`（入手と検証は [setup.md](setup.md)）
- 書き込むキーボード 1 台だけを USB 接続し、VIA のページを閉じる

## 仕組みと注意（VIA 対応版を使っていた場合）

VIA 対応版（`via` keymap）は、キーマップや設定を**純正 FW のキーマップ表 B と同じ EEPROM 領域**（`0x08080700–0x08080CFF`）に保存しています。純正に戻したときは次のように動きます。

1. 起動時に、まずブートローダが純正キーマップの有効マーク（`0x08080300`）を確認します。無効なら、キーマップ表 A だけを既定値で作り直してマークを有効に戻します（表 B には触れません）。
2. 続いて純正 FW がマークを有効と見て、表 A と**表 B**を読み込みます。

表 B に VIA のデータが残っていると、純正 FW はそれをキーマップとして使うため、**入力できないキーや刻印と違う文字になるキーが多発します**。このため VIA 対応版から戻すときは、表 B を純正の状態（ゼロ）に戻してから書き込む必要があります。下の手順 A / B は、どちらもこれを自動で行います。

**QMK の `QK_BOOT` キー（VIA の Reset / Boot キー）で更新モードに入って純正に戻すのは避けてください。** QMK の更新にも使う経路なので、表 B を消しません。

`default` keymap（VIA 非対応）は表 B を使わないため、どの手順で戻しても問題ありません。

## 手順 A：flash.py で書き込む（推奨）

キーボードで QMK が動いている状態で行います。

```sh
python3 tools/flash.py --list                                             # FLASH TARGET（input1、06 60 ff）を確認
python3 tools/flash.py --device /dev/hidrawN local/HHKB800_FW_A048.hfb
```

- `flash.py` は HFB が純正（QMK を含まない）だと判定し、QMK に `AA AA F0` を送ります。出力に `stock HFB on a QMK keyboard: clearing the stock keymap window (F0)` と表示されます。VIA 対応版の QMK は、表 B を消してから更新モードに入ります。
- default 版（VIA 非対応）の QMK では、続けて `F0 not supported (non-VIA QMK build, window unused); using E0` と表示されます。default 版は表 B を使わないので、これは正常です。
- このコマンドを持たない古い VIA 対応版の場合、`flash.py` は書き込まずに中止します。先に最新の VIA 対応版を書き込んでから、改めて実行してください。
- 所要時間は約 3 分です。途中で止まった場合の対処は [troubleshooting.md](troubleshooting.md)。

## 手順 B：bootmagic で更新モードに入ってから書き込む

QMK が起動しているがキー入力や Raw HID が使えない場合などに使います。

### 1. 更新モードに入る

1. 電池駆動で電源が入っている場合は、電源ボタンを 2 秒長押しして OFF にします（電源が入ったまま USB を挿しても再起動しないため、bootmagic が働きません）。
2. USB ケーブルを抜いた状態で、キーボード左上の **Esc を押し続けます**。
3. Esc を押したまま USB ケーブルを接続し、数秒待ってから離します。
4. `python3 tools/flash.py --list` で `product='HHKB-Hybrid' bcdDevice=0001` と表示され、キー入力が効かなければ更新モードです。

bootmagic は QMK の設定を初期化します。VIA 対応版では表 B もゼロに戻します。

### 2. 純正 HFB を書き込む

更新モードに入っているので、`--resume` を付けて書き込みます。

```sh
python3 tools/flash.py --list                                                   # FLASH TARGET（input2、06 00 ff）を確認
python3 tools/flash.py --resume --device /dev/hidrawN local/HHKB800_FW_A048.hfb
```

更新モードの USB 認識に失敗して `FLASH TARGET` が表示されない場合（カーネルログに `error -71`）は、Esc を押さずに USB を抜き差しすると、更新モードのまま認識し直されます。

## 確認

書き込みの数秒後に再起動し、純正 FW として認識されます。起動直後に一度切断と再接続が起きることがあります。

- `python3 tools/flash.py --list` で `product='HHKB-Hybrid' bcdDevice=0001`（更新モードと同じ表示なので、次のキー入力で判断します）
- 刻印どおりに入力できること、純正の Fn 配置になっていること
  - **青 LED が点灯したままで、キーが入力できない場合：** 純正 FW が Bluetooth の再接続を続けていて、入力が USB に出ていない状態です（QMK で Bluetooth に登録していた接続先が、純正 FW に引き継がれるため）。**Fn + Ctrl + 0** で出力先を USB に切り替えると入力できます。
- USB を抜いて、電池での Bluetooth 接続・入力ができること

## すでにキー入力が崩れてしまった場合

VIA 対応版から表 B を消さずに純正へ戻してしまい、純正でキー入力が崩れている場合は、次の順で直せます（HID 経由で書き込むため、キー入力が崩れていても作業できます）。

1. 純正 FW のまま、最新の VIA 対応版の QMK を書き込む（`--list` の `FLASH TARGET` は純正 FW の `input2`。`--resume` は付けない）。
2. 手順 A で純正 HFB を書き込む（表 B が消去されます）。
3. 純正での入力を確認する。

## 補足

- JIS/US 補正の ON/OFF は、純正のスリープ時間設定バイトの最下位ビットに保存しています。純正に戻してもこのビットは残るため、補正を ON にしていた場合、純正の自動スリープ時間が 1 分ずれることがあります（例：30 分 → 31 分）。純正の設定ツールでスリープ時間を設定し直すと解消します（[jis-us-toggle.md](jis-us-toggle.md)）。
- HID 経由で戻せなくなった場合の最終手段が DFU です（基板の R6/R85 ショート＝物理分解が必要）。その前に [troubleshooting.md](troubleshooting.md) のチェックを試してください。
