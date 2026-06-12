/**
 * =====================================================================================
 * @file        E2E.c
 * @brief       AUTOSAR R25-11 End-to-End Protection Library Implementation
 *              Profile 5 (variable-length IPC frames) and Profile 22 (fixed chassis frames).
 *              CRC-16-CCITT implemented via optimized lookup table (256-entry, ROM).
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_E2ELibrary
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "E2E.h"

/* MISRA C:2023 Rule 20.1 — MemMap open */
#define E2E_START_SEC_CONST_UNSPECIFIED
#include "MemMap.h"

/*=====================================================================================
 * CRC-16-CCITT Lookup Table (Polynomial: 0x1021)
 * Generated at compile-time equivalent; stored in ROM for deterministic timing.
 *====================================================================================*/
static const uint16 E2E_Crc16Table[256U] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U, 0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU, 0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
    0x1231U, 0x0210U, 0x3273U, 0x2252U, 0x52B5U, 0x4294U, 0x72F7U, 0x62D6U,
    0x9339U, 0x8318U, 0xB37BU, 0xA35AU, 0xD3BDU, 0xC39CU, 0xF3FFU, 0xE3DEU,
    0x2462U, 0x3443U, 0x0420U, 0x1401U, 0x64E6U, 0x74C7U, 0x44A4U, 0x5485U,
    0xA56AU, 0xB54BU, 0x8528U, 0x9509U, 0xE5EEU, 0xF5CFU, 0xC5ACU, 0xD58DU,
    0x3653U, 0x2672U, 0x1611U, 0x0630U, 0x76D7U, 0x66F6U, 0x5695U, 0x46B4U,
    0xB75BU, 0xA77AU, 0x9719U, 0x8738U, 0xF7DFU, 0xE7FEU, 0xD79DU, 0xC7BCU,
    0x48C4U, 0x58E5U, 0x6886U, 0x78A7U, 0x0840U, 0x1861U, 0x2802U, 0x3823U,
    0xC9CCU, 0xD9EDU, 0xE98EU, 0xF9AFU, 0x8948U, 0x9969U, 0xA90AU, 0xB92BU,
    0x5AF5U, 0x4AD4U, 0x7AB7U, 0x6A96U, 0x1A71U, 0x0A50U, 0x3A33U, 0x2A12U,
    0xDBFDU, 0xCBDCU, 0xFBBFU, 0xEB9EU, 0x9B79U, 0x8B58U, 0xBB3BU, 0xAB1AU,
    0x6CA6U, 0x7C87U, 0x4CE4U, 0x5CC5U, 0x2C22U, 0x3C03U, 0x0C60U, 0x1C41U,
    0xEDAEU, 0xFD8FU, 0xCDECU, 0xDDCDU, 0xAD2AU, 0xBD0BU, 0x8D68U, 0x9D49U,
    0x7E97U, 0x6EB6U, 0x5ED5U, 0x4EF4U, 0x3E13U, 0x2E32U, 0x1E51U, 0x0E70U,
    0xFF9FU, 0xEFBEU, 0xDFDDU, 0xCFFCU, 0xBF1BU, 0xAF3AU, 0x9F59U, 0x8F78U,
    0x9188U, 0x81A9U, 0xB1CAU, 0xA1EBU, 0xD10CU, 0xC12DU, 0xF14EU, 0xE16FU,
    0x1080U, 0x00A1U, 0x30C2U, 0x20E3U, 0x5004U, 0x4025U, 0x7046U, 0x6067U,
    0x83B9U, 0x9398U, 0xA3FBU, 0xB3DAU, 0xC33DU, 0xD31CU, 0xE37FU, 0xF35EU,
    0x02B1U, 0x1290U, 0x22F3U, 0x32D2U, 0x4235U, 0x5214U, 0x6277U, 0x7256U,
    0xB5EAU, 0xA5CBU, 0x95A8U, 0x8589U, 0xF56EU, 0xE54FU, 0xD52CU, 0xC50DU,
    0x34E2U, 0x24C3U, 0x14A0U, 0x0481U, 0x7466U, 0x6447U, 0x5424U, 0x4405U,
    0xA7DBU, 0xB7FAU, 0x8799U, 0x97B8U, 0xE75FU, 0xF77EU, 0xC71DU, 0xD73CU,
    0x26D3U, 0x36F2U, 0x0691U, 0x16B0U, 0x6657U, 0x7676U, 0x4615U, 0x5634U,
    0xD94CU, 0xC96DU, 0xF90EU, 0xE92FU, 0x99C8U, 0x89E9U, 0xB98AU, 0xA9ABU,
    0x5844U, 0x4865U, 0x7806U, 0x6827U, 0x18C0U, 0x08E1U, 0x3882U, 0x28A3U,
    0xCB7DU, 0xDB5CU, 0xEB3FU, 0xFB1EU, 0x8BF9U, 0x9BD8U, 0xABBBU, 0xBB9AU,
    0x4A75U, 0x5A54U, 0x6A37U, 0x7A16U, 0x0AF1U, 0x1AD0U, 0x2AB3U, 0x3A92U,
    0xFD2EU, 0xED0FU, 0xDD6CU, 0xCD4DU, 0xBDAAU, 0xAD8BU, 0x9DE8U, 0x8DC9U,
    0x7C26U, 0x6C07U, 0x5C64U, 0x4C45U, 0x3CA2U, 0x2C83U, 0x1CE0U, 0x0CC1U,
    0xEF1FU, 0xFF3EU, 0xCF5DU, 0xDF7CU, 0xAF9BU, 0xBFBAU, 0x8FD9U, 0x9FF8U,
    0x6E17U, 0x7E36U, 0x4E55U, 0x5E74U, 0x2E93U, 0x3EB2U, 0x0ED1U, 0x1EF0U
};

