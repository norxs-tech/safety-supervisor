/**
 * =====================================================================================
 * @file        Dem.c
 * @brief       AUTOSAR R25-11 Diagnostic Event Manager (DEM) — Implementation
 *              Manages DTC confirmation counters, freeze frame storage, and NvM
 *              write-back for ASIL-D fault persistence across ECU power cycles.
 *
 *              DTC Confirmation Model (ISO 14229-1 §11.2):
 *              ─────────────────────────────────────────────────────────────────────
 *              PREFAILED × DEM_FAULT_CONFIRM_THRESHOLD → FAILED (confirmed DTC)
 *              PASSED    × DEM_PASS_CONFIRM_THRESHOLD  → event cleared
 *              ─────────────────────────────────────────────────────────────────────
 *
 *              NvM Integration:
 *              Each confirmed DTC is written to NvM block DEM_NVM_BLOCK_ID.
 *              Freeze frame data captured at first FAILED status per DTC.
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_DiagnosticEventManager
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "Dem.h"

/*=====================================================================================
 * DEM Configuration Constants
 *====================================================================================*/
#define DEM_MAX_EVENTS              (16U) /**< Total registered DTC events              */
#define DEM_FAULT_CONFIRM_THRESHOLD (2U)  /**< PREFAILED cycles before FAILED confirm   */
#define DEM_PASS_CONFIRM_THRESHOLD  (3U)  /**< PASSED cycles before event cleared       */
#define DEM_NVM_BLOCK_ID            (0x0010U) /**< NvM block ID for DTC storage         */
#define DEM_MAX_FREEZE_FRAMES       (8U)  /**< Max concurrent freeze frames stored      */

/*=====================================================================================
 * DEM Event Status Byte (ISO 14229-1 §11.2.1 bit-field mapping)
 *====================================================================================*/
#define DEM_UDS_STATUS_TF           (0x01U) /**< testFailed                             */
#define DEM_UDS_STATUS_TFTOC        (0x02U) /**< testFailedThisOperationCycle           */
#define DEM_UDS_STATUS_PDTC         (0x04U) /**< pendingDTC                             */
#define DEM_UDS_STATUS_CDTC         (0x08U) /**< confirmedDTC                           */
#define DEM_UDS_STATUS_TNCSLC       (0x10U) /**< testNotCompletedSinceLastClear         */
#define DEM_UDS_STATUS_TFSLC        (0x20U) /**< testFailedSinceLastClear               */
#define DEM_UDS_STATUS_TNCTOC       (0x40U) /**< testNotCompletedThisOperationCycle     */
#define DEM_UDS_STATUS_WIR          (0x80U) /**< warningIndicatorRequested              */

/*=====================================================================================
 * Internal DEM Event Table Entry
 *====================================================================================*/
typedef struct
{
    Dem_EventIdType     EventId;
    Dem_EventStatusType CurrentStatus;         /**< Raw API status (PASSED/FAILED/etc.) */
    uint8               UdsStatusByte;         /**< ISO 14229-1 §11.2 composite status  */
    uint8               FailedCount;           /**< Consecutive FAILED/PREFAILED cycles  */
    uint8               PassedCount;           /**< Consecutive PASSED cycles            */
    boolean             FreezeFrameCaptured;   /**< TRUE if freeze frame already stored  */
    boolean             NvmWritePending;       /**< TRUE if NvM write not yet flushed    */
} Dem_EventTableEntry;

/*=====================================================================================
 * Internal Freeze Frame Storage
 *====================================================================================*/
typedef struct
{
    Dem_EventIdType      EventId;
    boolean              Valid;
    Dem_FreezeFrameType  Data;
} Dem_FreezeFrameStorage;

/*=====================================================================================
 * Module State
 *====================================================================================*/
