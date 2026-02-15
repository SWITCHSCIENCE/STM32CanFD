#include "STM32CanFD.h"

/* 送信フォーマット指定用 */
#define CAN_FMT_AUTO -1
#define CAN_FMT_CLASSIC 0
#define CAN_FMT_FD 1

// 複数のインスタンス管理用
static STM32CanFD *_instances[3] = {NULL, NULL, NULL};

STM32CanFD::STM32CanFD(FDCAN_GlobalTypeDef *instance)
    : _rxPin(NC), _txPin(NC), _mode(FDCAN_MODE_NORMAL), _is_fd_enabled(false),
      _rxHead(0), _rxTail(0)
{
    _hfdcan.Instance = instance;
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
    _hfdcan.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    _hfdcan.Init.FrameFormat = (dataBaud > 0) ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_CLASSIC;
    _hfdcan.Init.Mode = _mode;
    _hfdcan.Init.AutoRetransmission = DISABLE;
    _hfdcan.Init.TransmitPause = DISABLE;
    _hfdcan.Init.ProtocolException = DISABLE;
    _hfdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

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

    if (HAL_FDCAN_Init(&_hfdcan) != HAL_OK)
        return false;

    // フィルタ：デフォルトは全通し（FIFO0へ）
    FDCAN_FilterTypeDef sFilterConfig;
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = 0x000;
    sFilterConfig.FilterID2 = 0x000;
    HAL_FDCAN_ConfigFilter(&_hfdcan, &sFilterConfig);
    HAL_FDCAN_ConfigGlobalFilter(&_hfdcan, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);

    // 割り込み設定
    HAL_FDCAN_ActivateNotification(&_hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);

    IRQn_Type irq0, irq1;
    if (_hfdcan.Instance == FDCAN1) {
        irq0 = FDCAN1_IT0_IRQn;
        irq1 = FDCAN1_IT1_IRQn;
    }
#if defined(FDCAN2)
    else if (_hfdcan.Instance == FDCAN2)
        irq0 = FDCAN2_IT0_IRQn;
        irq1 = FDCAN2_IT1_IRQn;
#endif
#if defined(FDCAN3)
    else if (_hfdcan.Instance == FDCAN3)
        irq0 = FDCAN3_IT0_IRQn;
        irq1 = FDCAN3_IT1_IRQn;
#endif

    HAL_NVIC_SetPriority(irq0, 0, 0);
    HAL_NVIC_EnableIRQ(irq0);
    HAL_NVIC_SetPriority(irq1, 0, 0);
    HAL_NVIC_EnableIRQ(irq1);

    return HAL_FDCAN_Start(&_hfdcan) == HAL_OK;
}

void STM32CanFD::end()
{
    HAL_FDCAN_DeInit(&_hfdcan);
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
            _hfdcan.Init.NominalPrescaler = timing.prescaler;
            _hfdcan.Init.NominalTimeSeg1 = timing.timeSeg1;
            _hfdcan.Init.NominalTimeSeg2 = timing.timeSeg2;
            _hfdcan.Init.NominalSyncJumpWidth = timing.sjw;
            nomFound = true;
            break;
        }
    }

    // テーブルにない場合はデフォルト設定 (500kbps相当) または計算ロジックを入れる
    if (!nomFound) {
        // Fallback: 500kbps defaults
        _hfdcan.Init.NominalPrescaler = 2;
        _hfdcan.Init.NominalTimeSeg1 = 135;
        _hfdcan.Init.NominalTimeSeg2 = 34;
        _hfdcan.Init.NominalSyncJumpWidth = 17;
    }

    // --- Data Phase 設定 ---
    if (data > 0)
    {
        _is_fd_enabled = true;
        bool dataFound = false;
        
        for (const auto& timing : dataTimings) {
            if (timing.baudrate == data) {
                _hfdcan.Init.DataPrescaler = timing.prescaler;
                _hfdcan.Init.DataTimeSeg1 = timing.timeSeg1;
                _hfdcan.Init.DataTimeSeg2 = timing.timeSeg2;
                _hfdcan.Init.DataSyncJumpWidth = timing.sjw;
                dataFound = true;
                break;
            }
        }

        if (!dataFound) {
            // Fallback: 2Mbps defaults
            _hfdcan.Init.DataPrescaler = 5;
            _hfdcan.Init.DataTimeSeg1 = 12;
            _hfdcan.Init.DataTimeSeg2 = 4;
            _hfdcan.Init.DataSyncJumpWidth = 2;
        }

        // TDC設定 (通信速度に応じて調整が必要だが、ここでは安全な値を設定)
        // 2Mbps以上なら少し小さめに、それ以外は標準的な値を設定
        uint32_t tdcOffset = (data >= 4000000) ? 20 : 25; 
        HAL_FDCAN_ConfigTxDelayCompensation(&_hfdcan, tdcOffset, 0);
        HAL_FDCAN_EnableTxDelayCompensation(&_hfdcan);
    }
    else
    {
        _is_fd_enabled = false;
        HAL_FDCAN_DisableTxDelayCompensation(&_hfdcan);
    }
}
// void STM32CanFD::applyBaudrate(uint32_t nom, uint32_t data)
// {
//     // 170MHz / 500kbps = 340 Tq
//     // Prescaler 2 => 170 Tq. Sync=1, Seg1=135, Seg2=34 (SP=80%)
//     _hfdcan.Init.NominalPrescaler = 2;
//     _hfdcan.Init.NominalTimeSeg1 = 135;
//     _hfdcan.Init.NominalTimeSeg2 = 34;
//     _hfdcan.Init.NominalSyncJumpWidth = 17;

