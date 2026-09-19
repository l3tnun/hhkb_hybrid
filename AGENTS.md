# AI エージェント向けの運用ルール

このリポジトリで作業する AI エージェント（Claude Code など）が守るルールです。人間向けの手順は `README.md` と `docs/` にあります。**作業の前に `docs/quickstart.md` と、作業内容に応じた docs を読んでください。**

## 実機・ハードウェアに関わる操作

### ユーザーの明示的な承認が必要な操作

次の操作は、**実行の直前に対象と内容をユーザーに示し、明示的な承認を得てから**行います。以前の承認は流用しません。

- `tools/flash.py` による書き込み（`--list` と `--dry-run` 以外のすべて）
- キーボードの hidraw への書き込み全般（`AA AA E0` / `AA AA F0` などの更新コマンド、VIA の設定を変えるコマンド）
- DFU 関連の操作（`dfu-util` など）

### 承認なしで行ってよい読み取り専用の操作

- `python3 tools/flash.py --list`、`python3 tools/flash.py --dry-run <HFB>`
- `lsusb`、sysfs（`/sys/class/hidraw/*/device/uevent`、`report_descriptor` など）の閲覧
- カーネルログの閲覧（`journalctl -k`）
- `docs/via.md` の「動作中のファームを確認する（書き込みなし）」にある VIA の読み出しコマンド（`0x01` / `0x11` / `0x04` / `0x08` だけ）

### 書き込み前の必須チェック

1. 接続されている HHKB が **1 台だけ**であること（`--list`）
2. `docs/device-state.md` に従い、状態（純正 FW / 更新モード / QMK）と `FLASH TARGET` を特定し、ユーザーに提示すること。純正 FW と更新モードは USB 上で区別できないので、キー入力が効くかをユーザーに確認すること
3. HFB が意図したものであること。どのファイルを書き込むかをユーザーに確認し、`--dry-run` で次を確かめる
   - `match=True`
   - `hfb_kind=QMK`（QMK の HFB）または `hfb_kind=stock (no QMK)`（純正 HFB）が意図どおり
   - 純正 HFB なら `sha256sum` が `635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43`（`header_word=0x11a3`）
   - QMK の HFB は、出どころが不明（版の分からない既存ファイルなど）なら `qmk_to_hfb.py` で変換し直す（`template ... (sha256 ok)` を確認）
4. ユーザーに VIA のページを閉じてもらったこと。書き込むキーボードが、この PC と Bluetooth でペアリングされていないことをユーザーに確認すること（転送が止まる原因になる。`docs/troubleshooting.md`）
5. `--resume` は更新モードのときだけ使うこと

### 禁止事項

- `--allow-crc-mismatch` や `--allow-unknown-template` を使うこと
- `FLASH TARGET` 以外の hidraw に書き込むこと、候補の hidraw を総当たりで試すこと
- 書き込みを途中で中断すること
- 複数の HHKB が接続された状態で書き込むこと
- USB に `04fe:0021` が見えている状態で DFU（分解）を提案すること。更新モードで認識されている限り、`--resume` で復旧できます
- USB エラー（`-71`）を理由に DFU を提案すること。抜き差しで回復する既知の現象です（`docs/troubleshooting.md`）

### 書き込みの実行方法

- 書き込みは約 3 分かかる。**コマンドのタイムアウトを 600 秒以上にするか、バックグラウンドで実行して出力を監視する。** 既定のタイムアウト（例：2 分）で実行すると転送の途中で強制終了され、「途中で中断」と同じことになる。
- `--device` は省略できるが、常に `--list` で確認した `FLASH TARGET` を明示する。
- 実行中は出力を見続ける。`unplug and replug the USB cable` と表示されたら、**すぐに**ユーザーへ USB ケーブルの抜き差しを依頼する（既定で 180 秒待つ）。
- 実行中に別の `flash.py` を起動しない。
- `done` の後、再起動を待ってから `--list` で結果を確認し、ユーザーにキー入力の確認を依頼する。

### システム設定の変更

udev ルールの作成（`/etc/udev/rules.d`、`sudo udevadm`）、docker グループへの追加など、`sudo` が必要な操作やシステム設定の変更は、内容をユーザーに示して依頼する（エージェントが勝手に実行しない）。

### 人間にしかできない操作

USB ケーブルの抜き差し、bootmagic（Esc を押しながら接続）、電源ボタン、電池の着脱、キー入力の確認は、ユーザーに具体的に依頼して結果を待ちます。実行したと推測して先に進まないでください。

### 失敗したとき

`docs/troubleshooting.md` の表に従います。キーボードが更新モードに残っていれば、ユーザーの承認を得て `--resume` で書き込み直します。

### DFU

DFU モード（基板の R6/R85 ショート）は物理的な分解が必要でリスクを伴います。入る回数を最小限にし、HID 経由（E0/E1/E2/E3）で完結させてください。DFU は切り分けや最終手段に限ります。

## リポジトリの変更

- `.hfb`、`.bin`、`local/` の中身をコミットしない、配布しない（PFU 製バイナリを含むため）。
- `qmk_firmware` submodule のコミットや、`tools/build.sh` のビルドイメージのダイジェストを、ユーザーの指示なしに変更しない。
- ビルドはリポジトリのルートで `./tools/build.sh [keymap] [オプション]` を使う。`qmk compile` を直接使う場合は、先に `build.sh` で定義をコピーする。
- EEPROM に関わる変更をしたら `python3 tools/check_eeprom_refs.py <bin>` で `result: OK` を確認する。
- VIA 対応版で EEPROM の配置を変えたら（レイヤー数、`VIA_EEPROM_CUSTOM_CONFIG_SIZE` など）、`hhkb_hybrid.c` の `HHKB_VIA_LAYOUT_VERSION` を上げる。
- VIA の独自キーコードは末尾に追加するだけにする（並べ替え・削除をしない）。VIA の定義を変えたら `python3 tools/gen_via_json.py` で再生成してコミットする。ドロップダウンの選択肢に値 0 を使わない。
- 詳細は `docs/via.md` の「保守・開発者向けメモ」。

## ドキュメント

- `README.md` と `docs/` は、それだけで初見の人が環境構築・ビルド・状態確認・書き込み・復旧をできるように書く。
- 作業計画や作業ログ（`PLAN.md` など）をリポジトリに残さない。残すべき情報は `README.md` や `docs/` に書く。
- ユーザー名、ホームディレクトリのパス、デバイスのシリアル番号など、個人や特定の環境に固有の情報を書かない。
