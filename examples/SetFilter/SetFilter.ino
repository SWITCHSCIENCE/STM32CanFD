#include <STM32CanFD.h>

// FDCAN1を使用 (デフォルト)
STM32CanFD CANFD;

// CANトランシーバーVIOピン
const int pinVIO = PB7;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;

  // CANトランシーバーVIOピンに電源供給
  pinMode(pinVIO, OUTPUT);
  digitalWrite(pinVIO, HIGH);

  // 外部ループバックモード
  CANFD.setMode(FDCAN_MODE_EXTERNAL_LOOPBACK);

  // Classic CANのみの場合: 仲裁フェーズ(500k)
  CANFD.begin(500000);
  // CAN-FDの場合: 仲裁フェーズ(500k), データフェーズ(2M)
  //CANFD.begin(500000, 2000000);

  // --- フィルタ設定のテスト (6種類のフィルターを同時に設定) ---

  // 1. マスクフィルター: ID 0x100 のみ受信 (フィルターインデックス 0)
  CANFD.setFilterMask(0, 0x100, 0x7FF);

  // 2. 範囲フィルター: ID 0x200 から 0x2FF まで受信 (フィルターインデックス 1)
  CANFD.setFilterRange(1, 0x200, 0x2FF);

  // 3. デュアルフィルター: ID 0x300 と 0x400 を受信 (フィルターインデックス 2)
  CANFD.setFilterDual(2, 0x300, 0x400);

  // 4. 拡張マスクフィルター: ID 0x1000 のみ受信 (フィルターインデックス 0)
  CANFD.setFilterMaskExt(0, 0x1000, 0x7FFF, 1);

  // 5. 拡張範囲フィルター: ID 0x2000 から 0x2FFF まで受信 (フィルターインデックス 1)
  CANFD.setFilterRangeExt(1, 0x2000, 0x2FFF, 1);

  // 6. 拡張デュアルフィルター: ID 0x3000 と 0x4000 を受信 (フィルターインデックス 2)
  CANFD.setFilterDualExt(2, 0x3000, 0x4000, 1);

  Serial.println("6種類のフィルター設定完了:");
  Serial.println("- マスクフィルター: ID 0x100");
  Serial.println("- 範囲フィルター: ID 0x200-0x2FF");
  Serial.println("- デュアルフィルター: ID 0x300, 0x400");
  Serial.println("- 拡張マスクフィルター: ID 0x1000");
  Serial.println("- 拡張範囲フィルター: ID 0x2000-0x2FFF");
  Serial.println("- 拡張デュアルフィルター: ID 0x3000, 0x4000");
}

void loop() {
  static unsigned long lastSend = 0;
  static int testIndex = 0;

  // 受信処理
  if (CANFD.parsePacket()) {
    Serial.print("受信 ID: 0x");
    Serial.print(CANFD.packetId(), HEX);
    Serial.print(" FIFO: ");
    Serial.print(CANFD.packetFifo());
    Serial.print(" DLC: ");
    Serial.print(CANFD.packetDlc());
    Serial.print(" データ: ");
    while (CANFD.available()) {
      Serial.print(" 0x");
      Serial.print(CANFD.read(), HEX);
    }
    Serial.println();
  }

  // フィルター設定テスト用の送信 (1秒ごとに異なるIDで送信)
  if (millis() - lastSend > 500) {
    uint32_t testIds[] = { 0x100, 0x250, 0x300, 0x400, 0x500, 0x1000, 0x2500, 0x3000, 0x4000, 0x5000 };  // フィルター対象 + 非対象
    uint32_t sendId = testIds[testIndex % 5];

    CANFD.beginPacket(sendId);
    CANFD.write(0xAA);
    CANFD.write(0xBB);
    CANFD.write(0xCC);
    CANFD.write(testIndex % 256);  // テスト番号
    CANFD.endPacket();

    Serial.print("送信 ID: 0x");
    Serial.print(sendId, HEX);
    Serial.print(" (テスト ");
    Serial.print(testIndex % 5 + 1);
    Serial.println("/5)");

    lastSend = millis();
    testIndex++;
  }

  delay(200);
}