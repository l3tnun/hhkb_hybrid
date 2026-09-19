# キーボードの状態の見分け方と書き込み先の選び方

書き込む前に、**どの状態のキーボードがつながっているか**と、**どの `/dev/hidraw*` に送るか**を必ず確認します。取り違えると書き込みが失敗したり、別の個体に書き込んだりします。

## まず接続は 1 台だけ

- **書き込むキーボード 1 台だけを USB 接続してください。** 他の HHKB は USB から外します（Bluetooth 接続は関係ありません）。
- 純正 FW と更新モードの HHKB は USB シリアル番号を持たないため、複数台つながっていると USB 上で区別できません。
- `tools/flash.py` は、`04fe:0021` の USB デバイスがちょうど 1 台でないと書き込みを始めません。

## `flash.py --list` で確認する

```sh
python3 tools/flash.py --list
```

出力例（QMK が動いている場合。番号やシリアルは環境によって変わります）：

```
USB 3-1: product='HHKB Hybrid QMK' bcdDevice=0020 serial=XXXXXXXXXXXXXXXX
  state: QMK (this repository)
  /dev/hidraw3  input0  descriptor=05 01 09  not a flash target
  /dev/hidraw4  input1  descriptor=06 60 ff  FLASH TARGET
  /dev/hidraw5  input2  descriptor=05 01 09  not a flash target
```

出力例（純正 FW または更新モードの場合）：

```
USB 3-1: product='HHKB-Hybrid' bcdDevice=0001 serial=-
  state: stock firmware or PFU update mode (keys work = stock, keys do nothing = update mode)
  /dev/hidraw3  input0  descriptor=05 01 09  not a flash target
  /dev/hidraw4  input1  descriptor=05 01 05  not a flash target
  /dev/hidraw5  input2  descriptor=06 00 ff  FLASH TARGET
```

- **`FLASH TARGET` と表示された行の `/dev/hidrawN` を `--device` に指定します。** それ以外（キーボード入力用など）へ送ると失敗します。`flash.py` も書き込み前にこれを検査し、違えば中止します。
- `no read/write permission` と出る場合は、udev ルールを設定してください（[setup.md](setup.md)）。
- USB デバイスは表示されるのに hidraw が 1 つも無い場合（`no hidraw nodes`）や、hidraw はあるが `FLASH TARGET` の行が無い場合（`WARNING: no FLASH TARGET`）は、USB の認識に一部または全部失敗しています。**表示された他の hidraw を書き込み先に使わず**、ケーブルを抜き差ししてから `--list` をやり直してください（[troubleshooting.md](troubleshooting.md)）。
- `no HHKB (04fe:0021) found.` と出る場合は、ケーブル・ポートを確認してください。対応機種でない可能性もあります（下の「対応機種」）。

## 状態の一覧

| 状態 | product | bcdDevice | serial | キー入力 | 書き込み先 | `flash.py` の使い方 |
|---|---|---|---|---|---|---|
| **純正 FW** | `HHKB-Hybrid` | `0001` | なし | **効く** | `input2`（`06 00 ff`） | `--resume` を付けない |
| **PFU 更新モード** | `HHKB-Hybrid` | `0001` | なし | **効かない** | `input2`（`06 00 ff`） | **`--resume` を付ける** |
| **QMK（default / via）** | `HHKB Hybrid QMK` | `0020` | あり | 効く | `input1`（`06 60 ff`） | `--resume` を付けない |

- 純正 FW と更新モードは USB 上の表示がまったく同じです。**キーを押して入力できるか**で見分けます（テキストエディタなどで確認）。
- ただし純正 FW でも、**青 LED が点灯したまま**なら Bluetooth の再接続中で、入力が USB に出ません。先に **Fn + Ctrl + 0** で USB に切り替えてから判断してください。
- `--resume` は「すでに更新モードに入っている」ときだけ使います。`flash.py` は QMK に `--resume` を指定すると中止しますが、純正 FW と更新モードは区別できないため、この判断は利用者が行います。

### QMK が default 版か VIA 対応版か

`--list` では区別できません。VIA 対応版は VIA の問い合わせに応答するので、[via.md](via.md) の「動作中のファームを確認する（書き込みなし）」の読み出しで確認できます（`protocol 0x000d` と出れば VIA 対応版）。

## 意図せず更新モードになる場合

「`HHKB-Hybrid` と表示され、キーがまったく効かない」ときは、故障ではなく**更新モードに入っている**可能性が高いです。分解（DFU）は不要で、`--resume` でファームを書き込めば戻ります。

更新モードに入る主な原因：

| 原因 | 説明 |
|---|---|
| bootmagic | Esc を押しながら USB を接続した |
| 書き込みの中断 | 更新モードへの移行後に書き込みが止まった |
| QMK のフェイルセーフ（ウォッチドッグ） | QMK がハングして約 5〜11 秒でウォッチドッグがリセットすると、次の起動で更新モードに入る |
| QMK のフェイルセーフ（USB 未構成） | USB 給電中に、ホストが列挙を始めたのに 20 秒以内に構成が完了しなかった場合も、ウォッチドッグ経由で更新モードに入る（通信の途中で止まった状態から抜けるため） |

更新モードに入った後は、何度抜き差ししても更新モードのままです（ファームの書き込みが完了するまで起動フラグが落ちたままになるため）。

## 対応機種の確認

- 対象は **HHKB Professional HYBRID（英語配列）** で、USB ID が `04fe:0021` の機種です。純正ファームの更新ファイルは `HHKB800_FW_A048.hfb`（`PD-KB800` 系）です。
- 実機で確認しているのは、USB ID `04fe:0021` の英語配列の個体だけです。
- 日本語配列、HHKB Classic、HHKB Studio、その他の機種は対象外です（USB ID や更新ファイルが異なる可能性があり、未確認です）。
- 確認方法：
  - キーボード底面のラベルで型番（`PD-KB800` 系）を確認する
  - `lsusb -d 04fe:` の結果に `04fe:0021` が表示されることを確認する
- `tools/flash.py` は `04fe:0021` 以外を書き込み対象にしません。

## 手作業で確認する方法（参考）

`flash.py --list` は sysfs の次の情報を読んでいます。

```sh
for h in /sys/class/hidraw/hidraw*; do
  grep -q 'HID_ID=0003:000004FE:00000021' "$h/device/uevent" 2>/dev/null || continue
  usb=$(dirname "$(dirname "$(readlink -f "$h/device")")")
  printf '%s  %s  %s  bcdDevice=%s  descriptor=%s\n' "/dev/${h##*/}" \
    "$(sed -n 's/^HID_NAME=//p' "$h/device/uevent")" \
    "$(sed -n 's/^HID_PHYS=.*\/\(input[0-9]*\)$/\1/p' "$h/device/uevent")" \
    "$(cat "$usb/bcdDevice")" \
    "$(od -An -tx1 -N3 "$h/device/report_descriptor" | tr -s ' ' | sed 's/^ //')"
done
```

キーボードのデバイスファイルは開かないので、アクセス権が無くても実行できます。
