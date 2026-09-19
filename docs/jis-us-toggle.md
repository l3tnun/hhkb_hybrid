# JIS 環境で US 刻印どおりに入力する（JIS/US トグル）

OS 側の配列を **JIS（日本語）** に設定したまま、HHKB の **US 刻印どおり**に記号を入力できるようにする補正機能。

## 操作

- **Ctrl + Alt + Shift + J** で ON / OFF を切り替える（押すたびにトグル）。
- **`JIS_TOG` キー**でも切り替えられる。VIA 対応版（`via` keymap）では VIA の Custom タブから好きなキーに割り当てられる（[via.md](via.md)）。
- VIA 対応版では、VIA の HHKB メニューで次の設定ができる。
  - JIS/US 補正の ON/OFF。キー操作で切り替えた状態は、VIA のページを再読み込みすると表示に反映される。
  - Ctrl+Alt+Shift+J による切替の有効/無効（既定は有効）。無効にすると、Ctrl+Alt+Shift+J は通常のキー入力として送られ、`JIS_TOG` とメニューでのみ切り替わる。
- モード表示 LED は無し。入力結果で判断する：
  - 補正 OFF（US 通常）: `Shift + 2` → `"`
  - 補正 ON（JIS 環境で US 刻印）: `Shift + 2` → `@`
- 切替状態は保持される（下記「永続化」参照）。
- どの keymap でもキーボード側（`process_record_kb`）で変換するため、keymap を変えても機能する。
- `S(KC_2)`（VIA の `@` など）のような**修飾キー付きキーコード**も変換する。キーコードに含まれる Shift を押下中の Shift と同様に扱い、Ctrl / Alt / GUI は変換後のキーと一緒に送る。
- **VIA のマクロは変換しない**（マクロの文字列はキー処理を通らずに直接送出されるため）。JIS 環境で記号を含むマクロを使う場合は、JIS 配列で目的の文字になるキーを指定する。

## ビルドオプション（有効/無効）

この機能はビルド時に有効/無効を選べる（既定は有効）。`rules.mk` の `HHKB_JIS_US_TOGGLE`（既定 `yes`）で `-DHHKB_JIS_US_TOGGLE` を付与し、無効時は変換・トグル・永続化のコードごとコンパイルから外れる。

```sh
./tools/build.sh                                  # 既定: JIS/US トグル有効
./tools/build.sh default -e HHKB_JIS_US_TOGGLE=no  # 無効化（via keymap も同様: ./tools/build.sh via -e HHKB_JIS_US_TOGGLE=no）
```

無効ビルドでは `Ctrl+Alt+Shift+J` は通常の `J` として扱われ、記号変換も行われない。`JIS_TOG` キーは何もしないキーになり、VIA の HHKB メニューに JIS/US 補正の項目は表示されるが操作できない。

## 仕組み

US 配列の keymap を JIS ホストで使うと、一部の記号キーが別の文字になる。補正 ON のときは、対象キーを「JIS ホスト上で US 刻印の文字になるキー＋Shift 状態」に置き換えて送出する。

変換表（US 刻印 → 送出するキー / Shift）:

| US 刻印（押下） | 送出 | Shift |
|---|---|---|
| `Shift+2` → `@` | `[` | なし |
| `Shift+6` → `^` | `=` | なし |
| `Shift+7` → `&` | `6` | あり |
| `Shift+8` → `*` | `'` | あり |
| `Shift+9` → `(` | `8` | あり |
| `Shift+0` → `)` | `9` | あり |
| `Shift+-` → `_` | `INT1`(ろ) | あり |
| `Shift+=` → `+` | `;` | あり |
| `Shift+;` → `:` | `'` | なし |
| `Shift+'` → `"` | `2` | あり |
| `Shift+[` → `{` | `]` | あり |
| `Shift+]` → `}` | `\` | あり |
| `Shift+\` → `\|` | `INT3`(¥) | あり |
| `` Shift+` `` → `~` | `=` | あり |
| `=` | `-` | あり |
| `[` | `]` | なし |
| `]` | `\` | なし |
| `\` | `INT3`(¥) | なし |
| `'` | `7` | あり |
| `` ` `` | `[` | あり |

（この対応は、動作確認済みの純正 JIS/US トグルパッチの変換表と同一。）

対象外：複数の変換対象記号キーを同時に押しっぱなしにするロールオーバー（単キー入力・Shift 併用・Shift を押したまま別キーへ移る一般的な入力は対象）。

## 永続化

切替フラグは **STM32L0 の内蔵データ EEPROM** に保存する（純正 JIS/US トグルと同方式）。

- 保存先は純正の **sleep タイムアウト設定バイト `0x08080520`（1〜60 分）の最下位ビット**。
  - 補正 ON = 奇数（例 31）、OFF = 偶数（例 30）。
  - 純正の起動時チェックは 1〜60 を有効値として扱うため、奇数値もリセットされず保持される。
- **完全な非揮発**：電池を抜いても、純正 FW に戻しても保持される。
- ブートローダの起動判定フラグ（`0x08080360` / CRC `0x08080370`）には一切触れない。バイト単位書き込みのため隣接バイトも変更しない。

補足:

- sleep バイトが奇数になるが、**QMK はこのバイトを自動スリープ時間に使わない**（default 版はコンパイル時定数 `HHKB_AUTOSLEEP_TIMEOUT_MS`、VIA 対応版は VIA の HHKB メニューの設定）ため、この値は QMK の挙動に影響しない。
- QMK 稼働中は**このバイトを書き換えるのは本機能のトグルだけ**（公式 Keymap Tool は QMK 上では動作せず対象外）。純正 FW に戻して公式ツールで sleep を再設定すると最下位ビットが変わりうるが、それは純正動作中の話で、その後 QMK を書き込めば本機能のトグルで再設定できる。
- VIA 対応版の「設定の初期化」や bootmagic による設定の初期化では、このバイトは変わらない（補正の ON/OFF は保持される）。

実装：`power_save_jis_mode` / `power_load_jis_mode`（`power.c`）。EEPROM 書き込みは `FLASH->PEKEYR` でロック解除してバイト書き込み（`board.c` の更新モード処理と同じ手順）。

## 実装位置

- 変換とトグル：`keyboards/hhkb_hybrid/hhkb_hybrid.c`（`jis_translate` / `jis_send` / `jis_toggle` / `process_record_kb`）
- `JIS_TOG` キーコード：`keyboards/hhkb_hybrid/hhkb_hybrid.h`（`enum hhkb_keycodes`）
- VIA の HHKB メニュー（補正の ON/OFF、Ctrl+Alt+Shift+J の有効/無効）：`hhkb_hybrid.c`（`via_custom_value_command_kb`）、定義 JSON は `tools/gen_via_json.py`
- 永続化：`keyboards/hhkb_hybrid/power.c`（`SLEEP_EE_ADDR`, EEPROM 書き込み）。Ctrl+Alt+Shift+J の有効/無効は VIA 対応版の EEPROM 領域（[via.md](via.md)）