#define E2E_STOP_SEC_CONST_UNSPECIFIED
#include "MemMap.h"

/* MISRA C:2023 Rule 20.1 — MemMap open for code */
#define E2E_START_SEC_CODE
#include "MemMap.h"

/*=====================================================================================
 * Internal Helper: CRC-16-CCITT Table Lookup
 *====================================================================================*/
uint16 E2E_CRC16_CCITT(
    const uint8 * const Data,
    uint16              Length,
    uint16              StartValue)
{
    uint16 crc = StartValue;
    uint16 index;

    if (Data == NULL_PTR)
    {
        /* MISRA C:2023 Rule 15.5 — single exit with error value */
        crc = 0x0000U;
    }
    else
    {
        for (index = 0U; index < Length; index++)
        {
            uint8 tableIdx = (uint8)(((crc >> 8U) ^ Data[index]) & 0xFFU);
            crc = (uint16)((crc << 8U) ^ E2E_Crc16Table[tableIdx]);
        }
    }

    return crc;
}

/*=====================================================================================
 * E2E Profile 5 — Protect
 *  Header layout (bytes 0-3):
 *   [0..1] = CRC-16-CCITT (computed over bytes 2..N)
 *   [2..3] = uint16 Counter (little-endian)
 *====================================================================================*/
Std_ReturnType E2E_P5Protect(
    const E2E_P5ConfigType    * const Config,
    E2E_P5ProtectStateType    * const State,
    uint8                     * const Data,
    uint16                            Length)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint16         crc;

    if ((Config == NULL_PTR) || (State == NULL_PTR) || (Data == NULL_PTR) ||
        (Length < 4U) || (Length != Config->DataLength))
    {
        /* Parameter validation failed — return without modification */
        retVal = E_NOT_OK;
    }
    else
    {
        /* Write counter into header bytes [2..3] (little-endian) */
        Data[2U] = (uint8)(State->Counter & 0xFFU);
        Data[3U] = (uint8)((State->Counter >> 8U) & 0xFFU);

        /* CRC over bytes [2..N] (skip CRC field itself at [0..1]) */
        crc = E2E_CRC16_CCITT(&Data[2U], (uint16)(Length - 2U), 0xFFFFU);

        /* Write CRC into header bytes [0..1] (big-endian) */
        Data[0U] = (uint8)((crc >> 8U) & 0xFFU);
        Data[1U] = (uint8)(crc & 0xFFU);

        /* Increment counter, wrap at 0xFFFF */
        State->Counter = (State->Counter == 0xFFFFU) ? 0U : (State->Counter + 1U);

        retVal = E_OK;
    }

    return retVal;
}

