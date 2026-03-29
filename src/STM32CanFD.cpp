#include "STM32CanFD.h"

/* 送信フォーマット指定用 */

// 複数のインスタンス管理用
static STM32CanFD *_instances[3] = {NULL, NULL, NULL};

STM32CanFD::STM32CanFD(FDCAN_GlobalTypeDef *instance)
    : _rxPin(NC), _txPin(NC), _mode(FDCAN_MODE_NORMAL)
#if STM32CANFD_STREAM_API
      ,
      _rxHead(0), _rxTail(0)
#endif
{
    hfdcan.Instance = instance;
    if (instance == FDCAN1)
        _instances[0] = this;
#if defined(FDCAN2)
    else if (instance == FDCAN2)
        _instances[1] = this;
#endif
#if defined(FDCAN3)
    else if (instance == FDCAN3)
        _instances[2] = this;
#endif
}

STM32CanFD::~STM32CanFD() { end(); }

void STM32CanFD::setPins(uint32_t rxPin, uint32_t txPin)
{
    _rxPin = rxPin;
    _txPin = txPin;
}

void STM32CanFD::setMode(uint32_t mode)
{
    _mode = mode;
}

bool STM32CanFD::begin(uint32_t nomBaud, uint32_t dataBaud)
{
    // if (_rxPin == NC || _txPin == NC)
    //     return false;

    // 170MHzソースを想定した基本設定
    hfdcan.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hfdcan.Init.FrameFormat = (dataBaud > 0) ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_CLASSIC;
    hfdcan.Init.Mode = _mode;
    hfdcan.Init.AutoRetransmission = DISABLE;
    hfdcan.Init.TransmitPause = DISABLE;
    hfdcan.Init.ProtocolException = DISABLE;
    hfdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    hfdcan.Init.StdFiltersNbr = 28;
    hfdcan.Init.ExtFiltersNbr = 8;

    applyBaudrate(nomBaud, dataBaud);

    /** Initializes the peripherals clocks */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_FDCAN_CLK_ENABLE();

    // PinName _can_rd = digitalPinToPinName(_rxPin);
    // PinName _can_tx = digitalPinToPinName(_txPin);
    // void *fdcan_rd = pinmap_peripheral(_can_rd, PinMap_CAN_RD);
    // void *fdcan_tx = pinmap_peripheral(_can_tx, PinMap_CAN_TD);
    // void *instance = pinmap_merge_peripheral(fdcan_rd, fdcan_tx);
    // pinmap_pinout(_can_rd, PinMap_CAN_RD);
    // pinmap_pinout(_can_tx, PinMap_CAN_TD);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /** FDCAN1 GPIO Configuration
    PB8-BOOT0     ------> FDCAN1_RX
    PB9     ------> FDCAN1_TX
    */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_PB8);
    __HAL_SYSCFG_FASTMODEPLUS_ENABLE(SYSCFG_FASTMODEPLUS_PB9);

    if (HAL_FDCAN_Init(&hfdcan) != HAL_OK)
        return false;

    // フィルタ：デフォルトは全通し（FIFO0へ）
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = 0x000;
    sFilterConfig.FilterID2 = 0x000;
    HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig);
    HAL_FDCAN_ConfigGlobalFilter(&hfdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);

    // 割り込み設定
    HAL_FDCAN_ActivateNotification(&hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO1_NEW_MESSAGE, 0);

    IRQn_Type irq0, irq1;
    if (hfdcan.Instance == FDCAN1) {
        irq0 = FDCAN1_IT0_IRQn;
        irq1 = FDCAN1_IT1_IRQn;
    }
#if defined(FDCAN2)
    else if (hfdcan.Instance == FDCAN2)
        irq0 = FDCAN2_IT0_IRQn;
        irq1 = FDCAN2_IT1_IRQn;
#endif
#if defined(FDCAN3)
    else if (hfdcan.Instance == FDCAN3)
        irq0 = FDCAN3_IT0_IRQn;
        irq1 = FDCAN3_IT1_IRQn;
