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

  // G431/G474などのピン配置例 (RX:PB8, TX:PB9)
  CanFD.setPins(PB8, PB9);

  // 500kbps (Nominal)
  if (!CanFD.begin(500000)) {
  // 500kbps (Nominal) / 2Mbps (Data)
//   if (!CanFD.begin(500000, 2000000)) {
    Serial.println("FDCAN Init Failed!");
    while (1)
      ;
  }
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