/*=====================================================================================
 * E2E Profile 5 — Check
 *====================================================================================*/
Std_ReturnType E2E_P5Check(
    const E2E_P5ConfigType    * const Config,
    E2E_P5CheckStateType      * const State,
    const uint8               * const Data,
    uint16                            Length,
    E2E_PCheckStatusType      * const Status)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint16         receivedCrc;
    uint16         computedCrc;
    uint16         receivedCounter;
    sint16         deltaCounter;

    if (Status == NULL_PTR)
    {
        /* Status output unavailable — reject without dereference (CERT EXP34-C) */
        retVal = E_NOT_OK;
    }
    else if ((Config == NULL_PTR) || (State == NULL_PTR) ||
             (Data == NULL_PTR)   ||
             (Length < 4U)        || (Length != Config->DataLength))
    {
        *Status = E2E_P_STATUS_ERROR;
        retVal = E_NOT_OK;
    }
    else
    {
        /* Extract received CRC from bytes [0..1] (big-endian) */
        receivedCrc = (uint16)(((uint16)Data[0U] << 8U) | (uint16)Data[1U]);

        /* Extract received counter from bytes [2..3] (little-endian) */
        receivedCounter = (uint16)((uint16)Data[2U] | ((uint16)Data[3U] << 8U));

        /* Recompute CRC over bytes [2..N] */
        computedCrc = E2E_CRC16_CCITT(&Data[2U], (uint16)(Length - 2U), 0xFFFFU);

        if (computedCrc != receivedCrc)
        {
            /* CRC mismatch — data corrupted */
            State->ErrorCount++;
            *Status = E2E_P_STATUS_ERROR;
        }
        else
        {
            /* CRC OK — check counter sequence */
            deltaCounter = (sint16)((sint16)receivedCounter - (sint16)State->Counter);

            if (deltaCounter == 0)
            {
                /* Same counter as last — repeated data */
                *Status = E2E_P_STATUS_REPEATED;
            }
            else if ((deltaCounter > 0) &&
                     (deltaCounter <= (sint16)Config->MaxDeltaCounter))
            {
                /* Counter advanced within tolerance — OK */
                State->Counter  = receivedCounter;
                State->OkCount++;
                State->ErrorCount = 0U;
                State->NewDataAvailable = TRUE;
                *Status = E2E_P_STATUS_OK;
            }
            else
            {
                /* Counter jumped too far — wrong sequence */
                State->ErrorCount++;
                *Status = E2E_P_STATUS_WRONGSEQUENCE;
            }
        }

        retVal = E_OK;
    }

    return retVal;
}

/*=====================================================================================
 * E2E Profile 22 — Protect
 *  Header layout (bit-level per AUTOSAR R25-11 §7.22.4):
 *   Bits [0..7]  = CRC-8 (using CRC-16 truncated for Profile 22 compliance)
 *   Bits [8..11] = 4-bit Counter
 *   Bits [12..15] = DataID nibble (DataIDList[Counter] low nibble)
 *  Note: For this implementation DataLength must be >= 2 bytes.
 *====================================================================================*/
