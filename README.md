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

### 3. 受信 (Reception)

```cpp
void loop() {
  // パケット受信確認 (戻り値 true で受信あり)
  if (CanFD.parsePacket()) {
    Serial.print("Received ID: 0x");
    Serial.println(CanFD.packetId(), HEX);

    if (CanFD.packetBrs()) Serial.println("BRS: ON");

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

* `STM32CanFD(FDCAN_GlobalTypeDef *instance)`: コンストラクタ。`FDCAN1` 等を指定。
* `void setPins(uint32_t rxPin, uint32_t txPin)`: 使用するピンを設定。`HAL_GPIO_Init` 等の設定と一致させる必要があります。
* `bool begin(uint32_t nomBaud, uint32_t dataBaud)`: 初期化と通信開始。`dataBaud` に `0` を指定すると Classic CAN モードになります。成功時 `true`。
* `void setMode(uint32_t mode)`: 動作モード設定。`FDCAN_MODE_NORMAL` (デフォルト), `FDCAN_MODE_EXTERNAL_LOOPBACK` 等。

### 送信

* `STM32CanFD &beginPacket(uint32_t id)`: パケット作成開始。IDを指定。
* `STM32CanFD &fd(bool brs)`: CAN FDモードに設定。`brs=true` で高速転送有効。
* `STM32CanFD &classic()`: Classic CAN (2.0B) モードに設定。
* `STM32CanFD &standard()` / `&extended()`: IDタイプの設定。
* `size_t write(uint8_t byte)`: データをバッファに書き込む。
* `int endPacket()`: パケットを送信キューに入れる。成功時 `1`、失敗時 `0`。

### 受信

* `bool parsePacket()`: 新しいパケットが受信されているか確認。受信時は `true` を返す。
* `int available()`: 受信バッファにあるデータのバイト数を返す。
* `int read()`: 1バイト読み出す。
* `uint32_t packetId()`: 受信したパケットのIDを取得。
* `bool packetExtended()`: 拡張IDかどうか。
* `bool packetRtr()`: リモートフレームかどうか。
* `bool packetBrs()`: BRSビットが立っていたか（高速転送されたか）。
* `uint8_t packetDlc()`: データ長コードを取得。

## 注意事項

1.  **クロック周波数**: 本ライブラリは `SystemCoreClock` が **170MHz** であることを前提にビットタイミング（Prescaler, TimeSeg）を計算しています。異なるクロックで動作させる場合は `STM32CanFD.cpp` 内の `nominalTimings` および `dataTimings` テーブルの修正が必要です。
2.  **バッファサイズ**: STM32G4のFDCANはハードウェア制約により、Tx Buffer/FIFO および Rx FIFO はそれぞれ最大3メッセージ分しか保持できません。送信頻度が高い場合は `endPacket()` の戻り値をチェックしてください。
3.  **ピン配置**: STM32G4はピンのAlternate Function割り当てが柔軟ですが、ハードウェアの配線と一致する正しいピンを `setPins` で指定してください。
4.  **終端抵抗**: CANバスの両端には必ず120Ωの終端抵抗を接続してください。
5.  **TDC (Transmitter Delay Compensation)**: 高速通信（データフェーズ）時はTDCが自動的に有効になります。
