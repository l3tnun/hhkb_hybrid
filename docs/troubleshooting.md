# トラブルシューティング

## 最初に確認すること

- **書き込むキーボードを、書き込みに使う PC と Bluetooth でペアリングしたままにしないでください。** 転送が途中で止まる原因になることがあります（下の「転送が同じ位置で毎回止まる」）。

- **キーボードの USB 表示が出ている限り、分解（DFU）は不要です。** `lsusb -d 04fe:` に `04fe:0021` が表示されていれば、HID 経由で書き込み直せます。
- 状態の確認：`python3 tools/flash.py --list`（[device-state.md](device-state.md)）
- USB のエラーはカーネルログで確認できます：`journalctl -k -f`（または `sudo dmesg -w`）

## やってはいけないこと

- `--allow-crc-mismatch` を付けて書き込む（HFB が壊れている可能性があります。変換からやり直してください）
- `FLASH TARGET` 以外の hidraw に順番に書き込みを試す
- 書き込み中に USB ケーブルを抜く・電池を外す（`flash.py` が抜き差しを求めた場合を除く）
- 複数の HHKB を同時に接続したまま書き込む
- USB に `04fe:0021` が見えているのに DFU（分解）に進む

## flash.py のメッセージ別の対処

| 表示 | 意味 | キーボードの状態 | 対処 |
|---|---|---|---|
| `no read/write permission for /dev/hidrawN; set up the udev rule (docs/setup.md)` / `PermissionError` | hidraw へのアクセス権が無い | 変化なし | [setup.md](setup.md) の udev ルールを設定し、ケーブルを抜き差しする |
| `expected exactly one HHKB connected, found N.` | HHKB が 0 台、または 2 台以上 | 変化なし | 書き込むキーボード 1 台だけを接続する |
| `specify --device; use --list to find the FLASH TARGET` | 書き込み先を自動で決められない | 変化なし | `--list` の `FLASH TARGET` を `--device` に指定する |
| `/dev/hidrawN is not the flash target interface (descriptor ...)` | キーボード入力用などのインターフェースを指定した | 変化なし | `--list` の `FLASH TARGET` を指定する |
| `--resume is only for a keyboard already in the PFU update mode; ...` | QMK に `--resume` を指定した | 変化なし | `--resume` を外して実行する |
| `CRC mismatch: stored=0x.... computed=0x....` | HFB ファイルが壊れている | 変化なし | `qmk_to_hfb.py` で変換し直す。純正 HFB なら取得し直して sha256 を確認する |
| `this QMK VIA build cannot clear the stock keymap window (no F0); ...` | 動作中の VIA 対応版が古く、純正へ戻す準備ができない | 変化なし | 先に最新の VIA 対応版を書き込んでから、改めて純正 HFB を書き込む |
| `mode change failed: ... Nothing was written. ...` | 更新モードへの移行コマンドの送受信が失敗した | 通常は変化なし（書き込みなし）。ただしコマンドが届いていて更新モードに入っている場合がある | `--list` で `HHKB-Hybrid` と表示され、キー入力が効かなければ更新モードなので `--resume` で書き込む。それ以外は再実行する |
| `update-mode HID interface not found (USB enumeration may have failed).` `unplug and replug the USB cable; waiting up to 180s ...` | 更新モードに入った後、USB の認識に失敗した（下の「USB エラー（-71）」） | 更新モード | **USB ケーブルを抜き差し**する。見つかれば自動で書き込みを続ける |
| `update-mode interface did not appear; replug the keyboard and rerun with --resume ...` | 抜き差しを待つ時間（既定 180 秒）を過ぎた | 更新モード | 抜き差しして `--list` で `FLASH TARGET` を確認し、`--resume --device /dev/hidrawN <HFB>` で再実行する |
| `transfer failed: ...` `The keyboard stays in the PFU update mode ...` | 転送中にエラー（切断、タイムアウト、応答不一致など） | 更新モード | 必要なら抜き差しし、`--list` で確認して `--resume --device /dev/hidrawN <HFB>` で再実行する |
| `transfer failed: ...` `If keys still work, the keyboard is running the stock firmware ...`（`--resume` 指定時） | `--resume` で E1 以降が失敗した | 更新モード、または純正 FW（`--resume` を誤って付けた） | キー入力が効くなら純正 FW なので `--resume` を外して再実行する。効かなければ上の行と同じ |
| `/dev/hidrawN is not an HHKB hidraw node; use --list` | HHKB 以外、または存在しない hidraw を指定した | 変化なし | `--list` の `FLASH TARGET` を指定する |
| `the HFB file is required (except with --list)` | HFB ファイルを指定していない | 変化なし | HFB のパスを指定する |
| `F0 not supported (non-VIA QMK build, window unused); using E0` | default 版（VIA 非対応）の QMK から純正に戻すときの**正常な表示** | — | 対処不要（そのまま書き込みが続く） |
| `--list` に `WARNING: no FLASH TARGET` | USB の認識が一部失敗し、書き込み先のインターフェースが作られていない | 状態による | USB ケーブルを抜き差しして `--list` をやり直す |
| `transfer failed: unexpected E1/E2/E3 response: ...` | キーボードの応答が想定と違った（E3 なら転送終了時にファームが受理されなかった可能性） | 更新モードの可能性が高い | `--list` で状態を確認し、更新モードなら `--resume` で書き込み直す |

- 更新モードに入った後の書き込みは、`--resume` で最初（E1）から全量を送り直します。更新モードへの移行直後に止まったケースからの再開は実機で確認済みです。転送の途中で失敗した後の再開、E3 の応答が異常だった後の挙動は未確認です。
- `tools/qmk_to_hfb.py` の `unknown stock HFB (sha256 ...)` は、テンプレートが公式の `HHKB800_FW_A048.hfb` ではないことを示します。[setup.md](setup.md) の手順で取得し直してください。

