# HHKB Hybrid 内蔵 nRF ファームウェアイメージ解析

この文書は、HHKB Professional HYBRID 英語配列の純正FW `HHKB800_FW_A048.hfb` の後半部分に含まれる Bluetooth (nRF) 用ファームウェアイメージを解析した記録である。

## 結論

HHKB Hybrid の Bluetooth は **Taiyo Yuden EYSHCNZWZ モジュール (Nordic nRF52832, 512KB Flash / 64KB RAM 内蔵)** で確定した。

- メイン (USB/マトリクス): STM32L072RBT6 (Cortex-M0+、HFB A048 の 0x08010000 起点 64KiB アプリ。チップシルクで確定。旧記載の「Cortex-M3 / F1 系」は誤り)
- Bluetooth: **nRF52832 (EYSHCNZWZ)**、SoftDevice ベース、アプリベース `0x00026000`
- モジュール認証: FCC ID `RYYEYSHCN`、工事設計認証 `001-A10745`、IC `4389B-EYSHCN` (Taiyo Yuden データシートより)

### 型番の確定根拠

| 出典 | 内容 |
|---|---|
| 基板写真 (ユーザー提供) | 銀色シールドモジュール。シルクに `EYSHCN` 系表記 (視覚読みでは `MDBT42Q`/`RAYTAC` にも見えたが、データシートの表示規定「シールドケース上に品名、ロット番号、電波法ID、会社名を印字」と整合) |
| FCC ID `RYYEYSHCN` | Grantee: TAIYO YUDEN、Bluetooth Smart / ANT Module、モデル EYSHCN、2402-2480MHz |
| Taiyo Yuden データシート `EYSHCNZWZ` (V1.4, 2019-06-06) | チップ: **Nordic nRF52832 (512kB Flash, 64kB RAM)**、Bluetooth 5.0 LE、49-pin LGA、32MHz + 32.768kHz 内蔵 |
| 工事設計認証 `001-A10745` | EYSHCN: 001-A10745 (データシート記載) |
| HFB バイナリ解析 | nRF52832 (RAM 64KiB 端 = 0x20010000 の SP) + S132 系 SoftDevice と整合 |

### SoftDevice の版数について

Taiyo Yuden データシートの一般事項書 g には「本製品には固定の SoftDevice (s132_nrf52_5.0.0_softdevice.hex) ファームウェアを書き込んでいます」とある。一方、HFB のバイナリ解析ではアプリベース `0x00026000` (= S132 v6.x 相当) を観測している。これは **PFU が出荷後の FW 更新 (A0.48 時点) で SoftDevice を v5.0.0 から v6 系へ更新している** ことを示す (HFB の後半セクションに SoftDevice 本体も含まれるため、nRF 側も STM32 経由で更新される)。

HFB は 2 チップ分のイメージを 1 ファイルに同梱しており、PFU の HID アップデート (`E0/E1/E2/E3`) はどちらも書き込める構造になっている（nRF 側は STM32 経由での書き込みと推定）。

## HFB 全体のセクション構成

| HFB offset | サイズ | 内容 | ロード先 |
|---|---|---|---|
| `0x0000..0x0003` | 4 | ファイル管理情報 (CRC 等) | - |
| `0x0004..0x10003` | 64KiB | STM32 アプリ | STM32 flash `0x08010000` |
| `0x10004..0x4501f` | 217,116 B | nRF 系イメージ (SD + アプリ) | nRF flash `0x00001000` 起点 |
| `0x45020..0x4502f` | 16 | `AHUX01` + version フッタ | - |

nRF セクションは、先頭 12 バイト `0xff` + 16 バイトのハッシュ値に続いて、nRF flash `0x00001000` に対応するデータが始まる。

```text
16-byte hash (a6d96838 b8bfe6cb 45970e39 57706d31)
```

このハッシュはセクション検証用とみられる (CRC or CMAC 相当、計算手順は未確定)。

## nRF セクション内のレイアウト

nRF セクション内オフセットは、`nrf_offset = hfb_offset - 0x10004`。flash アドレスとの対応は以下で一致を確認した。

```text
flash = 0x00026000 + (nrf_offset - 0x2501C)
```

