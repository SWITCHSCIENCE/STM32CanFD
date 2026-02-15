# STM32CanFD Library for STM32G4

STM32G4シリーズのFDCANコントローラを、Arduino環境（STM32Duino）で簡単に扱うためのラッパーライブラリです。
STM32 HALライブラリを内部で使用し、Arduinoの `Stream` インターフェースライクな操作感で CAN FD (Flexible Data-rate) 通信を実現します。

## 特徴

* **CAN FD 対応**: ISO 11898-1:2015 準拠。最大64バイトのデータペイロードに対応。
* **Bit Rate Switching (BRS) 対応**: データフェーズの高速化（例: 仲裁1Mbps / データ5Mbpsなど）をサポート。
* **ArduinoライクなAPI**: `begin()`, `write()`, `read()`, `parsePacket()` など、EthernetやWiFiライブラリに近い操作感。
* **自動設定**: 170MHzシステムクロック（STM32G4の最大動作周波数）に最適化されたビットタイミング設定を内蔵。
* **メソッドチェーン**: 送信パケットの構築を流れるように記述可能。

## 対応ハードウェア

* **MCU**: STM32G4シリーズ (Nucleo-G431RB 等)
    * ※ 本ライブラリは **170MHz** のカーネルクロックを前提にパラメータ計算されています。
* **Transceiver**: CAN FD対応トランシーバ (TJA1044, MCP2542FD, TCAN3414 等)

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
  // Nucleo-G431RBの例: PA11/PA12 または PB8/PB9
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
```cpp
void loop() {
  uint8_t data[] = {0x11, 0x22, 0x33, 0x44};

  // 標準ID (0x123) で CAN FD (BRS有効) パケットを送信
  CanFD.beginPacket(0x123)
       .fd(true)          // FDモード有効, BRS有効
       .write(data, 4);   // データ書き込み
  CanFD.endPacket();      // 送信実行

  delay(1000);
}
```

### 3. 受信 (Reception)

```cpp
void loop() {
  // パケット受信確認
  if (CanFD.parsePacket()) {
    Serial.print("Received ID: 0x");
    Serial.println(CanFD.packetId(), HEX);

    Serial.print("Data: ");
    while (CanFD.available()) {
      Serial.print(CanFD.read(), HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
}
```