## USB エラー（-71）

更新モードでキーボードが USB に再接続される瞬間に、カーネルログに次のようなエラーが出て、hidraw が作られないことがあります。

```
usb 3-1: can't set config #1, error -71
usbhid 3-1:1.2: can't add hid device: -71
usb 3-1: can't read configurations, error -71
```

- インターフェースが 1 つも作られない場合と、一部（書き込み先の `input2` など）だけ作られない場合があります。どちらも `flash.py` は書き込み先以外のインターフェースを使わず、抜き差しを待ちます。
- 断続的に発生し、根本原因は特定できていません（PFU ブートローダの USB 列挙とホストの組み合わせによるものと見ています）。USB ハブの有無、VIA のページの開閉との明確な関係も確認できていません。
- **キーボードは更新モードのままなので、ケーブルを抜き差しすれば回復します。** `flash.py` は抜き差しを案内して待ち、認識されたら自動で書き込みを続けます。
- 何度も起きる場合は、PC 本体の別のポートに直接つなぐ、ケーブルを替える、を試してください。
- 観察された事実の詳細は [reverse-engineering/implementation.md](reverse-engineering/implementation.md) にあります。

## 転送が同じ位置（`sent 1024/4960` の後）で毎回止まる

`sent 1024/4960` の後に `transfer failed: timed out waiting for HHKB response` で止まり、`--resume` でやり直しても同じ位置（トレースではパケット 1149 の次）で止まる場合です。

- この位置は、STM32 アプリの領域が終わり、HFB に含まれる Bluetooth（nRF）ファームの領域が始まる境目です。
- 実機で、**書き込み中のキーボードがこの PC とペアリング済みで、更新モードの USB 接続と同時に Bluetooth でもこの PC に接続した**ときに、この止まり方が 2 回続けて起きました。PC からそのキーボードのペアリングを削除し、USB を抜き差しして `--resume` したところ、最後まで書き込めました。原因は確定していませんが、Bluetooth 接続中はブートローダから nRF への受け渡しが止まると見ています。
- 確認方法：抜き差しした時刻に、カーネルログ（`journalctl -k`）へ `BLUETOOTH HID ... [HHKB-Hybrid...]`（`0005:04FE:0021`）が出ていないかを見る。
- 対処：
  1. PC の Bluetooth 設定から、そのキーボードのペアリングを削除する（または一時的にブロックする。例：`bluetoothctl block <アドレス>`）。
  2. USB ケーブルを抜き差しする（止まった後は、抜き差しするまで最初のコマンドにも応答しないことがあります）。
  3. `--list` で `FLASH TARGET` を確認して `--resume` で書き込む。
  4. 書き込み後、必要ならキーボードを Bluetooth でペアリングし直す。

## キー入力ができない / おかしい

| 症状 | 考えられる原因 | 対処 |
|---|---|---|
| `HHKB-Hybrid` と表示され、**青 LED が点灯したまま**キーが効かない（純正 FW に戻した直後など） | 純正 FW が Bluetooth の接続先へ再接続中で、入力が USB に出ていない | **Fn + Ctrl + 0** で USB に切り替える。それでも効かなければ次の行 |
| `HHKB-Hybrid` と表示され、キーがまったく効かない | 更新モード（bootmagic、書き込みの中断、QMK のフェイルセーフ） | `--resume` で HFB を書き込む（[device-state.md](device-state.md)） |
| 純正に戻したら、入力できないキーや刻印と違う文字が多発する | VIA 対応版の設定が純正のキーマップ領域に残ったまま戻した | [restore-stock.md](restore-stock.md) の「すでにキー入力が崩れてしまった場合」 |
| 電池駆動のときだけ、キーの取りこぼし・誤入力、Bluetooth の切断、反応しないなどが起きる（USB 接続では起きない） | 電池駆動時の省電力動作（`HHKB_LOW_POWER`、[usage.md](usage.md#電池駆動時の省電力動作)）が関係している可能性がある | USB に接続する。`HHKB Hybrid QMK` なら、`-e HHKB_LOW_POWER=no` でビルドしたファームを書き込んで同じ症状が出るか確かめる。`HHKB-Hybrid` でキーが効かなければ更新モードなので、同じファームを `--resume` で書き込む |
| JIS 配列の OS で記号がずれる / US 配列の OS で記号がずれる | JIS/US 補正の ON/OFF が合っていない | Ctrl+Alt+Shift+J で切り替える（[jis-us-toggle.md](jis-us-toggle.md)） |
| VIA で変えたキーが効かない | 別のレイヤーに割り当てた | VIA でレイヤー番号を確認する |

## VIA に接続できない

- udev ルールが無いと、「Authorize device」の一覧にキーボードが出ません（[setup.md](setup.md)）。
- VIA に接続できるのは VIA 対応版（`via` keymap）だけです。default 版や純正 FW には接続できません。
- 定義 JSON を読み込んでいない場合は、[via.md](via.md) の手順で読み込みます。

## DFU（分解）を検討する前に

次をすべて試しても、`lsusb -d 04fe:` に何も表示されない場合だけ、[recovery.md](recovery.md) の DFU を検討してください。DFU は基板の R6/R85 をショートするための分解が必要です。

1. ケーブルを替える、PC 本体の別のポートに直接つなぐ、USB ハブを外す
2. 電池を外し、USB ケーブルも抜いて 10 秒待ってから、USB だけで接続する
3. Esc を押しながら USB を接続する（bootmagic。QMK が起動できる状態なら更新モードに入る）
4. 別の PC で `lsusb` を確認する