//     if (data > 0)
//     {
//         _is_fd_enabled = true;
//         // 170MHz / 1Mbps = 85 Tq
//         // Prescaler 5 => 85 Tq. Sync=1, Seg1=67, Seg2=17 (SP=80%)
//         _hfdcan.Init.DataPrescaler = 5;
//         _hfdcan.Init.DataTimeSeg1 = 12;
//         _hfdcan.Init.DataTimeSeg2 = 4;
//         _hfdcan.Init.DataSyncJumpWidth = 2;

//         // TDC設定 (BRS時に必須)
//         HAL_FDCAN_ConfigTxDelayCompensation(&_hfdcan, 20, 0);
//         HAL_FDCAN_EnableTxDelayCompensation(&_hfdcan);
//     }
//     else
//     {
//         _is_fd_enabled = false;
//     }
// }

/* --- 送信ロジック --- */

STM32CanFD &STM32CanFD::beginPacket(uint32_t id)
{
    _txHeader.Identifier = id;
    _txHeader.IdType = (id > 0x7FF) ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    _txHeader.TxFrameType = FDCAN_DATA_FRAME;
    _txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    _txHeader.BitRateSwitch = _is_fd_enabled ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    _txHeader.FDFormat = _is_fd_enabled ? FDCAN_FD_CAN : FDCAN_CLASSIC_CAN;
    _txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    _txHeader.MessageMarker = 0;

    _txBufferIdx = 0;
    _txFormat = CAN_FMT_AUTO;
    return *this;
}

STM32CanFD &STM32CanFD::standard()
{
    _txHeader.IdType = FDCAN_STANDARD_ID;
    return *this;
}

STM32CanFD &STM32CanFD::extended()
{
    _txHeader.IdType = FDCAN_EXTENDED_ID;
    return *this;
}

STM32CanFD &STM32CanFD::fd(bool brs)
{
    _txHeader.FDFormat = FDCAN_FD_CAN;
    _txHeader.BitRateSwitch = brs ? FDCAN_BRS_ON : FDCAN_BRS_OFF;
    _txFormat = CAN_FMT_FD;
    return *this;
}

STM32CanFD &STM32CanFD::classic()
{
    _txHeader.FDFormat = FDCAN_CLASSIC_CAN;
    _txHeader.BitRateSwitch = FDCAN_BRS_OFF;
    _txFormat = CAN_FMT_CLASSIC;
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
    if (_txFormat == CAN_FMT_AUTO && _txBufferIdx > 8)
    {
        _txHeader.FDFormat = FDCAN_FD_CAN;
    }

    _txHeader.DataLength = len2dlc(_txBufferIdx);

    // パディング (CAN FDはDLCで規定された長さに合わせる必要がある)
    size_t fullLen = dlc2len(_txHeader.DataLength);
    if (fullLen > _txBufferIdx)
    {
        memset(&_txData[_txBufferIdx], 0, fullLen - _txBufferIdx);
    }

    if (HAL_FDCAN_AddMessageToTxFifoQ(&_hfdcan, &_txHeader, _txData) != HAL_OK)
    {
        return 0;
    }
    return 1;
}

/* --- 受信ロジック --- */

bool STM32CanFD::parsePacket()
{
    // リングバッファに溜まっているデータがあるか確認
    // 本実装では簡易化のため、HALから直接FIFOを確認し、
    // Streamとして読めるように内部バッファへコピーする
    if (HAL_FDCAN_GetRxFifoFillLevel(&_hfdcan, FDCAN_RX_FIFO0) > 0)
    {
        if (HAL_FDCAN_GetRxMessage(&_hfdcan, FDCAN_RX_FIFO0, &_rxHeader, _rxBuffer) == HAL_OK)
        {
            _currentRx.length = dlc2len(_rxHeader.DataLength);
            _rxHead = _currentRx.length;
            _rxTail = 0;
            return true;
        }
    }
    return false;
}

int STM32CanFD::available() { return _rxHead - _rxTail; }
int STM32CanFD::read() { return (available()) ? _rxBuffer[_rxTail++] : -1; }
int STM32CanFD::peek() { return (available()) ? _rxBuffer[_rxTail] : -1; }

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

// 割り込みハンドラの実装

void STM32CanFD::irqHandler()
{
    HAL_FDCAN_IRQHandler(&_hfdcan);
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