#endif

    HAL_NVIC_SetPriority(irq0, 0, 0);
    HAL_NVIC_EnableIRQ(irq0);
    HAL_NVIC_SetPriority(irq1, 0, 0);
    HAL_NVIC_EnableIRQ(irq1);

    return HAL_FDCAN_Start(&hfdcan) == HAL_OK;
}

void STM32CanFD::end()
{
    HAL_FDCAN_DeInit(&hfdcan);
}

// 170MHz FDCAN Clock用ボーレート計算
// 内部で使用するパラメータ保持用の構造体
struct FDCAN_TimingParams {
    uint32_t baudrate;
    uint16_t prescaler;
    uint16_t timeSeg1;
    uint16_t timeSeg2;
    uint16_t sjw;
};

// 170MHzクロック向け Nominal (仲裁) フェーズ用テーブル
// Sample Point (SP) は 80% 付近をターゲットに
static const FDCAN_TimingParams nominalTimings[] = {
    // Baud, Prescaler, Seg1, Seg2, SJW
    {1000000, 1, 135, 34, 17}, // 1Mbps:   170MHz/1/(1+135+34) = 1.0M, SP=80%
    { 500000, 2, 135, 34, 17}, // 500kbps: 170MHz/2/(1+135+34) = 500k, SP=80%
    { 250000, 4, 135, 34, 17}, // 250kbps: 170MHz/4/(1+135+34) = 250k, SP=80%
    { 125000, 8, 135, 34, 17}  // 125kbps: 170MHz/8/(1+135+34) = 125k, SP=80%
};

// 170MHzクロック向け Data (データ) フェーズ用テーブル
// Sample Point (SP) は 75%〜80% 付近、SJWはSeg2未満を確保
static const FDCAN_TimingParams dataTimings[] = {
    // Baud, Prescaler, Seg1, Seg2, SJW
    {8000000, 1, 15, 5, 2},   // 8Mbps: 170/1/21 = 8.09M(+1.2%), SP=76%
    {5000000, 1, 26, 7, 3},   // 5Mbps: 170/1/34 = 5.0M,         SP=79%
    {4000000, 1, 31, 10, 5},  // 4Mbps: 170/1/42 = 4.04M(+1.1%), SP=76% (Seg1 max is 32)
    {2000000, 5, 12, 4, 2},   // 2Mbps: 170/5/17 = 2.0M,         SP=80%
    {1000000, 10, 12, 4, 2}   // 1Mbps: 170/10/17 = 1.0M,        SP=80%
};

void STM32CanFD::applyBaudrate(uint32_t nom, uint32_t data)
{
    // --- Nominal (Arbitration) Phase 設定 ---
    bool nomFound = false;
    for (const auto& timing : nominalTimings) {
        if (timing.baudrate == nom) {
            hfdcan.Init.NominalPrescaler = timing.prescaler;
            hfdcan.Init.NominalTimeSeg1 = timing.timeSeg1;
            hfdcan.Init.NominalTimeSeg2 = timing.timeSeg2;
            hfdcan.Init.NominalSyncJumpWidth = timing.sjw;
            nomFound = true;
            break;
        }
    }

    // テーブルにない場合はデフォルト設定 (500kbps相当) または計算ロジックを入れる
    if (!nomFound) {
        // Fallback: 500kbps defaults
        hfdcan.Init.NominalPrescaler = 2;
        hfdcan.Init.NominalTimeSeg1 = 135;
        hfdcan.Init.NominalTimeSeg2 = 34;
        hfdcan.Init.NominalSyncJumpWidth = 17;
    }

    // --- Data Phase 設定 ---
    if (data > 0)
    {
        bool dataFound = false;
        
        for (const auto& timing : dataTimings) {
            if (timing.baudrate == data) {
                hfdcan.Init.DataPrescaler = timing.prescaler;
                hfdcan.Init.DataTimeSeg1 = timing.timeSeg1;
                hfdcan.Init.DataTimeSeg2 = timing.timeSeg2;
                hfdcan.Init.DataSyncJumpWidth = timing.sjw;
                dataFound = true;
                break;
            }
        }

        if (!dataFound) {
            // Fallback: 2Mbps defaults
            hfdcan.Init.DataPrescaler = 5;
            hfdcan.Init.DataTimeSeg1 = 12;
            hfdcan.Init.DataTimeSeg2 = 4;
            hfdcan.Init.DataSyncJumpWidth = 2;
        }

        // TDC設定 (通信速度に応じて調整が必要だが、ここでは安全な値を設定)
        // 2Mbps以上なら少し小さめに、それ以外は標準的な値を設定
        uint32_t tdcOffset = (data >= 4000000) ? 20 : 25; 
        HAL_FDCAN_ConfigTxDelayCompensation(&hfdcan, tdcOffset, 0);
        HAL_FDCAN_EnableTxDelayCompensation(&hfdcan);
    }
    else
    {
        HAL_FDCAN_DisableTxDelayCompensation(&hfdcan);
    }
}

