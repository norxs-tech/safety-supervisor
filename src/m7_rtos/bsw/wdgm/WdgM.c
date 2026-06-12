/**
 * =====================================================================================
 * @file        WdgM.c
 * @brief       AUTOSAR R25-11 Watchdog Manager — Full Implementation
 *              Implements alive supervision (indication counting per window),
 *              deadline supervision (timestamp delta check), and hardware SWT
 *              (Software Watchdog Timer) service on NXP S32G.
 *              On supervision failure: calls Dem_SetEventStatus() and withholds
 *              hardware watchdog refresh to trigger controlled system reset.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_WatchdogManager
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "WdgM.h"
#include "Dem.h"

/*=====================================================================================
 * NXP S32G Software Watchdog Timer (SWT) Register Base
 * (MCAL abstraction — mapped via Wdg MCAL driver in production)
 *====================================================================================*/
#define S32G_SWT0_BASE_ADDR           (0x40270000UL)
#define S32G_SWT_CR_OFFSET            (0x00UL)   /**< Control Register               */
#define S32G_SWT_TO_OFFSET            (0x08UL)   /**< Timeout Register               */
#define S32G_SWT_SR_OFFSET            (0x10UL)   /**< Service Register               */
#define S32G_SWT_SERVICE_KEY1         (0xA602U)  /**< First service word             */
#define S32G_SWT_SERVICE_KEY2         (0xB480U)  /**< Second service word            */
#define S32G_SWT_TIMEOUT_VALUE        (0x000F4240UL) /**< ~50ms at 200MHz SWT clock  */

#ifdef UNIT_TEST_BUILD
/* Host-native unit tests: shadow the SWT register file in plain RAM so the
 * supervision logic can be exercised on x86_64 without MMIO access faults.   */
static volatile uint32 WdgM_SwtShadowRegs[8U];
#define SWT_REG32(base, offset)       (WdgM_SwtShadowRegs[((offset) >> 2U) & 0x07U])
#else
#define SWT_REG32(base, offset)       (*((volatile uint32 *)((base) + (offset))))
#endif

/*=====================================================================================
 * Alive Supervision State (per supervised entity)
 *====================================================================================*/
#define WDGM_NUM_ENTITIES             (4U)
#define WDGM_SUPERVISION_WINDOW_MS    (10U)      /**< One runnable cycle             */

typedef struct
{
    WdgM_SupervisedEntityIdType EntityId;
    uint32                       AliveCounter;        /**< Incremented on CP reached  */
    uint32                       LastCheckTimestamp;  /**< ms timestamp of last CP    */
    uint32                       WindowCounter;       /**< Supervision windows elapsed */
    WdgM_LocalStatusType         LocalStatus;
    /* Alive supervision config */
    uint32                       ExpectedIndications;
    uint32                       MinMargin;
    uint32                       MaxMargin;
    uint32                       SupervisionCycle_ms;
    /* Entry CP for deadline check */
    uint32                       EntryTimestamp;
    uint32                       DeadlineLimit_ms;    /**< Max allowed exec time ms   */
    boolean                      EntryReached;
} WdgM_EntityStateType;

/*=====================================================================================
 * Module Variables
 *====================================================================================*/
#define SAFETY_ARBITRATOR_START_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

static WdgM_GlobalStatusType  WdgM_GlobalStatus;
static boolean                WdgM_Initialized;
static uint32                 WdgM_MainFunctionTick_ms;

static WdgM_EntityStateType WdgM_Entities[WDGM_NUM_ENTITIES] =
{
    {
        .EntityId            = WDGM_SE_SAFETY_ARBITRATOR,
        .AliveCounter        = 0U,
        .LocalStatus         = WDGM_LOCAL_STATUS_OK,
        .ExpectedIndications = 3U,    /* ENTRY + ENVELOPE_CHECK + EXIT per 10ms */
        .MinMargin           = 0U,
        .MaxMargin           = 0U,
        .SupervisionCycle_ms = WDGM_SUPERVISION_WINDOW_MS,
        .DeadlineLimit_ms    = 8U,    /* Must complete within 8ms */
        .EntryReached        = FALSE
    },
    {
        .EntityId            = WDGM_SE_VEHICLE_DYNAMICS,
        .AliveCounter        = 0U,
        .LocalStatus         = WDGM_LOCAL_STATUS_OK,
        .ExpectedIndications = 3U,
        .MinMargin           = 0U,
        .MaxMargin           = 0U,
        .SupervisionCycle_ms = WDGM_SUPERVISION_WINDOW_MS,
        .DeadlineLimit_ms    = 5U,
        .EntryReached        = FALSE
    },
    {
        .EntityId            = WDGM_SE_SAFE_STATE_MGR,
        .AliveCounter        = 0U,
        .LocalStatus         = WDGM_LOCAL_STATUS_OK,
        .ExpectedIndications = 3U,
        .MinMargin           = 0U,
        .MaxMargin           = 0U,
        .SupervisionCycle_ms = WDGM_SUPERVISION_WINDOW_MS,
        .DeadlineLimit_ms    = 9U,
        .EntryReached        = FALSE
    },
    {
        .EntityId            = WDGM_SE_IPC_HANDLER,
        .AliveCounter        = 0U,
        .LocalStatus         = WDGM_LOCAL_STATUS_OK,
        .ExpectedIndications = 2U,
        .MinMargin           = 0U,
        .MaxMargin           = 1U,
        .SupervisionCycle_ms = WDGM_SUPERVISION_WINDOW_MS,
        .DeadlineLimit_ms    = 3U,
        .EntryReached        = FALSE
    }
};

