# STM32CanFD Library for STM32G4

STM32G4シリーズのFDCANコントローラを、Arduino環境（STM32Duino）で簡単に扱うためのラッパーライブラリです。
STM32 HALライブラリを内部で使用し、Arduinoの `Stream` インターフェースライクな操作感で CAN FD (Flexible Data-rate) 通信を実現します。

## 特徴

* **CAN FD 対応**: 最大64バイトのデータペイロードに対応。
* **Bit Rate Switching (BRS) 対応**: データフェーズの高速化（例: 仲裁1Mbps / データ5Mbpsなど）をサポート。
* **ArduinoライクなAPI**: `begin()`, `write()`, `read()`, `parsePacket()` など、EthernetやWiFiライブラリに近い操作感。
* **自動設定**: 170MHzシステムクロック（STM32G4の最大動作周波数）に最適化されたビットタイミング設定およびTDC（送信遅延補償）設定を内蔵。
* **メソッドチェーン**: 送信パケットの構築を流れるように記述可能。

## 対応ハードウェア

* **MCU**: STM32G4シリーズ (Nucleo-G431RB 等)
    * ※ 本ライブラリは **170MHz** のカーネルクロックを前提にパラメータ計算されています。
    * 対応FDCAN：FDCAN1, FDCAN2 (サポート対象のチップのみ)
* **Transceiver**: CAN FD対応トランシーバ (TJA1044, MCP2542FD, TCAN332, TCAN3413 等)

## インストール

1.  このリポジトリ（フォルダ）を Arduino の `libraries` フォルダに配置してください。
2.  `STM32CanFD.h`, `STM32CanFD.cpp` がプロジェクトから参照可能になります。

## 使い方

### 1. 初期化

```cpp
#include "STM32CanFD.h"

STM32CanFD CanFD(FDCAN1);

void setup() {
  Serial.begin(115200);

  // ピン設定 (Rx, Tx)
  // Nucleo-G431RBのデフォルトピン配置: PA11/PA12
  // 自作基板等の場合: PB8/PB9 など
  CanFD.setPins(PB8, PB9);

  // 通信開始
  // begin(仲裁ボーレート, データボーレート)
  // 例: 仲裁 500kbps, データ 2Mbps
  if (!CanFD.begin(500000, 2000000)) {
    Serial.println("FDCAN Init Failed!");
    while (1);
  }
}
```

### 2. 送信 (Transmission)

メソッドチェーンを使用してパケットを作成・送信します。

```cpp
void loop() {
  uint8_t data[] = {0x11, 0x22, 0x33, 0x44};

  // 標準ID (0x123) で CAN FD (BRS有効) パケットを送信
  CanFD.beginPacket(0x123)
       .fd(true)          // FDモード有効, BRS有効
       .write(data, 4);   // データ書き込み

  if (CanFD.endPacket()) {
      Serial.println("Packet sent");
  } else {
      Serial.println("Tx Error or Buffer Full");
  }

  delay(1000);
}
```

#### fdFormat（フレームフォーマット）の自動決定ロジック

`_txHeader.fdFormat` は、以下のルールに従って自動的に決定されます：

| 条件 | fdFormat | 説明 |
|------|----------|------|
| `classic()` で明示的に指定 | **0** (CAN 2.0B) | `classic()` 呼び出しで確定。引数による自動判定は無視されます |
| `fd(true)` で明示的に指定 | **1** (CAN FD with BRS) | `fd(true)` 呼び出しで確定。Bit Rate Switching を有効化 |
| `fd(false)` で明示的に指定 | **1** (CAN FD without BRS) | `fd(false)` 呼び出しで確定。Bit Rate Switching を無効化 |
| 明示的指定なし、データ ≤ 8 バイト | `begin(dataBaud)` に依存 | データが8バイト以下の場合は `begin()` で指定されたフレームフォーマットを継承 |
| 明示的指定なし、データ > 8 バイト | **1** (CAN FD) | **自動切り替え** ーー クラシックCAN制限(8バイト)を超える容量が必要な場合、自動的にCAN FDに切り替え |

##### 使用例

