/**
 * =====================================================================================
 * @file        E2E.h
 * @brief       AUTOSAR R25-11 End-to-End Protection Library — Profile 5 & Profile 22
 *              Provides CRC-16-CCITT based data integrity over Shared SRAM IPC channel.
 *              Detects: data corruption, packet loss, insertion, masquerading, replay.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_E2ELibrary, IEC 61508
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#ifndef E2E_H
#define E2E_H

#include "Platform_Types.h"

/*=====================================================================================
 * E2E Status / Error Codes
 *====================================================================================*/
typedef uint8 E2E_PCheckStatusType;
#define E2E_P_STATUS_OK            ((E2E_PCheckStatusType)0x00U) /**< Data valid        */
#define E2E_P_STATUS_REPEATED      ((E2E_PCheckStatusType)0x01U) /**< Counter repeated  */
#define E2E_P_STATUS_WRONGSEQUENCE ((E2E_PCheckStatusType)0x02U) /**< Counter wrong seq */
#define E2E_P_STATUS_ERROR         ((E2E_PCheckStatusType)0x03U) /**< CRC mismatch      */
#define E2E_P_STATUS_NOTAVAILABLE  ((E2E_PCheckStatusType)0x04U) /**< No data yet       */
#define E2E_P_STATUS_NONEWDATA     ((E2E_PCheckStatusType)0x05U) /**< Stale data        */

/*=====================================================================================
 * E2E Profile 5 Configuration (AUTOSAR R25-11 §7.5)
 *  Used for: IPC AI command frames (variable-length, up to 512 bytes)
 *====================================================================================*/
#define E2E_P5_MAX_DELTA_COUNTER    (1U)     /**< Max tolerated counter skip            */
#define E2E_P5_HEADER_OFFSET_BYTES  (0U)     /**< CRC & Counter at byte 0               */
#define E2E_P5_CRC_POLYNOMIAL       (0x1021U)/**< CRC-16-CCITT generator polynomial     */

typedef struct
{
    uint16 MaxDeltaCounter;     /**< Max tolerated delta between consecutive counters    */
    uint16 MinOkStateInit;      /**< Minimum OK states needed before data is accepted   */
    uint16 MaxErrorStateInit;   /**< Max consecutive errors before ERROR state declared  */
    uint16 WindowSize;          /**< Sliding window size for monitoring                  */
    uint16 DataLength;          /**< Length of protected data payload in bytes           */
} E2E_P5ConfigType;

typedef struct
{
    uint16 Counter;             /**< Current expected counter value                      */
    uint16 OkCount;             /**< Count of consecutive OK results                     */
    uint16 ErrorCount;          /**< Count of consecutive error results                  */
    boolean NewDataAvailable;   /**< Flag: fresh data received since last check          */
} E2E_P5CheckStateType;

typedef struct
{
    uint16 Counter;             /**< Transmit counter (increments each protect call)     */
} E2E_P5ProtectStateType;

/*=====================================================================================
 * E2E Profile 22 Configuration (AUTOSAR R25-11 §7.22)
 *  Used for: chassis safety command frames (fixed 8–32 bytes, deterministic)
 *====================================================================================*/
#define E2E_P22_MAX_DELTA_COUNTER   (1U)
#define E2E_P22_CRC_POLYNOMIAL      (0x1021U)
#define E2E_P22_COUNTER_MASK        (0x0FU)  /**< 4-bit rolling counter                 */

typedef struct
{
    uint8  DataIDList[16];      /**< 16-byte Data ID list for profile 22 CRC input       */
    uint16 Offset;              /**< Bit offset to E2E header within the data            */
    uint16 DataLength;          /**< Length of protected data in bits                    */
    uint8  MaxDeltaCounter;     /**< Maximum allowed counter delta                       */
} E2E_P22ConfigType;

typedef struct
{
    uint8  Counter;             /**< 4-bit rolling counter                               */
    uint8  OkCount;
    uint8  ErrorCount;
    boolean NewDataAvailable;
} E2E_P22CheckStateType;

typedef struct
{
    uint8 Counter;
} E2E_P22ProtectStateType;

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Protect a data frame using E2E Profile 5 (CRC-16-CCITT + counter).
 * @param  Config   [in]  Pointer to static profile 5 configuration.
 * @param  State    [io]  Protect state (counter incremented on success).
 * @param  Data     [io]  Pointer to data buffer. CRC & counter written to header.
 * @param  Length   [in]  Total data length in bytes including E2E header.
 * @return E_OK on success, E_NOT_OK if invalid parameters.
 * @req    SWS_E2E_00385
 */
extern Std_ReturnType E2E_P5Protect(
    const E2E_P5ConfigType    * const Config,
    E2E_P5ProtectStateType    * const State,
    uint8                     * const Data,
    uint16                            Length);

/**
 * @brief  Check a received data frame using E2E Profile 5.
 * @param  Config   [in]  Pointer to static profile 5 configuration.
 * @param  State    [io]  Check state (counters updated).
 * @param  Data     [in]  Pointer to received data buffer.
 * @param  Length   [in]  Total data length in bytes.
 * @param  Status   [out] Check result status.
 * @return E_OK on success, E_NOT_OK if invalid parameters.
 * @req    SWS_E2E_00386
 */
extern Std_ReturnType E2E_P5Check(
    const E2E_P5ConfigType    * const Config,
    E2E_P5CheckStateType      * const State,
    const uint8               * const Data,
    uint16                            Length,
    E2E_PCheckStatusType      * const Status);

/**
 * @brief  Protect a data frame using E2E Profile 22.
 * @req    SWS_E2E_00445
 */
extern Std_ReturnType E2E_P22Protect(
    const E2E_P22ConfigType   * const Config,
    E2E_P22ProtectStateType   * const State,
    uint8                     * const Data);

/**
 * @brief  Check a received data frame using E2E Profile 22.
 * @req    SWS_E2E_00446
 */
extern Std_ReturnType E2E_P22Check(
    const E2E_P22ConfigType   * const Config,
    E2E_P22CheckStateType     * const State,
    const uint8               * const Data,
    E2E_PCheckStatusType      * const Status);

/**
 * @brief  Calculate CRC-16-CCITT over a byte array.
 * @note   Used internally by Profile 5 and Profile 22. Exposed for unit testing.
 * @param  Data    [in]  Input data buffer.
 * @param  Length  [in]  Length in bytes.
 * @param  StartValue [in] Initial CRC value (use 0xFFFFU per CCITT spec).
 * @return 16-bit CRC result.
 */
extern uint16 E2E_CRC16_CCITT(
    const uint8 * const Data,
    uint16              Length,
    uint16              StartValue);

#endif /* E2E_H */