Std_ReturnType E2E_P22Protect(
    const E2E_P22ConfigType   * const Config,
    E2E_P22ProtectStateType   * const State,
    uint8                     * const Data)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint16         crc;
    uint8          counterNibble;
    uint8          dataIdByte;

    if ((Config == NULL_PTR) || (State == NULL_PTR) || (Data == NULL_PTR))
    {
        retVal = E_NOT_OK;
    }
    else
    {
        counterNibble = (uint8)(State->Counter & E2E_P22_COUNTER_MASK);
        dataIdByte    = Config->DataIDList[counterNibble];

        /* Write nibble counter into high nibble of Data[1] */
        Data[1U] = (uint8)((Data[1U] & 0x0FU) | (uint8)(counterNibble << 4U));

        /* CRC covers DataIDList byte + Data[1..N] */
        uint8 crcInput[2U];
        crcInput[0U] = dataIdByte;
        crcInput[1U] = Data[1U];
        crc = E2E_CRC16_CCITT(crcInput, 2U, 0xFFFFU);

        /* Truncate to 8-bit CRC (upper byte of CRC-16) */
        Data[0U] = (uint8)((crc >> 8U) & 0xFFU);

        /* Increment 4-bit counter */
        State->Counter = (uint8)((State->Counter + 1U) & E2E_P22_COUNTER_MASK);

        retVal = E_OK;
    }

    return retVal;
}

/*=====================================================================================
 * E2E Profile 22 — Check
 *====================================================================================*/
Std_ReturnType E2E_P22Check(
    const E2E_P22ConfigType   * const Config,
    E2E_P22CheckStateType     * const State,
    const uint8               * const Data,
    E2E_PCheckStatusType      * const Status)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint8          receivedCrc;
    uint8          computedCrc;
    uint8          receivedCounter;
    uint8          dataIdByte;
    uint16         crc16;
    sint8          delta;

    if (Status == NULL_PTR)
    {
        /* Status output unavailable — reject without dereference (CERT EXP34-C) */
        retVal = E_NOT_OK;
    }
    else if ((Config == NULL_PTR) || (State == NULL_PTR) ||
             (Data == NULL_PTR))
    {
        *Status = E2E_P_STATUS_ERROR;
        retVal = E_NOT_OK;
    }
    else
    {
        receivedCrc     = Data[0U];
        receivedCounter = (uint8)((Data[1U] >> 4U) & E2E_P22_COUNTER_MASK);
        dataIdByte      = Config->DataIDList[receivedCounter];

        uint8 crcInput[2U];
        crcInput[0U] = dataIdByte;
        crcInput[1U] = Data[1U];
        crc16        = E2E_CRC16_CCITT(crcInput, 2U, 0xFFFFU);
        computedCrc  = (uint8)((crc16 >> 8U) & 0xFFU);

        if (computedCrc != receivedCrc)
        {
            State->ErrorCount++;
            *Status = E2E_P_STATUS_ERROR;
        }
        else
        {
            /* 4-bit counter wrap-around: counter space = 16 (0..15).
             * Forward delta computed modulo-16 to correctly handle
             * wrap-around (e.g., received=0, expected=15 → delta=1).
             * AUTOSAR R25-11 SWS_E2E §7.22.5.3 */
            delta = (sint8)(((uint8)(receivedCounter - State->Counter)) &
                            E2E_P22_COUNTER_MASK);

            if (delta == 0)
            {
                *Status = E2E_P_STATUS_REPEATED;
            }
            else if ((delta > 0) && (delta <= (sint8)Config->MaxDeltaCounter))
            {
                /* Counter advanced within tolerance window — frame is new and valid */
                State->Counter         = receivedCounter;
                State->OkCount++;
                State->ErrorCount      = 0U;
                State->NewDataAvailable = TRUE;
                *Status = E2E_P_STATUS_OK;
            }
            else
            {
                /* Counter jumped beyond MaxDeltaCounter — sequence error */
                State->ErrorCount++;
                *Status = E2E_P_STATUS_WRONGSEQUENCE;
            }
        }

        retVal = E_OK;
    }

    return retVal;
}

#define E2E_STOP_SEC_CODE
#include "MemMap.h"