/* --- 送信ロジック --- */

#if STM32CANFD_STREAM_API
STM32CanFD &STM32CanFD::beginPacket(uint32_t id)
{
    _txHeader.identifier = id;
    _txHeader.dataLength = 0;
    _txHeader.extended = (id > 0x7FF) ? 1 : 0;
    _txHeader.remote = 0;
    _txHeader.fdFormat = 0;
    _txHeader.brs = 0;
    _txHeader.esiPassive = 0;
    if (hfdcan.Init.FrameFormat != FDCAN_FRAME_CLASSIC)
    {
        _txHeader.fdFormat = 1;
        _txHeader.brs = 1;
    }
    _txBufferIdx = 0;
    _txFormatExplicit = false;
    return *this;
}

STM32CanFD &STM32CanFD::standard()
{
    _txHeader.extended = 0;
    return *this;
}

STM32CanFD &STM32CanFD::extended()
{
    _txHeader.extended = 1;
    return *this;
}

STM32CanFD &STM32CanFD::fd(bool brs)
{
    _txHeader.fdFormat = 1;
    _txHeader.brs = brs ? 1 : 0;
    _txFormatExplicit = true;
    return *this;
}

STM32CanFD &STM32CanFD::classic()
{
    _txHeader.fdFormat = 0;
    _txHeader.brs = 0;
    _txFormatExplicit = true;
    return *this;
}

size_t STM32CanFD::write(uint8_t byte)
{
    if (_txBufferIdx < 64)
    {
        _txData[_txBufferIdx++] = byte;
        return 1;
    }
    return 0;
}

size_t STM32CanFD::write(const uint8_t *buffer, size_t size)
{
    size_t n = min(size, (size_t)(64 - _txBufferIdx));
    memcpy(&_txData[_txBufferIdx], buffer, n);
    _txBufferIdx += n;
    return n;
}

int STM32CanFD::endPacket()
{
    if (!_txFormatExplicit && _txBufferIdx > 8)
    {
        _txHeader.fdFormat = 1;
    }

    _txHeader.dataLength = len2dlc(_txBufferIdx);

    size_t fullLen = dlc2len(_txHeader.dataLength);
    if (fullLen > _txBufferIdx)
    {
        memset(&_txData[_txBufferIdx], 0, fullLen - _txBufferIdx);
    }

    return sendPacket(_txHeader, _txData) ? 1 : 0;
}

/* --- 受信ロジック --- */

bool STM32CanFD::parsePacket()
{
    uint32_t level0 = HAL_FDCAN_GetRxFifoFillLevel(&hfdcan, FDCAN_RX_FIFO0);
    uint32_t level1 = HAL_FDCAN_GetRxFifoFillLevel(&hfdcan, FDCAN_RX_FIFO1);
    uint8_t fifo = 0;

    if (level0 > 0)
    {
        fifo = 0;
    }
    else if (level1 > 0)
    {
        fifo = 1;
    }
    else
    {
        return false;
    }

    int received = readPacket(fifo, &_currentRx.header, _rxBuffer);
    if (received < 0)
        return false;

    _currentRx.length = (uint8_t)received;
    _currentRx.fifo = fifo;
    _rxHead = _currentRx.length;
    _rxTail = 0;
    return true;
}
#endif

