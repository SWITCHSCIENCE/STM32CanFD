#include "STM32CanFD.h"

// FDCAN1を使用
STM32CanFD CanFD(FDCAN1);

const int pinVIO = PB7;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

  pinMode(pinVIO, OUTPUT);
  digitalWrite(pinVIO, HIGH);

  // 500kbps (Nominal)
  if (!CanFD.begin(500000)) {
  // 500kbps (Nominal) / 2Mbps (Data)
//   if (!CanFD.begin(500000, 2000000)) {
    Serial.println("FDCAN Init Failed!");
    while (1)
      ;
  }

  // 全通しフィルターを設定（すべてのメッセージを受信）
  // 注意: 実際の使用では、setFilterMask()などで必要なIDのみ受信するフィルターを設定してください
  CanFD.setFilterMask(0, 0x000, 0x000);  // 全IDを通す

  Serial.println("FDCAN Ready.");
}

void loop() {
  // 1秒ごとに送信
  static uint32_t lastMsg = 0;
  if (millis() - lastMsg > 1000) {
    lastMsg = millis();

    CanFD.beginPacket(0x123);
    CanFD.print("Hello");
    CanFD.endPacket();
  }

  // 受信処理
  if (CanFD.parsePacket()) {
    Serial.print("Received Packet! ID: 0x");
    Serial.print(CanFD.packetId(), HEX);
    Serial.print(" Len: ");
    Serial.println(CanFD.available());

    while (CanFD.available()) {
      Serial.print((char)CanFD.read());
    }
    Serial.println();
  }
}
