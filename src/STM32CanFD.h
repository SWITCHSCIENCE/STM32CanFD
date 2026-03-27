#ifndef STM32_CAN_FD_H
#define STM32_CAN_FD_H

#include <Arduino.h>

#if defined(STM32CANFD_DISABLE_STREAM_API)
#define STM32CANFD_STREAM_API 0
#else
#define STM32CANFD_STREAM_API 1
#endif

#if STM32CANFD_STREAM_API
class STM32CanFD : public Stream
#else
class STM32CanFD
#endif
{
public:
    struct CanMessageHeader
    {
        uint32_t identifier;
        uint8_t dataLength;
        uint8_t extended : 1;
        uint8_t remote : 1;
        uint8_t fdFormat : 1;
        uint8_t brs : 1;
        uint8_t esiPassive : 1;
    };

    STM32CanFD(FDCAN_GlobalTypeDef *instance = FDCAN1);
    ~STM32CanFD();

    void setPins(uint32_t rxPin, uint32_t txPin);
    bool begin(uint32_t nomBaud = 500000, uint32_t dataBaud = 2000000);
    void end();
    void setMode(uint32_t mode = FDCAN_MODE_NORMAL);

#if STM32CANFD_STREAM_API
    STM32CanFD &beginPacket(uint32_t id);
    STM32CanFD &standard();
    STM32CanFD &extended();
    STM32CanFD &fd(bool brs = true);
    STM32CanFD &classic();

    virtual size_t write(uint8_t byte) override;
    virtual size_t write(const uint8_t *buffer, size_t size) override;
    using Stream::write;
    int endPacket();

    bool parsePacket();
    virtual int available() override;
    virtual int read() override;
    virtual int peek() override;
    virtual void flush() override {}

    uint32_t packetId() { return _currentRx.header.identifier; }
    uint8_t packetFifo() { return _currentRx.fifo; }
    bool packetExtended() { return _currentRx.header.extended != 0; }
    bool packetRtr() { return _currentRx.header.remote != 0; }
    bool packetFdf() { return _currentRx.header.fdFormat != 0; }
    bool packetBrs() { return _currentRx.header.brs != 0; }
    uint8_t packetDlc() { return _currentRx.header.dataLength; }
#endif

    int readPacket(uint8_t fifo, CanMessageHeader *header, uint8_t *buffer);
    bool setFilter(uint8_t index, uint32_t id, uint32_t mask, bool isExtended = false, int fifo = 0);
    bool sendPacket(const CanMessageHeader &header, const uint8_t *data, size_t len);
    uint32_t getErrorCode();
    bool isBusOff();
    void irqHandler();
    FDCAN_HandleTypeDef _hfdcan;

private:
    uint32_t _rxPin, _txPin;
    uint32_t _mode;

#if STM32CANFD_STREAM_API
    CanMessageHeader _txHeader;
    uint8_t _txData[64];
    size_t _txBufferIdx;
    bool _txFormatExplicit;

    static const size_t RX_BUFFER_SIZE = 64;
    uint8_t _rxBuffer[RX_BUFFER_SIZE];
    volatile size_t _rxHead;
    size_t _rxTail;

    struct RxPacketMeta
    {
        CanMessageHeader header;
        uint8_t length;
        uint8_t fifo;
    } _currentRx;
#endif

    void applyBaudrate(uint32_t nom, uint32_t data);
    static uint8_t len2dlc(size_t len);
    static size_t dlc2len(uint32_t dlc);
    static FDCAN_TxHeaderTypeDef expandTxHeader(const CanMessageHeader &header);
    static CanMessageHeader compactRxHeader(const FDCAN_RxHeaderTypeDef &header);
};

#endif