#define DEM_START_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/* Known event IDs registered in DEM — order matches DEM_MAX_EVENTS */
static Dem_EventTableEntry Dem_EventTable[DEM_MAX_EVENTS] =
{
    { DEM_EVENT_SAFETY_ENVELOPE_VIOLATED,    DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED,  DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_SAFE_STATE_ENTRY,            DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_SAFE_STATE_TIMEOUT,          DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_WHEEL_SPEED_DELTA_HIGH,      DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_YAW_RATE_IMPLAUSIBLE,        DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_ROAD_MU_LOW_CONFIDENCE,      DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_SENSOR_REDUNDANCY_FAIL,      DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_IPC_E2E_ERROR,               DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_IPC_BUFFER_OVERFLOW,         DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_IPC_FRAME_TIMEOUT,           DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_OTA_ROLLBACK_TRIGGERED,      DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_WDGM_ALIVE_SUPERVISION_FAIL, DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_WDGM_DEADLINE_FAIL,          DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_CSM_MAC_VERIFICATION_FAIL,   DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
    { DEM_EVENT_CSM_KEY_EXPIRED,             DEM_EVENT_STATUS_PASSED, 0U, 0U, 0U, FALSE, FALSE },
};

static Dem_FreezeFrameStorage Dem_FreezeFrames[DEM_MAX_FREEZE_FRAMES];
static boolean                Dem_Initialized = FALSE;

#define DEM_STOP_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/*=====================================================================================
 * Internal: Find event table entry by EventId
 *====================================================================================*/
#define DEM_START_SEC_CODE
#include "MemMap.h"

static Dem_EventTableEntry * Dem_FindEvent(Dem_EventIdType EventId)
{
    Dem_EventTableEntry * found = NULL_PTR;
    uint8 i;

    for (i = 0U; i < DEM_MAX_EVENTS; i++)
    {
        if (Dem_EventTable[i].EventId == EventId)
        {
            found = &Dem_EventTable[i];
            break;
        }
    }

    return found;
}

/*=====================================================================================
 * Internal: Find free freeze frame slot
 *====================================================================================*/
static Dem_FreezeFrameStorage * Dem_FindFreezeFrameSlot(Dem_EventIdType EventId)
{
    Dem_FreezeFrameStorage * result = NULL_PTR;
    uint8 i;

    /* Check if already stored for this event */
    for (i = 0U; i < DEM_MAX_FREEZE_FRAMES; i++)
    {
        if ((Dem_FreezeFrames[i].Valid == TRUE) &&
            (Dem_FreezeFrames[i].EventId == EventId))
        {
            result = &Dem_FreezeFrames[i]; /* Update existing */
            break;
        }
    }

    if (result == NULL_PTR)
    {
        /* Find empty slot */
        for (i = 0U; i < DEM_MAX_FREEZE_FRAMES; i++)
        {
            if (Dem_FreezeFrames[i].Valid == FALSE)
            {
                result = &Dem_FreezeFrames[i];
                break;
            }
        }
    }

    return result;
}

/*=====================================================================================
 * Internal: NvM write stub
 *  In production: calls NvM_WriteBlock(DEM_NVM_BLOCK_ID, &Dem_EventTable)
 *  The NvM module queues the write and flushes on next NvM_MainFunction cycle.
 *====================================================================================*/
static void Dem_RequestNvmWrite(void)
{
    /* NvM_WriteBlock(DEM_NVM_BLOCK_ID, (const uint8 *)&Dem_EventTable); */
    /* Stub: actual NvM integration via AUTOSAR NvM MCAL driver */
}

/*=====================================================================================
 * Dem_SetEventStatus
 *====================================================================================*/
Std_ReturnType Dem_SetEventStatus(
    Dem_EventIdType     EventId,
    Dem_EventStatusType EventStatus)
{
    Std_ReturnType       retVal = E_NOT_OK;
    Dem_EventTableEntry *entry;

    if (Dem_Initialized == FALSE)
    {
        /* Init on first call (lazy init for ASIL-D early boot) */
        uint8 i;
        for (i = 0U; i < DEM_MAX_FREEZE_FRAMES; i++)
        {
            Dem_FreezeFrames[i].Valid   = FALSE;
            Dem_FreezeFrames[i].EventId = 0U;
        }
        Dem_Initialized = TRUE;
    }

    entry = Dem_FindEvent(EventId);

    if (entry != NULL_PTR)
    {
        entry->CurrentStatus = EventStatus;

        switch (EventStatus)
        {
            case DEM_EVENT_STATUS_FAILED:
            {
                entry->FailedCount++;
                entry->PassedCount  = 0U;

                /* Set testFailed and testFailedSinceLastClear bits */
                entry->UdsStatusByte |= (uint8)(DEM_UDS_STATUS_TF |
                                                DEM_UDS_STATUS_TFTOC |
                                                DEM_UDS_STATUS_TFSLC);

                /* Confirm DTC immediately on FAILED (ASIL-D — no debounce for safety) */
                entry->UdsStatusByte |= DEM_UDS_STATUS_CDTC;
                entry->UdsStatusByte |= DEM_UDS_STATUS_PDTC;
                entry->NvmWritePending = TRUE;
                Dem_RequestNvmWrite();
                break;
            }

            case DEM_EVENT_STATUS_PREFAILED:
            {
                entry->FailedCount++;
                entry->PassedCount  = 0U;
                entry->UdsStatusByte |= DEM_UDS_STATUS_PDTC;

                if (entry->FailedCount >= DEM_FAULT_CONFIRM_THRESHOLD)
                {
                    entry->UdsStatusByte |= (uint8)(DEM_UDS_STATUS_TF |
                                                    DEM_UDS_STATUS_CDTC |
                                                    DEM_UDS_STATUS_TFSLC);
                    entry->NvmWritePending = TRUE;
                    Dem_RequestNvmWrite();
                }
                break;
            }

            case DEM_EVENT_STATUS_PASSED:
            {
                entry->PassedCount++;
                entry->FailedCount  = 0U;

                /* Clear testFailed bit — event healed */
                entry->UdsStatusByte &= (uint8)(~DEM_UDS_STATUS_TF);
                entry->UdsStatusByte &= (uint8)(~DEM_UDS_STATUS_TFTOC);
                entry->UdsStatusByte &= (uint8)(~DEM_UDS_STATUS_PDTC);

                if (entry->PassedCount >= DEM_PASS_CONFIRM_THRESHOLD)
                {
                    /* confirmedDTC stays set until UDS $14 ClearDTC */
                    entry->FreezeFrameCaptured = FALSE;
                }
                break;
            }

            case DEM_EVENT_STATUS_PREPASSED:
            {
                entry->PassedCount++;
                break;
            }

            default:
            {
                retVal = E_NOT_OK;
                break;
            }
        }

        retVal = E_OK;
    }

    return retVal;
}

/*=====================================================================================
 * Dem_SetFreezeFrameData
 *====================================================================================*/
Std_ReturnType Dem_SetFreezeFrameData(
    Dem_EventIdType           EventId,
    const Dem_FreezeFrameType * const FreezeData)
{
    Std_ReturnType          retVal = E_NOT_OK;
    Dem_EventTableEntry    *entry;
    Dem_FreezeFrameStorage *slot;
    uint8                   byteIdx;
    const uint8            *srcBytes;
    uint8                  *dstBytes;

    if (FreezeData == NULL_PTR)
    {
        retVal = E_NOT_OK;
    }
    else
    {
        entry = Dem_FindEvent(EventId);

        if ((entry != NULL_PTR) && (entry->FreezeFrameCaptured == FALSE))
        {
            slot = Dem_FindFreezeFrameSlot(EventId);

            if (slot != NULL_PTR)
            {
                /* Byte-by-byte copy (MISRA C:2023 — no memcpy on safety-critical data) */
                srcBytes = (const uint8 *)FreezeData;
                dstBytes = (uint8 *)&slot->Data;
                for (byteIdx = 0U; byteIdx < (uint8)sizeof(Dem_FreezeFrameType); byteIdx++)
                {
                    dstBytes[byteIdx] = srcBytes[byteIdx];
                }

                slot->EventId            = EventId;
                slot->Valid              = TRUE;
                entry->FreezeFrameCaptured = TRUE;
                entry->NvmWritePending   = TRUE;
                Dem_RequestNvmWrite();

                retVal = E_OK;
            }
        }
        else if (entry != NULL_PTR)
        {
            retVal = E_OK; /* Freeze frame already stored — silently OK */
        }
        else
        {
            retVal = E_NOT_OK;
        }
    }

    return retVal;
}

/*=====================================================================================
 * Dem_ReportErrorStatus
 *====================================================================================*/
Std_ReturnType Dem_ReportErrorStatus(
    Dem_EventIdType     EventId,
    Dem_EventStatusType EventStatus)
{
    /* ReportErrorStatus is a thin wrapper per AUTOSAR R25-11 SWS_Dem_00589 */
    return Dem_SetEventStatus(EventId, EventStatus);
}

#define DEM_STOP_SEC_CODE
#include "MemMap.h"