```cpp
void setup() {
  // パターン1: begin()でCANFD指定 (dataBaud指定あり)
  CanFD.begin(500000, 2000000);  // 仲裁 500k, データ 2Mbps → fdFormat初期値=1
}

void loop() {
  // ケース1: 明示的に classic() を指定 → CAN 2.0B で送信
  CanFD.beginPacket(0x100);
  CanFD.classic();  // 強制的にCAN 2.0B (fdFormat=0)
  CanFD.write((uint8_t)0x11);
  CanFD.endPacket();  // → CAN 2.0B フレーム (8バイト以下必須)

  // ケース2: 明示的に fd() を指定 → CAN FD で送信
  CanFD.beginPacket(0x200);
  CanFD.fd(true);  // 強制的にCAN FD with BRS (fdFormat=1)
  CanFD.write((uint8_t)0x22);
  CanFD.endPacket();  // → CAN FD フレーム

  // ケース3: 明示的指定なし、データ小 → begin() の設定に従う
  CanFD.beginPacket(0x300);
  // classic() も fd() も呼ばない
  for (int i = 0; i < 5; i++) {
    CanFD.write((uint8_t)i);
  }
  CanFD.endPacket();  // → 5バイト ≤ 8 → begin()での設定に従う

  // ケース4: 明示的指定なし、データ大 → 自動的にCAN FDに切り替え
  CanFD.beginPacket(0x400);
  // classic() も fd() も呼ばない
  for (int i = 0; i < 20; i++) {
    CanFD.write((uint8_t)i);
  }
  CanFD.endPacket();  // → 20バイト > 8 → 自動的に fdFormat=1 (CAN FD) に切り替え

  delay(100);
}
```

### 3. 受信 (Reception)

```cpp
void loop() {
  // パケット受信確認 (戻り値 true で受信あり)
  if (CanFD.parsePacket()) {
    Serial.print("Received ID: 0x");
    Serial.println(CanFD.packetId(), HEX);

    Serial.print("DLC: ");
    Serial.println(CanFD.packetDlc());

    Serial.print("Format: ");
    if (CanFD.packetFdf())
      Serial.print("CAN FD ");
    else
      Serial.print("Classic CAN ");

    if (CanFD.packetBrs()) Serial.println("(BRS: ON)");
    else Serial.println("(BRS: OFF)");

    Serial.print("Data: ");
    while (CanFD.available()) {
      Serial.print(CanFD.read(), HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
}
```

## API リファレンス

### 初期化・設定

* `STM32CanFD(FDCAN_GlobalTypeDef *instance)`: コンストラクタ。`FDCAN1`, `FDCAN2` 等を指定。複数インスタンス同時使用可能。
* `void setPins(uint32_t rxPin, uint32_t txPin)`: 使用するピンを設定。`HAL_GPIO_Init` 等の設定と一致させる必要があります。
* `bool begin(uint32_t nomBaud, uint32_t dataBaud)`: 初期化と通信開始。`dataBaud` に `0` を指定すると Classic CAN モードになります。成功時 `true`。
  * 対応ボーレート（仲裁フェーズ）: 125k, 250k, 500k, 1M bps
  * 対応ボーレート（データフェーズ）: 1M, 2M, 4M, 5M, 8M bps
* `void setMode(uint32_t mode)`: 動作モード設定。`FDCAN_MODE_NORMAL` (デフォルト), `FDCAN_MODE_EXTERNAL_LOOPBACK` 等。
* `void end()`: 通信を終了。

### 送信

* `STM32CanFD &beginPacket(uint32_t id)`: パケット作成開始。IDを指定。
* `STM32CanFD &fd(bool brs)`: CAN FDモードに設定。`brs=true` で高速転送有効。
* `STM32CanFD &classic()`: Classic CAN (2.0B) モードに設定。
* `STM32CanFD &standard()` / `&extended()`: IDタイプの設定。
* `size_t write(uint8_t byte)`: データをバッファに書き込む。
* `size_t write(const uint8_t *buffer, size_t size)`: バッファデータを一括書き込む（最大64バイト）。
* `int endPacket()`: パケットを送信キューに入れる。成功時 `1`、失敗時 `0`。バッファ満杯等で送信失敗も返す `0`。

### 受信

* `bool parsePacket()`: 新しいパケットが受信されているか確認。受信時は `true` を返す。複数のパケットがある場合は先着順に処理。
* `int available()`: 受信バッファにあるデータのバイト数を返す。
* `int read()`: 1バイト読み出す。バッファが空の場合は `-1` を返す。
* `int peek()`: バッファを読み出さずに次のバイトをプレビューする。
* `uint32_t packetId()`: 受信したパケットのIDを取得。
* `uint8_t packetFifo()`: 受信したパケットが格納されたFIFO番号（0 or 1）。
* `bool packetExtended()`: 拡張IDかどうか。
* `bool packetRtr()`: リモートフレームかどうか。
* `bool packetFdf()`: CAN FDフレームかどうか。
* `bool packetBrs()`: BRSビットが立っていたか（高速転送されたか）。CAN FDで `fd(true)` の場合に `true`。
* `uint8_t packetDlc()`: データ長コード (0-15)。実際のデータバイト数は `available()` で確認。

