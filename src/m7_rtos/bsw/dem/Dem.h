/**
 * =====================================================================================
 * @file        Dem.h
 * @brief       AUTOSAR R25-11 Diagnostic Event Manager (DEM) — Interface Header
 *              Provides DTC (Diagnostic Trouble Code) logging, freeze frame capture,
 *              and UDS-compatible event status management for ASIL-D safety violations.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_DiagnosticEventManager,
 *              ISO 14229-1 (UDS), SAE J2012
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#ifndef DEM_H
#define DEM_H

#include "Platform_Types.h"

/*=====================================================================================
 * DEM Event Status Byte (ISO 14229-1 §11.2.1 — UDS statusOfDTC)
 *====================================================================================*/
typedef uint8 Dem_EventStatusType;
#define DEM_EVENT_STATUS_PASSED        ((Dem_EventStatusType)0x00U)
#define DEM_EVENT_STATUS_FAILED        ((Dem_EventStatusType)0x01U)
#define DEM_EVENT_STATUS_PREPASSED     ((Dem_EventStatusType)0x02U)
#define DEM_EVENT_STATUS_PREFAILED     ((Dem_EventStatusType)0x03U)

/*=====================================================================================
 * DEM Event ID Definitions — norxs Safety-Supervisor DTC Catalogue
 * DTC format: 0xCXYZ — C=chassis, X=subsystem, YZ=fault code
 *====================================================================================*/
typedef uint16 Dem_EventIdType;

/* Safety Arbitrator Events */
#define DEM_EVENT_SAFETY_ENVELOPE_VIOLATED    ((Dem_EventIdType)0xC101U)
#define DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED  ((Dem_EventIdType)0xC102U)
#define DEM_EVENT_SAFE_STATE_ENTRY            ((Dem_EventIdType)0xC103U)
#define DEM_EVENT_SAFE_STATE_TIMEOUT          ((Dem_EventIdType)0xC104U)

/* Vehicle Dynamics Events */
#define DEM_EVENT_WHEEL_SPEED_DELTA_HIGH      ((Dem_EventIdType)0xC201U)
#define DEM_EVENT_YAW_RATE_IMPLAUSIBLE        ((Dem_EventIdType)0xC202U)
#define DEM_EVENT_ROAD_MU_LOW_CONFIDENCE      ((Dem_EventIdType)0xC203U)
#define DEM_EVENT_SENSOR_REDUNDANCY_FAIL      ((Dem_EventIdType)0xC204U)

/* IPC / E2E Events */
#define DEM_EVENT_IPC_E2E_ERROR               ((Dem_EventIdType)0xC301U)
#define DEM_EVENT_IPC_BUFFER_OVERFLOW         ((Dem_EventIdType)0xC302U)
#define DEM_EVENT_IPC_FRAME_TIMEOUT           ((Dem_EventIdType)0xC303U)

/* OTA / Watchdog Events */
#define DEM_EVENT_OTA_ROLLBACK_TRIGGERED      ((Dem_EventIdType)0xC401U)
#define DEM_EVENT_WDGM_ALIVE_SUPERVISION_FAIL ((Dem_EventIdType)0xC402U)
#define DEM_EVENT_WDGM_DEADLINE_FAIL          ((Dem_EventIdType)0xC403U)

/* CSM / HSE Cryptographic Events */
#define DEM_EVENT_CSM_MAC_VERIFICATION_FAIL   ((Dem_EventIdType)0xC501U)
#define DEM_EVENT_CSM_KEY_EXPIRED             ((Dem_EventIdType)0xC502U)

/* OS / Platform Events */
#define DEM_EVENT_OS_TASK_OVERRUN             ((Dem_EventIdType)0xC601U)
#define DEM_EVENT_SBST_FAILURE                ((Dem_EventIdType)0xC602U)

/*=====================================================================================
 * DEM Freeze Frame Data — captured at moment of first failure
 *====================================================================================*/
typedef struct
{
    uint32  Timestamp_ms;           /**< System time at fault occurrence (ms)            */
    float32 VehicleSpeed_mps;       /**< Vehicle speed at fault (m/s)                    */
    float32 YawRate_radps;          /**< Yaw rate at fault (rad/s)                       */
    float32 SteerAngle_deg;         /**< Steering angle at fault (degrees)               */
    float32 LateralAccel_mps2;      /**< Lateral acceleration at fault (m/s²)            */
    uint8   SafeStateActive;        /**< Safe state was active at time of fault           */
    uint8   AiCmdOverrideCount;     /**< Number of AI overrides in last 100ms             */
    uint16  IPC_FrameSeqNum;        /**< IPC sequence number at fault                    */
} Dem_FreezeFrameType;

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Set the status of a DEM event (PASSED/FAILED/PREFAILED/PREPASSED).
 *         Internally manages confirmation counters and triggers NvM write on confirmed
 *         failure per AUTOSAR R25-11 SWS_Dem_00234.
 * @param  EventId   [in]  DEM event identifier.
 * @param  EventStatus [in] New event status.
 * @return E_OK on success.
 * @req    SWS_Dem_00234
 */
extern Std_ReturnType Dem_SetEventStatus(
    Dem_EventIdType    EventId,
    Dem_EventStatusType EventStatus);

/**
 * @brief  Capture freeze frame data for a specific event.
 *         Called immediately after Dem_SetEventStatus(FAILED) to record fault context.
 * @param  EventId    [in]  DEM event identifier.
 * @param  FreezeData [in]  Pointer to freeze frame data structure.
 * @return E_OK on success.
 */
extern Std_ReturnType Dem_SetFreezeFrameData(
    Dem_EventIdType          EventId,
    const Dem_FreezeFrameType * const FreezeData);

/**
 * @brief  Report a raw fault byte directly (used by WDGM / CSM subsystems).
 * @param  EventId   [in]  DEM event identifier.
 * @param  EventStatus [in] New event status.
 * @return E_OK on success.
 * @req    SWS_Dem_00589
 */
extern Std_ReturnType Dem_ReportErrorStatus(
    Dem_EventIdType     EventId,
    Dem_EventStatusType EventStatus);

/**
 * @brief  Read the ISO 14229-1 §11.2.1 composite UDS status byte of an event.
 *         Supports UDS service $19 (ReadDTCInformation) and host-native unit tests.
 * @param  EventId      [in]  DEM event identifier.
 * @param  UdsStatus    [out] Composite UDS statusOfDTC byte.
 * @return E_OK on success, E_NOT_OK if EventId unknown or UdsStatus is NULL.
 * @req    SWS_Dem_00915
 */
extern Std_ReturnType Dem_GetEventUdsStatus(
    Dem_EventIdType   EventId,
    uint8     * const UdsStatus);

#endif /* DEM_H */
