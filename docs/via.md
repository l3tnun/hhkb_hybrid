# VIA 対応（キーマップを GUI で編集する）

`via` keymap でビルドした QMK を書き込むと、[VIA](https://usevia.app)（Web アプリ）からキーマップ・マクロ・キーボード固有の設定をビルドなしで変更できます。変更はキーボード内蔵の EEPROM に保存され、抜き差し・電源 OFF/ON・スリープ復帰・QMK の再書き込みの後も残ります。

`default` keymap（VIA 非対応）の挙動は変わりません。VIA を使う場合だけ `via` keymap を選んでください。

## 必要なもの

- Chrome または Edge（VIA は WebHID を使うため、Firefox / Safari は不可）
- USB 接続（VIA での設定は USB 経由のみ。Bluetooth 接続中は設定できません）
- Linux では、キーボードの `hidraw` を一般ユーザーで読み書きできること（udev ルール。[setup.md](setup.md)）

## 1. ビルドと書き込み

```sh
./tools/build.sh via
python3 tools/qmk_to_hfb.py qmk_firmware/.build/hhkb_hybrid_via.bin local/HHKB800_FW_A048.hfb local/hhkb_hybrid_via.hfb
python3 tools/flash.py --list
python3 tools/flash.py --device /dev/hidrawN local/hhkb_hybrid_via.hfb
```

全体の流れと、開始状態ごとのコマンドは [quickstart.md](quickstart.md) を参照してください。

- 変換時に `stored_crc=0x.... match=True` が出ることを確認してください。
- **書き込み中は VIA のページ（ブラウザのタブ）を閉じてください。** VIA がキーボードと通信していると、書き込みの開始処理と干渉するおそれがあります。
- 書き込み先の選び方、書き込みが途中で止まったときの対処は [flashing.md](flashing.md) を参照してください。

初めて `via` を書き込んだとき（または EEPROM の配置が変わる版を書き込んだとき）は、キーマップと設定が初期状態から始まります。以後、同じ配置の版を書き込み直しても設定は残ります。

## 2. VIA に定義を読み込ませる

このキーボードは VIA の公式リストに載っていないため、定義 JSON を自分で読み込ませます。定義はリポジトリの `via/hhkb_hybrid.json` です。

1. Chrome / Edge で https://usevia.app を開く
2. 右上の歯車（Settings）で **Show Design tab** を ON にする
3. **Design** タブで **Load Draft Definition** を押し、`via/hhkb_hybrid.json` を選ぶ
4. **Configure** タブで **Authorize device** を押し、`HHKB Hybrid QMK` を選んで接続する

読み込んだ定義はそのブラウザに保存されるため、次回からは 4 だけで接続できます。別のブラウザや PC を使うとき、サイトデータを消したとき、定義 JSON が更新されたとき（独自キーコードやメニューの追加など）は読み込み直してください。

定義 JSON は `tools/gen_via_json.py` が `keyboards/hhkb_hybrid/keyboard.json` から生成します（日本語ラベルは VIA が UTF-8 を解釈しないため `\uXXXX` で出力しています）。

## 3. キーマップ

- レイヤーは 8 枚です（0：Base、1：Fn、2〜7：空き）。初期状態のレイヤー 0 / 1 は `default` keymap と同じ配置です。
- **Fn はレイヤー 1 に置いてください。** 純正互換の Bluetooth キー操作（Fn+Ctrl+1〜4 など）は「レイヤー 1 が有効か」で判定しています（下の「Bluetooth キー操作」を OFF にした場合は無関係です）。
- VIA のマクロも使えます。ただしマクロの文字列は JIS/US 補正を通りません（[jis-us-toggle.md](jis-us-toggle.md)）。

### 独自キー（Custom タブ）

VIA の **Custom** タブに、このキーボード専用のキーが表示されます。好きな位置・レイヤーに割り当てられます。

| 表示 | 名前 | 動作 |
|---|---|---|
| JIS | `JIS_TOG` | JIS/US 補正の ON/OFF |
| BT1〜BT4 | `BT_SLOT1`〜`BT_SLOT4` | Bluetooth スロット 1〜4 に切り替える。ペアリングモード中はそのスロットでペアリングする |
| Pair | `BT_PAIR` | ペアリングモードに入る（Fn+Q 相当） |
| PCan | `BT_CANCEL` | ペアリングモードを取り消す（Fn+X 相当） |
| USB | `OUT_AUTO` | 出力を自動（USB 接続中は USB）に戻す（Fn+Ctrl+0 相当） |
| BDel1〜BDel4 | `BT_DEL1`〜`BT_DEL4` | ペアリングモード中に押すと、そのスロットの登録を削除する（Fn+Q の後に Fn+Ctrl+Del+数字 相当） |

独自キーの値（`QK_KB_0` から順）はキーマップと一緒に EEPROM に保存されるため、ファーム側では並べ替えずに末尾へ追加するだけにしています。

`HHKB_JIS_US_TOGGLE=no` でビルドした場合、`JIS_TOG` は何もしないキーになります。

## 4. HHKB メニュー（キーボード固有の設定）

VIA の設定画面に **HHKB** メニューが追加され、左側の一覧に次の項目が並びます。

| 項目 | 設定 | 既定値 |
|---|---|---|
| JIS/US 補正 | JIS/US 補正の ON/OFF | 現在の状態 |
| JIS/US 補正 | Ctrl+Alt+Shift+J で補正を切り替えるか | 有効 |
| 自動スリープ | 無操作で自動スリープするまでの時間（1 / 5 / 10 / 30 / 60 分）。電池駆動時のみ。DIP SW6 が ON なら常に無効 | 30 分 |
| Bluetooth キー操作 | 純正互換のキー操作（Fn+Ctrl+1〜4 / Fn+Ctrl+0 / Fn+Q / Fn+X / Fn+Ctrl+Del+数字）を使うか。OFF にすると、Custom タブの BT キーだけで操作します | 有効 |
| 設定の初期化 | ボタン。下の「設定を初期状態に戻す」を参照 | — |

- 設定は変更した時点で保存されます。
- **VIA はメニューの値を接続時に読み込むだけです。** キー操作（`JIS_TOG` や Ctrl+Alt+Shift+J）で JIS/US 補正を切り替えた場合、VIA の表示は**ページを再読み込みすると**反映されます。

## 5. 設定を初期状態に戻す

次のどちらかで、キーマップ・マクロ・HHKB メニューの設定が初期状態に戻ります。JIS/US 補正の ON/OFF は保持されます。

- **HHKB メニューの「設定の初期化」ボタン**：押すとキーボードが再起動します。ページを再読み込みして接続し直してください。
- **Esc を押しながら USB を接続（bootmagic）**：設定を初期化したうえで、書き込み用の更新モードに入ります（キー入力は効かなくなるので、続けてファームを書き込みます）。

VIA 標準の EEPROM リセットは開発版の Debug 画面にしかなく、しかもキーボード固有の設定を戻さないため、このボタンを用意しています。

## 6. 純正 FW に戻すとき

VIA の設定は、純正 FW がキーマップ（表 B）に使う EEPROM 領域 `0x08080700–0x08080CFF` に保存しています。この領域に VIA のデータが残ったまま純正に戻すと、純正 FW がそれをキーマップとして読み込み、**入力できないキーや刻印と違う文字になるキーが多発します**。

そのため、純正に戻すときは次のどちらかを使ってください。どちらも領域を純正の状態（ゼロ）に戻してから更新モードに入ります。

- **`tools/flash.py` で純正 HFB を書き込む（推奨）**：純正の HFB だと自動で判定し、動作中の QMK にこの領域を消すコマンドを送ってから書き込みます。
- **Esc を押しながら USB を接続（bootmagic）** してから純正 HFB を書き込む。

**QMK の `QK_BOOT` キー（Reset / Boot）で更新モードに入って純正に戻すのは避けてください。** QMK の更新にも使う経路なので、この領域を消しません。

手順の詳細と、すでにキー入力が崩れてしまった場合の直し方は [restore-stock.md](restore-stock.md) を参照してください。

## 制約

- 設定は USB 接続時のみ。Bluetooth 出力でも、変更したキーマップはそのまま使えます。
- Bluetooth ではマウスキー・NKRO・システム制御（電源/スリープ）は送れません。メディアキーは純正 BLE ファームが対応するものだけです。VIA でこれらのキーを割り当てても、Bluetooth では効きません（このビルドはマウスキー自体が無効です）。
- VIA のマクロは JIS/US 補正の対象外です。
- レイヤー数など EEPROM の配置を変えた版を書き込むと、最初の起動で設定が一度だけ初期状態に戻ります。

## EEPROM の使い方（参考）

| アドレス | 用途 |
|---|---|
| `0x08080700–0x08080CFF`（1,536 B） | VIA の保存領域（QMK の eeconfig、VIA 設定、8 レイヤー分のキーマップ、マクロ、HHKB メニューの設定 2 B）。純正 FW のキーマップ表 B と同じ領域 |
| `0x08080300` | 純正キーマップの有効マーク。VIA 版は領域を初期化するときに無効化します |
| `0x08080520` | JIS/US 補正の ON/OFF（純正のスリープ時間設定バイトの最下位ビット、default 版と共通） |

QMK 標準の STM32L0 用 EEPROM ドライバは先頭 `0x08080000` から使うため、ブートローダの起動判定（`0x08080360` / `0x08080370`）を壊してしまいます。そのため `via` keymap は、上の領域だけを読み書きする専用ドライバ（`keyboards/hhkb_hybrid/eeprom_hhkb.c`）を使います。ビルドしたファームがこれ以外の EEPROM アドレスを参照していないかは、次のコマンドで確認できます。

```sh
python3 tools/check_eeprom_refs.py qmk_firmware/.build/hhkb_hybrid_via.bin
```

純正 FW とブートローダが EEPROM のどこを書き込むかの解析は [reverse-engineering/stm32-analysis.md](reverse-engineering/stm32-analysis.md) にあります。

## 動作中のファームを確認する（書き込みなし）

キーボードに書き込まずに、VIA 対応版が動いているか・設定がどうなっているかを Raw HID で読み出せます。対象は QMK の Raw HID インターフェース（`input1`、レポートディスクリプタが `06 60 ff` で始まる hidraw。`python3 tools/flash.py --list` で `FLASH TARGET` と表示されるもの）です。

```sh
python3 - /dev/hidrawN <<'EOF'
import os, select, sys
fd = os.open(sys.argv[1], os.O_RDWR)
def cmd(p):
    os.write(fd, b"\x00" + bytes(p).ljust(32, b"\x00"))
    r, _, _ = select.select([fd], [], [], 1.0)
    return os.read(fd, 64)[:32] if r else None
rx = cmd([0x01])                                   # VIA プロトコル版数
if not rx or rx[0] != 0x01:
    sys.exit("VIA に応答しない（VIA 非対応の QMK、または QMK ではない）")
print("protocol 0x%04x" % ((rx[1] << 8) | rx[2]))    # VIA 対応版は 0x000d
print("layers", cmd([0x11])[1])                     # 8
rx = cmd([0x04, 0, 1, 0])                           # レイヤー 0 / row 1 / col 0（Esc）のキーコード
print("L0 Esc = 0x%04x" % ((rx[4] << 8) | rx[5]))
# HHKB メニューの値（下の「HHKB メニューの値 ID」）
print("menu", [cmd([0x08, 0x00, i])[3] for i in (1, 2, 3, 4)])
os.close(fd)
EOF
```

- 読み出しだけのコマンド（`0x01` / `0x11` / `0x04` / `0x08`）なので EEPROM は変わりません。
- マトリクス位置（row, col）は `keyboards/hhkb_hybrid/keyboard.json` の `matrix` を参照してください。

## 保守・開発者向けメモ

### 設計上の判断

- **VIA を採用し、Vial は使わない**：Vial 用の QMK（vial-qmk）は QMK 本家より古い時点を基にしており、このリポジトリで固定している QMK 本体と差し替えると再現ビルドの前提が崩れます。また 2026 年 9 月時点の QMK 公式ビルドイメージ（Python 3.14）では、vial-qmk のビルドツールがそのままでは動きませんでした。タップダンスなどの高度な機能はコードで実装できるため、GUI での配置変更は VIA で足りると判断しています。
- **JIS/US 補正はレイヤーではなくファームで変換する**：VIA のレイヤーだけで JIS 環境向けの記号を作ろうとすると、物理 Shift をレイヤー切替キーにする必要があり、Shift+クリックや Shift 単独押しを使う IME 操作などが壊れます。ファームで変換すれば Shift は本物の修飾キーのまま使えます。
- **純正の Keymap Tool には対応しない**：Keymap Tool は QMK 上では動作しません。純正に戻して Keymap Tool でキーマップを書き込むと表 B（VIA の保存領域）も書き換わるため、その後 VIA 対応版を書き込むと設定は初期状態から始まります。

### EEPROM 領域の並びと容量

VIA の保存領域（1,536 B）は、先頭から次の順に使われます。

| 内容 | サイズ |
|---|---|
| QMK の eeconfig | 37 B |
| VIA のマジック / レイアウトオプション | 3 B / 1 B |
| キーボード設定（HHKB メニュー） | 2 B |
| キーマップ（120 B × 8 レイヤー） | 960 B |
| マクロ（残り） | 533 B |

- 1 レイヤーは 4 行 × 15 列 × 2 B = 120 B。マクロは QMK の静的チェックで最低 100 B 必要なため、この領域でのレイヤー数の上限は 11 です。
- レイヤー数は `keymaps/via/config.h` の `DYNAMIC_KEYMAP_LAYER_COUNT`、領域サイズは同ファイルの `EEPROM_SIZE`（`eeprom_hhkb.c` が上限 1,536 B を静的に検査）です。

### キーボード設定 2 B と配置の版番号

`VIA_EEPROM_CUSTOM_CONFIG_SIZE 2` の 2 バイトを `hhkb_hybrid.c` で次のように使います。**既定値はすべて 0** なので、消去直後の EEPROM がそのまま既定値として読めます。

| 位置 | 内容 |
|---|---|
| バイト 0 の上位 4 ビット | EEPROM 配置の版番号 `HHKB_VIA_LAYOUT_VERSION`（1 以上。現在は 2） |
| バイト 0 の bit0 | Ctrl+Alt+Shift+J による JIS/US 補正の切替を**無効**にする |
| バイト 0 の bit1 | 純正互換の Bluetooth キー操作を**無効**にする |
| バイト 1 | 自動スリープのコード（0 = 30 分、1 = 1 分、2 = 5 分、3 = 10 分、4 = 60 分。範囲外は 30 分） |

- VIA のマジックは QMK のビルド日時から作られますが、`tools/build.sh` は `SKIP_VERSION=1` で日時を固定しています。そのため、同じ配置の版を書き込み直しても VIA の設定は残ります。
- その代わり、**EEPROM の配置が変わる変更**（レイヤー数、`VIA_EEPROM_CUSTOM_CONFIG_SIZE`、QMK 本体の更新による eeconfig サイズの変化など）をしたときは、`hhkb_hybrid.c` の `HHKB_VIA_LAYOUT_VERSION` を 1 つ上げてください。起動時（`via_init_kb()`）に版番号が一致しないと、キーマップ・マクロ・キーボード設定を初期化します。上げ忘れると、古いデータがずれた位置のまま読まれます。

### HHKB メニューの値 ID（VIA のカスタム値、チャンネル 0）

| ID | 項目 | VIA との値 |
|---|---|---|
| 1 | JIS/US 補正の ON/OFF | 0 / 1 |
| 2 | Ctrl+Alt+Shift+J で切り替えるか | 0 / 1 |
| 3 | 自動スリープ | 分数（1 / 5 / 10 / 30 / 60）。ファームが保存用のコードと相互変換 |
| 4 | 純正互換の Bluetooth キー操作 | 0 / 1 |
| 5 | 設定の初期化ボタン | 押すと 1 |

- 処理は `hhkb_hybrid.c` の `via_custom_value_command_kb()`、画面定義は `tools/gen_via_json.py` の `MENUS` です。ID と並びは両方を揃えてください。
- **ドロップダウンの選択肢に値 0 を使わないでください。** usevia.app は選択肢の値を `value || idx` で決めるため、値 0 の選択肢は並び順の番号に置き換わって送られます（自動スリープで「30 分」を選ぶと 10 分になる不具合の原因でした）。
- usevia.app は読み込んだ定義を UTF-8 として解釈しないため、`gen_via_json.py` は日本語を `\uXXXX` で出力しています。
- usevia.app はメニューの値を接続時に読むだけです。キーボード側で値が変わっても、ページを再読み込みするまで表示は更新されません。
- usevia.app 標準の EEPROM リセットは開発版の Debug 画面にしかなく、しかもキーマップとマクロだけを初期化します。そのため「設定の初期化」ボタン（ID 5）を用意しています。

### ファーム内部の注意

- **EEPROM ドライバの format は消去しない**：QMK は起動時に `via_init()` を eeconfig の初期化より**先に**呼びます。`eeprom_driver_format(false)`（初回起動・bootmagic・設定の初期化で呼ばれる）で消去すると、直前に VIA が書いたキーマップと版番号が消えてしまうため、`eeprom_hhkb.c` はここではマジック無効化だけを行います。残っていたバイトは、eeconfig の再初期化・VIA のマジック不一致・版番号の照合で既定値に戻ります。
- **領域の全消去（ゼロ化）は純正へ戻す経路だけ**：Raw HID の `AA AA F0 00 00` と bootmagic が `eeprom_driver_erase()` を呼びます。仕様は [reverse-engineering/implementation.md](reverse-engineering/implementation.md)。
- **Raw HID の受信**：VIA 対応版では VIA が `raw_hid_receive()` を持つため、純正の更新コマンド（`AA AA …`）は `via_command_kb()` で VIA より先に処理しています。
- **更新モードへ移る前に USB を切断する**：E0 / F0 / QK_BOOT / bootmagic は、USB を切断して 500 ms 待ってからリセットします（ホストに切断を確実に認識させるため）。
- **VIA のマクロは JIS/US 補正を通らない**：QMK のマクロ送出（`send_char` 系）はキー処理（`process_record_kb`）を経由しないためです。