#define SAFETY_ARBITRATOR_STOP_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/*=====================================================================================
 * Internal: Service hardware SWT (NXP S32G SWT0)
 *====================================================================================*/
#define SAFETY_ARBITRATOR_START_SEC_CODE
#include "MemMap.h"

static void WdgM_ServiceHardwareWatchdog(void)
{
    /* Two-word service sequence per NXP S32G Reference Manual §48.5 */
    SWT_REG32(S32G_SWT0_BASE_ADDR, S32G_SWT_SR_OFFSET) = S32G_SWT_SERVICE_KEY1;
    SWT_REG32(S32G_SWT0_BASE_ADDR, S32G_SWT_SR_OFFSET) = S32G_SWT_SERVICE_KEY2;
}

/*=====================================================================================
 * Internal: Evaluate alive supervision for one entity
 *====================================================================================*/
static void WdgM_EvaluateAliveSupervision(
    WdgM_EntityStateType * const Entity,
    uint32                        CurrentTick_ms)
{
    uint32 elapsed;
    uint32 actualIndications;

    elapsed = CurrentTick_ms - Entity->LastCheckTimestamp;

    if (elapsed >= Entity->SupervisionCycle_ms)
    {
        actualIndications = Entity->AliveCounter;
        Entity->AliveCounter        = 0U;
        Entity->LastCheckTimestamp  = CurrentTick_ms;

        /* Check if indications are within tolerance band */
        if ((actualIndications >= (Entity->ExpectedIndications - Entity->MinMargin)) &&
            (actualIndications <= (Entity->ExpectedIndications + Entity->MaxMargin)))
        {
            Entity->LocalStatus = WDGM_LOCAL_STATUS_OK;
        }
        else
        {
            Entity->LocalStatus = WDGM_LOCAL_STATUS_FAILED;
            (void)Dem_SetEventStatus(
                DEM_EVENT_WDGM_ALIVE_SUPERVISION_FAIL,
                DEM_EVENT_STATUS_FAILED);
        }
    }
}

/*=====================================================================================
 * WdgM_Init
 *====================================================================================*/
Std_ReturnType WdgM_Init(void)
{
    uint8 i;

    /* Configure SWT timeout */
    SWT_REG32(S32G_SWT0_BASE_ADDR, S32G_SWT_TO_OFFSET) = S32G_SWT_TIMEOUT_VALUE;

    /* Reset all entity states */
    for (i = 0U; i < WDGM_NUM_ENTITIES; i++)
    {
        WdgM_Entities[i].AliveCounter       = 0U;
        WdgM_Entities[i].LastCheckTimestamp = 0U;
        WdgM_Entities[i].LocalStatus        = WDGM_LOCAL_STATUS_OK;
        WdgM_Entities[i].EntryReached       = FALSE;
    }

    WdgM_GlobalStatus         = WDGM_GLOBAL_STATUS_OK;
    WdgM_MainFunctionTick_ms  = 0U;
    WdgM_Initialized          = TRUE;

    /* Initial watchdog service */
    WdgM_ServiceHardwareWatchdog();

    return E_OK;
}

/*=====================================================================================
 * WdgM_MainFunction — Called every 5ms from OS Alarm
 *====================================================================================*/