### フィルター設定（標準ID: index 0-27）

* `bool setFilterMask(uint8_t index, uint32_t id, uint32_t mask, int fifo = 0)`: マスクフィルター設定。 `(受信ID & mask) == (id & mask)` で受信。`fifo` は 0 (FIFO0) or 1 (FIFO1)。
* `bool setFilterRange(uint8_t index, uint32_t id1, uint32_t id2, int fifo = 0)`: レンジフィルター設定。 `id1 ≤ 受信ID ≤ id2` の範囲で受信。
* `bool setFilterDual(uint8_t index, uint32_t id1, uint32_t id2, int fifo = 0)`: デュアルフィルター設定。 `受信ID == id1 || 受信ID == id2` で受信。

### フィルター設定（拡張ID: index 0-7）

* `bool setFilterMaskExt(uint8_t index, uint32_t id, uint32_t mask, int fifo = 0)`: 拡張ID用マスクフィルター。
* `bool setFilterRangeExt(uint8_t index, uint32_t id1, uint32_t id2, int fifo = 0)`: 拡張ID用レンジフィルター。
* `bool setFilterDualExt(uint8_t index, uint32_t id1, uint32_t id2, int fifo = 0)`: 拡張ID用デュアルフィルター。

### エラーハンドリング

* `uint32_t getErrorCode()`: エラーコードを取得（HAL_FDCAN_GetError の戻り値）。
* `bool isBusOff()`: バスオフ状態かどうかを確認。

## 使用例

### 複数インスタンスでの使用

```cpp
// FDCAN1 と FDCAN2 を同時使用
STM32CanFD CanFD1(FDCAN1);
STM32CanFD CanFD2(FDCAN2);

void setup() {
  CanFD1.setPins(PA11, PA12);
  CanFD1.begin(500000, 2000000);

  CanFD2.setPins(PB8, PB9);
  CanFD2.begin(500000, 2000000);
}

void loop() {
  // CanFD1 送信
  CanFD1.beginPacket(0x100).write((uint8_t)0xAA).endPacket();

  // CanFD2 受信
  if (CanFD2.parsePacket()) {
    Serial.println(CanFD2.read(), HEX);
  }
}
```

### フィルター設定の例

```cpp
void setup() {
  CanFD.begin(500000, 2000000);

  // 標準ID 0x100-0x1FF を受信（マスクフィルター）
  CanFD.setFilterMask(0, 0x100, 0x700);

  // 標準ID 0x200, 0x201 を受信（デュアルフィルター）
  CanFD.setFilterDual(1, 0x200, 0x201);

  // 拡張ID 0x18DAF100 を受信
  CanFD.setFilterMaskExt(0, 0x18DAF100, 0xFFFFFFFF);
}
```

### エラーハンドリングの例

```cpp
void loop() {
  if (CanFD.isBusOff()) {
    Serial.println("BusOff detected!");
    // 定期的に復旧を試みる等の処理
  }

  uint32_t errorCode = CanFD.getErrorCode();
  if (errorCode) {
    Serial.print("Error: 0x");
    Serial.println(errorCode, HEX);
  }
}
```

## 注意事項

1.  **クロック周波数**: 本ライブラリは `SystemCoreClock` が **170MHz** であることを前提にビットタイミング（Prescaler, TimeSeg）を計算しています。異なるクロックで動作させる場合は `STM32CanFD.cpp` 内の `nominalTimings` および `dataTimings` テーブルの修正が必要です。
2.  **バッファサイズ**: STM32G4のFDCANはハードウェア制約により、Tx Buffer/FIFO および Rx FIFO はそれぞれ最大3メッセージ分しか保持できません。送信頻度が高い場合は `endPacket()` の戻り値をチェックしてください。
3.  **ピン配置**: STM32G4はピンのAlternate Function割り当てが柔軟ですが、ハードウェアの配線と一致する正しいピンを `setPins` で指定してください。※本実装はデフォルトで PB8/PB9 (FDCAN1) に固定されています。
4.  **終端抵抗**: CANバスの両端には必ず120Ωの終端抵抗を接続してください。
5.  **TDC (Transmitter Delay Compensation)**: データフェーズボーレートが 4Mbps 以上の場合、TDCが自動的に有効になります。
6.  **Stream API**: `STM32CANFD_DISABLE_STREAM_API` を定義するとStream APIを無効にでき、代わりに `readPacket()` と `sendPacket()` の低レベルAPIを使用できます。
7.  **複数FDCAN**: 複数のFDCANを使用する場合、各インスタンスが独立して動作します。ただしIRQ優先度の競合を避けるよう注意してください。