int STM32CanFD::readPacket(uint8_t fifo, CanMessageHeader *header, uint8_t *buffer)
{
    if (fifo > 1 || header == nullptr || buffer == nullptr)
        return -1;

    FDCAN_RxHeaderTypeDef rxHeader;
    uint32_t fifoType = (fifo == 0) ? FDCAN_RX_FIFO0 : FDCAN_RX_FIFO1;
    if (HAL_FDCAN_GetRxMessage(&hfdcan, fifoType, &rxHeader, buffer) != HAL_OK)
        return -1;

    *header = compactRxHeader(rxHeader);
    return (int)dlc2len(header->dataLength);
}

bool STM32CanFD::sendPacket(const CanMessageHeader &header, const uint8_t *data)
{
    FDCAN_TxHeaderTypeDef txHeader = expandTxHeader(header);
    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan, &txHeader, (uint8_t *)data) == HAL_OK;
}

// --- Standard ID Filters (index: 0-27) ---
bool STM32CanFD::setFilterMask(uint8_t index, uint32_t id, uint32_t mask, int fifo)
{
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = index;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = (fifo == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = id;
    sFilterConfig.FilterID2 = mask;

    return HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig) == HAL_OK;
}

bool STM32CanFD::setFilterRange(uint8_t index, uint32_t id1, uint32_t id2, int fifo)
{
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = index;
    sFilterConfig.FilterType = FDCAN_FILTER_RANGE;
    sFilterConfig.FilterConfig = (fifo == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = id1;
    sFilterConfig.FilterID2 = id2;

    return HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig) == HAL_OK;
}

bool STM32CanFD::setFilterDual(uint8_t index, uint32_t id1, uint32_t id2, int fifo)
{
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = index;
    sFilterConfig.FilterType = FDCAN_FILTER_DUAL;
    sFilterConfig.FilterConfig = (fifo == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = id1;
    sFilterConfig.FilterID2 = id2;

    return HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig) == HAL_OK;
}

// --- Extended ID Filters (index: 0-7) ---
bool STM32CanFD::setFilterMaskExt(uint8_t index, uint32_t id, uint32_t mask, int fifo)
{
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
    sFilterConfig.FilterIndex = index;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = (fifo == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = id;
    sFilterConfig.FilterID2 = mask;

    return HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig) == HAL_OK;
}

bool STM32CanFD::setFilterRangeExt(uint8_t index, uint32_t id1, uint32_t id2, int fifo)
{
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
    sFilterConfig.FilterIndex = index;
    sFilterConfig.FilterType = FDCAN_FILTER_RANGE;
    sFilterConfig.FilterConfig = (fifo == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = id1;
    sFilterConfig.FilterID2 = id2;

    return HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig) == HAL_OK;
}

bool STM32CanFD::setFilterDualExt(uint8_t index, uint32_t id1, uint32_t id2, int fifo)
{
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
    sFilterConfig.FilterIndex = index;
    sFilterConfig.FilterType = FDCAN_FILTER_DUAL;
    sFilterConfig.FilterConfig = (fifo == 1) ? FDCAN_FILTER_TO_RXFIFO1 : FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = id1;
    sFilterConfig.FilterID2 = id2;

    return HAL_FDCAN_ConfigFilter(&hfdcan, &sFilterConfig) == HAL_OK;
}

uint32_t STM32CanFD::getErrorCode()
{
    return HAL_FDCAN_GetError(&hfdcan);
}

bool STM32CanFD::isBusOff()
{
    return (hfdcan.Instance->PSR & FDCAN_PSR_BO) != 0;
}

#if STM32CANFD_STREAM_API
int STM32CanFD::available() { return _rxHead - _rxTail; }
int STM32CanFD::read() { return (_rxHead > _rxTail) ? _rxBuffer[_rxTail++] : -1; }
int STM32CanFD::peek() { return (_rxHead > _rxTail) ? _rxBuffer[_rxTail] : -1; }
#endif