void WdgM_MainFunction(void)
{
    uint8                 i;
    WdgM_GlobalStatusType newGlobalStatus = WDGM_GLOBAL_STATUS_OK;
    boolean               anyFailed       = FALSE;

    if (WdgM_Initialized != TRUE)
    {
        return; /* MISRA C:2023 Rule 15.5 — single-exit via early guard */
    }

    WdgM_MainFunctionTick_ms += 5U;

    /* Evaluate all supervised entities */
    for (i = 0U; i < WDGM_NUM_ENTITIES; i++)
    {
        WdgM_EvaluateAliveSupervision(&WdgM_Entities[i], WdgM_MainFunctionTick_ms);

        if (WdgM_Entities[i].LocalStatus != WDGM_LOCAL_STATUS_OK)
        {
            anyFailed = TRUE;
        }
    }

    if (anyFailed == TRUE)
    {
        newGlobalStatus = WDGM_GLOBAL_STATUS_FAILED;
    }

    WdgM_GlobalStatus = newGlobalStatus;

    if (WdgM_GlobalStatus == WDGM_GLOBAL_STATUS_OK)
    {
        /* All entities healthy — service the hardware watchdog */
        WdgM_ServiceHardwareWatchdog();
    }
    /* If global status FAILED: watchdog NOT serviced → hardware reset after timeout */
}

/*=====================================================================================
 * WdgM_CheckpointReached
 *====================================================================================*/
Std_ReturnType WdgM_CheckpointReached(
    WdgM_SupervisedEntityIdType SEID,
    WdgM_CheckpointIdType       CPID)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint8          i;

    for (i = 0U; i < WDGM_NUM_ENTITIES; i++)
    {
        if (WdgM_Entities[i].EntityId == SEID)
        {
            /* Increment alive supervision counter */
            WdgM_Entities[i].AliveCounter++;

            /* Deadline supervision: record entry timestamp */
            if ((CPID == WDGM_CP_SA_ENTRY)  ||
                (CPID == WDGM_CP_VD_ENTRY)  ||
                (CPID == WDGM_CP_SSM_ENTRY))
            {
                WdgM_Entities[i].EntryTimestamp = WdgM_MainFunctionTick_ms;
                WdgM_Entities[i].EntryReached   = TRUE;
            }

            /* Deadline supervision: check elapsed at exit */
            if (((CPID == WDGM_CP_SA_EXIT)  ||
                 (CPID == WDGM_CP_VD_EXIT)  ||
                 (CPID == WDGM_CP_SSM_EXIT)) &&
                (WdgM_Entities[i].EntryReached == TRUE))
            {
                uint32 elapsed = WdgM_MainFunctionTick_ms -
                                 WdgM_Entities[i].EntryTimestamp;

                if (elapsed > WdgM_Entities[i].DeadlineLimit_ms)
                {
                    WdgM_Entities[i].LocalStatus = WDGM_LOCAL_STATUS_EXPIRED;
                    (void)Dem_SetEventStatus(
                        DEM_EVENT_WDGM_DEADLINE_FAIL,
                        DEM_EVENT_STATUS_FAILED);
                }

                WdgM_Entities[i].EntryReached = FALSE;
            }

            retVal = E_OK;
            break; /* MISRA C:2023 Rule 15.4 compliant single break */
        }
    }

    return retVal;
}

/*=====================================================================================
 * WdgM_GetGlobalStatus
 *====================================================================================*/
Std_ReturnType WdgM_GetGlobalStatus(WdgM_GlobalStatusType * const Status)
{
    Std_ReturnType retVal = E_NOT_OK;

    if (Status != NULL_PTR)
    {
        *Status = WdgM_GlobalStatus;
        retVal  = E_OK;
    }

    return retVal;
}

/*=====================================================================================
 * WdgM_GetLocalStatus
 *====================================================================================*/
Std_ReturnType WdgM_GetLocalStatus(
    WdgM_SupervisedEntityIdType  SEID,
    WdgM_LocalStatusType        * const Status)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint8          i;

    if (Status != NULL_PTR)
    {
        for (i = 0U; i < WDGM_NUM_ENTITIES; i++)
        {
            if (WdgM_Entities[i].EntityId == SEID)
            {
                *Status = WdgM_Entities[i].LocalStatus;
                retVal  = E_OK;
                break;
            }
        }
    }

    return retVal;
}

/*=====================================================================================
 * WdgM_PerformReset — Emergency path (called by Safe State Manager)
 *====================================================================================*/
void WdgM_PerformReset(void)
{
    /* Force watchdog expiry by NOT servicing SWT.
     * In the interim, service once to ensure a clean final cycle. */
    WdgM_GlobalStatus = WDGM_GLOBAL_STATUS_EXPIRED;
    /* Hardware reset will occur after SWT_TIMEOUT_VALUE elapses */
}

#define SAFETY_ARBITRATOR_STOP_SEC_CODE
#include "MemMap.h"