| nrf offset | flash アドレス | 内容 |
|---|---|---|
| `0x1C` | `0x00001000` | SoftDevice ベクタテーブル (SP=0x200012E0, Reset=0x000243D1) |
| `0x1C..0x23A40` | `0x00001000..0x00024A24` | SoftDevice S132 (SVC 多数、周辺レジスタアクセス) |
| `0x23A40..0x2501C` | `0x00024A24..0x00026000` | 0xff (未使用/アライン) |
| `0x2501C` | `0x00026000` | **アプリのベクタテーブル** (SP=0x20010000, Reset=0x00026239) |
| `0x2501C..0x309A0` | `0x00026000..0x00031985` | アプリコード (約 45KiB) |
| `0x309A1..0x3501B` | `0x00031985..0x00036000` | 0xff (未使用) |

## ベクタテーブル

### SoftDevice 側 (flash 0x1000)

```text
SP   = 0x200012E0   (SD 起動時スタック)
Reset= 0x000243D1
NMI  = 0x00024343
SVC  = 0x00024429
未使用 IRQ = 0x00002F19 (本体 0x2F18 は `bx lr` の空ハンドラ)
```

### アプリ側 (flash 0x26000)

```text
SP   = 0x20010000   (nRF52832 RAM 64KiB の終端 = スタック初期値として整合)
Reset= 0x00026239
NMI  = 0x00026291, HardFault = 0x00026293, ...
IRQ0 = 0x0002859D   (POWER_CLOCK)
IRQ1 = 0x000262A7, ...
```

アプリのベクタテーブルは例外ハンドラ (0x262xx) と IRQ ハンドラが独立しており、SDK 生成の標準的なアプリ構成。

## SoftDevice と型番の推定根拠

| 観測 | 値 | 意味 |
|---|---|---|
| アプリベース | `0x00026000` | SoftDevice サイズ ≒ 152KB → S132 v6.x 系に一致 |
| RAM リテラル上限 | `0x20010000` | RAM 64KiB = nRF52832 (nRF52840 なら 0x20040000) |
| SVC 命令 | 834 箇所 (0xFF を 769 箇所) | SoftDevice API 呼び出し主体 = SD ベース実装 |
| 周辺レジスタ | `0x4000F500`(CLOCK), `0x40008000`, `0x40011000`, `0x4001E000` 等 | nRF52 系レジスタマップ。`0x50000000`(CRYPTOCELL) 参照なし → nRF52840 の証拠なし |
| ASCII | ` HHKB-Hybrid`, ` PFU Limited`, ` nRF5x` | デバイス名 / メーカー名 / SDK 識別子 |

SoftDevice の正確な版数 (S132 v6.0/v6.1) と nRF52832/nRF52840 の最終確定には、次項の確認が必要。

## BLE プロファイル情報

アプリコード内に GATT 初期化シーケンスを確認した。

```text
0x000243C2 周辺: GAP Service (0x1800) 追加
                 Device Name (0x2A00) / Appearance (0x2A01) 設定
```

- BLE デバイス名: `HHKB-Hybrid`
- 製造元文字列: `PFU Limited`
- SoftDevice の `nRF5x` (S132 系) SDK 由来文字列

## 今後の残タスク

型番は EYSHCNZWZ (nRF52832) で確定した。残るのは:

1. **STM32↔nRF 通信プロトコルの逆解析**: アプリ IRQ ハンドラ (0x262A7 等) と `0x2BA11` 等が STM32 通信の受信処理である可能性が高い。STM32 FW 側の USART/SPI ドライバと突き合わせる。
2. **SoftDevice 版数の確定**: HFB 内の SD が S132 v6 系のどのリビジョンか。データシート上の出荷時は v5.0.0 固定。
3. **nRF 側 FW 更新フロー**: HFB の HID アップデートで STM32 が nRF flash へどう書き込むか (SWD/SPI/UART) の解明。EYSHCNZWZ は SWDCLK/SWDIO ピンを備えるため、モジュール側のパッド経由での書き換えも可能。

## 解析再現方法

`/tmp/HHKB800_FW_A048.hfb` は以下のコマンドで取得したもの。

```bash
curl -sL https://origin.pfultd.com/downloads/hhkb/HHKB800_FW_A048.hfb -o HHKB800_FW_A048.hfb
sha256sum HHKB800_FW_A048.hfb   # 635b995eb5a15aa50c2cedc60da7d8998fcca8285a09d41d984c07fffcaf9d43
```

```python
from pathlib import Path
import struct
data = Path('HHKB800_FW_A048.hfb').read_bytes()
app = data[4:4+0x10000]          # STM32
nrf = data[0x10004:0x10004+0x3501C]  # nRF (SD+app)
# ベクタテーブル
struct.unpack_from('<II', nrf, 0x1C)      # SD: SP/Reset
struct.unpack_from('<II', nrf, 0x2501C)   # App: SP/Reset
```