/* --- ユーティリティ --- */

uint8_t STM32CanFD::len2dlc(size_t len)
{
    if (len <= 8)
        return len;
    if (len <= 12)
        return FDCAN_DLC_BYTES_12;
    if (len <= 16)
        return FDCAN_DLC_BYTES_16;
    if (len <= 20)
        return FDCAN_DLC_BYTES_20;
    if (len <= 24)
        return FDCAN_DLC_BYTES_24;
    if (len <= 32)
        return FDCAN_DLC_BYTES_32;
    if (len <= 48)
        return FDCAN_DLC_BYTES_48;
    return FDCAN_DLC_BYTES_64;
}

size_t STM32CanFD::dlc2len(uint32_t dlc)
{
    if (dlc <= 8)
        return dlc;
    switch (dlc)
    {
    case FDCAN_DLC_BYTES_12:
        return 12;
    case FDCAN_DLC_BYTES_16:
        return 16;
    case FDCAN_DLC_BYTES_20:
        return 20;
    case FDCAN_DLC_BYTES_24:
        return 24;
    case FDCAN_DLC_BYTES_32:
        return 32;
    case FDCAN_DLC_BYTES_48:
        return 48;
    case FDCAN_DLC_BYTES_64:
        return 64;
    }
    return 0;
}

FDCAN_TxHeaderTypeDef STM32CanFD::expandTxHeader(const CanMessageHeader &header)
{
    FDCAN_TxHeaderTypeDef halHeader = {};
    halHeader.Identifier = header.identifier;
    halHeader.IdType = header.extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    halHeader.TxFrameType = header.remote ? FDCAN_REMOTE_FRAME : FDCAN_DATA_FRAME;
    halHeader.DataLength = header.dataLength;
    halHeader.ErrorStateIndicator = header.esiPassive ? FDCAN_ESI_PASSIVE : FDCAN_ESI_ACTIVE;
    halHeader.BitRateSwitch = header.brs ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    halHeader.FDFormat = header.fdFormat ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    halHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    halHeader.MessageMarker = 0;
    return halHeader;
}

STM32CanFD::CanMessageHeader STM32CanFD::compactRxHeader(const FDCAN_RxHeaderTypeDef &header)
{
    CanMessageHeader compact = {};
    compact.identifier = header.Identifier;
    compact.dataLength = (uint8_t)header.DataLength;
    compact.extended = (header.IdType == FDCAN_EXTENDED_ID) ? 1 : 0;
    compact.remote = (header.RxFrameType == FDCAN_REMOTE_FRAME) ? 1 : 0;
    compact.fdFormat = (header.FDFormat == FDCAN_FD_CAN) ? 1 : 0;
    compact.brs = (header.BitRateSwitch == FDCAN_BRS_ON) ? 1 : 0;
    compact.esiPassive = (header.ErrorStateIndicator == FDCAN_ESI_PASSIVE) ? 1 : 0;
    return compact;
}

// 割り込みハンドラの実装

void STM32CanFD::irqHandler()
{
    HAL_FDCAN_IRQHandler(&hfdcan);
}

extern "C" void FDCAN1_IT0_IRQHandler(void)
{
    if (_instances[0])
        _instances[0]->irqHandler();
}
extern "C" void FDCAN1_IT1_IRQHandler(void)
{
    if (_instances[0])
        _instances[0]->irqHandler();
}

#if defined(FDCAN2)
extern "C" void FDCAN2_IT0_IRQHandler(void)
{
    if (_instances[1])
        _instances[1]->irqHandler();
}
extern "C" void FDCAN2_IT1_IRQHandler(void)
{
    if (_instances[1])
        _instances[1]->irqHandler();
}
#endif
#if defined(FDCAN3)
extern "C" void FDCAN3_IT0_IRQHandler(void)
{
    if (_instances[2])
        _instances[2]->irqHandler();
}
extern "C" void FDCAN3_IT1_IRQHandler(void)
{
    if (_instances[2])
        _instances[2]->irqHandler();
}
#endif
