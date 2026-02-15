#ifndef STM32_CAN_FD_H
#define STM32_CAN_FD_H

#include <Arduino.h>

class STM32CanFD : public Stream
{
public:
    STM32CanFD(FDCAN_GlobalTypeDef *instance = FDCAN1);
    ~STM32CanFD();

    // ピン設定 (Arduinoのピン番号。MspInitでAlternate Functionを設定)
    void setPins(uint32_t rxPin, uint32_t txPin);

    /**
     * ボーレート設定 (170MHz FDCANクロックに最適化された計算を行います)
     * @param nomBaud   仲裁フェーズ (例: 500000)
     * @param dataBaud  データフェーズ (例: 2000000) 0でClassic CANモード
     */
    bool begin(uint32_t nomBaud = 500000, uint32_t dataBaud = 2000000);
    void end();

    // モード設定 (begin前に呼び出し)
    void setMode(uint32_t mode = FDCAN_MODE_NORMAL); // FDCAN_MODE_EXTERNAL_LOOPBACK等

    /* --- 送信関連 (メソッドチェーン) --- */
    STM32CanFD &beginPacket(uint32_t id);
    STM32CanFD &standard();
    STM32CanFD &extended();
    STM32CanFD &fd(bool brs = true);
    STM32CanFD &classic();

    virtual size_t write(uint8_t byte) override;
    virtual size_t write(const uint8_t *buffer, size_t size) override;
    using Stream::write;
    int endPacket();

    /* --- 受信関連 --- */
    bool parsePacket();
    virtual int available() override;
    virtual int read() override;
    virtual int peek() override;
    virtual void flush() override {}

    uint32_t packetId() { return _rxHeader.Identifier; }
    bool packetExtended() { return _rxHeader.IdType == FDCAN_EXTENDED_ID; }
    bool packetRtr() { return _rxHeader.RxFrameType == FDCAN_REMOTE_FRAME; }
    bool packetFdf() { return _rxHeader.FDFormat == FDCAN_FD_CAN; }
    bool packetBrs() { return _rxHeader.BitRateSwitch == FDCAN_BRS_ON; }
    uint8_t packetDlc() { return _rxHeader.DataLength; }

    /* --- フィルタ設定 --- */
    bool setFilter(uint8_t index, uint32_t id, uint32_t mask, bool isExtended = false);

    /* --- 状態取得 --- */
    uint32_t getErrorCode();
    bool isBusOff();

    // HALコールバック用内部関数
    void irqHandler();

private:
    FDCAN_HandleTypeDef _hfdcan;
    FDCAN_TxHeaderTypeDef _txHeader;
    FDCAN_RxHeaderTypeDef _rxHeader;

    uint32_t _rxPin, _txPin;
    uint32_t _mode;
    bool _is_fd_enabled;

    // 送信一時バッファ
    uint8_t _txData[64];
    size_t _txBufferIdx;
    int _txFormat;
    bool _txBrs;

    // 受信リングバッファ (割り込み用)
    static const size_t RX_BUFFER_SIZE = 512;
    uint8_t _rxBuffer[RX_BUFFER_SIZE];
    volatile size_t _rxHead;
    size_t _rxTail;

    // 受信済みパケット情報
    struct RxPacketMeta
    {
        FDCAN_RxHeaderTypeDef header;
        size_t length;
    } _currentRx;

    void applyBaudrate(uint32_t nom, uint32_t data);
    static uint8_t len2dlc(size_t len);
    static size_t dlc2len(uint32_t dlc);
};

#endif