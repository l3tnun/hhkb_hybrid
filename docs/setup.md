# 環境構築

ビルドと書き込みの前に一度だけ行う準備です。終わったら [quickstart.md](quickstart.md) に進んでください。

## 必要なもの

| 項目 | 内容 |
|---|---|
| OS | **書き込みは Linux 限定**（`/dev/hidraw*` を使います）。ビルドは Docker が動く環境なら可能と考えられますが、確認しているのは Linux だけです |
| git | submodule（QMK 本体）の取得に使います |
| Docker | ビルドは QMK 公式イメージの中で行います。**`sudo` なしで `docker run` できること**（`docker` グループに入るか、rootless Docker） |
| Python | 3.9 以上。`tools/` のスクリプトは標準ライブラリだけで動きます（追加パッケージ不要） |
| coreutils | `sha256sum` で純正 HFB やビルド結果を照合します |
| ディスク | `qmk_firmware/`（QMK 本体と必要な submodule）約 710 MB、QMK ビルドイメージ約 3.7 GB |
| ネットワーク | 初回のみ：GitHub（submodule）、ghcr.io（ビルドイメージ）、PFU の配布サーバ（純正 HFB） |
| USB ケーブル | キーボードと PC を直接つなげるもの。USB ハブ経由でも書き込めた実績はありますが、問題の切り分けには直結がおすすめです |

動作確認した環境の例：Ubuntu 26.04 LTS、Docker 29.7、git 2.53、Python 3.14。

## 1. リポジトリの取得

```sh
git clone <このリポジトリの URL>
cd <clone したディレクトリ>
git submodule update --init qmk_firmware
```

- `qmk_firmware`（QMK 本体）はコミット単位で固定された submodule です。本体だけ取得すれば、ビルドに必要な下位の submodule（`lib/chibios` など）は `tools/build.sh` が自動で取得します。
- `git clone --recursive` でも取得できますが、QMK の全 submodule を取るため時間と容量が増えます。
- `qmk_firmware` 本体が無い状態で `tools/build.sh` を実行すると、上のコマンドを案内して終了します。

## 2. Docker の確認

```sh
docker run --rm hello-world
```

`permission denied` になる場合は、Docker の公式手順に従って自分のユーザーを `docker` グループに追加し、ログインし直してください。ビルドイメージ（`ghcr.io/qmk/qmk_cli`、ダイジェスト固定）は、初回の `tools/build.sh` 実行時に自動で取得されます。

## 3. キーボードへのアクセス権（udev ルール）

`tools/flash.py` と VIA（ブラウザの WebHID）は、キーボードの `/dev/hidraw*` を読み書きします。Linux の既定では root しかアクセスできないので、udev ルールで一般ユーザーに許可します。

### ルールを作る

`/etc/udev/rules.d/70-hhkb-hybrid.rules` を作成します（管理者権限が必要です）。

```
# HHKB Professional HYBRID: 純正 FW / PFU 更新モード / QMK はすべて 04fe:0021
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04fe", ATTRS{idProduct}=="0021", TAG+="uaccess"
```

- `TAG+="uaccess"` は、ログイン中のデスクトップユーザーにアクセス権を与えます（systemd-logind を使う一般的なデスクトップ環境）。ファイル名の番号は `73` より小さくしてください（`73-seat-late.rules` より前に読まれる必要があるため）。
- SSH 経由などログインセッションが無い環境では、代わりにグループで許可します。

  ```
  SUBSYSTEM=="hidraw", ATTRS{idVendor}=="04fe", ATTRS{idProduct}=="0021", GROUP="plugdev", MODE="0660"
  ```

  この場合は自分のユーザーを `plugdev` グループに入れ、ログインし直してください（グループ名はディストリビューションによって異なります）。

### 反映と確認

```sh
sudo udevadm control --reload-rules
```

反映後、**キーボードの USB ケーブルを抜き差し**します。そのうえで確認します。

```sh
python3 tools/flash.py --list
```

一覧の各行に `(no read/write permission: see docs/setup.md)` が出なければ設定できています。

### 注意

- **`sudo chmod` による一時的な許可は使えません。** 書き込み中にキーボードが更新モードへ切り替わると USB が再認識され、hidraw の番号が変わるため、新しいノードには権限が付きません。
- `sudo python3 tools/flash.py ...` でも書き込み自体はできますが、トレースログなどが root 所有になります。udev ルールの利用をおすすめします。
- Chrome / Edge の VIA も同じルールで接続できるようになります。Flatpak や Snap で入れたブラウザは、サンドボックスの制限で hidraw にアクセスできない場合があります（未確認）。

## 4. 純正 HFB の用意

変換のテンプレートと、純正へ戻すときに使います。PFU の公式配布ファイルです。**このリポジトリでは配布しません**（PFU 製バイナリを含み、再配布できないため）。

```sh
mkdir -p local
curl -fL -o local/HHKB800_FW_A048.hfb https://origin.pfultd.com/downloads/hhkb/HHKB800_FW_A048.hfb
sha256sum local/HHKB800_FW_A048.hfb
python3 tools/hfb_crc.py local/HHKB800_FW_A048.hfb
```

期待値：

```
635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43  local/HHKB800_FW_A048.hfb
```

```
stored=0x3b01
computed=0x3b01
match=True
type=AHUX01
version_bytes=0a000408
```

- サイズは 282,672 バイトです。
- sha256 が違うファイルは使わないでください。`tools/qmk_to_hfb.py` も、既知の A0.48 以外のテンプレートでは変換を中止します。

## `local/` フォルダ

リポジトリ直下の `local/` は `.gitignore` の対象です（ディレクトリ自体は `.gitkeep` で保持され、clone 直後から使えます）。次のような、コミットしてはいけないファイルを置く場所として使います。

| ファイル | 例 |
|---|---|
| 純正 HFB | `local/HHKB800_FW_A048.hfb` |
| 変換した HFB | `local/hhkb_hybrid_default.hfb`、`local/hhkb_hybrid_via.hfb` |
| 書き込みのトレース | `local/flash_<日付>.jsonl` |

HFB には PFU 製の Bluetooth（nRF）ファームが含まれるため、**変換した HFB も配布できません**。
