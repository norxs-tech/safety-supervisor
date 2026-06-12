/**
 * =====================================================================================
 * @file        WdgM.h
 * @brief       AUTOSAR R25-11 Watchdog Manager (WdgM) — Interface Header
 *              Implements Logical Execution Time (LET) monitoring via alive supervision,
 *              deadline supervision, and logical program flow supervision for ASIL-D.
 *              Interfaces with hardware watchdog (SWT on S32G) and DEM for fault logging.
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

#ifndef WDGM_H
#define WDGM_H

#include "Platform_Types.h"

/*=====================================================================================
 * WdgM Supervision Entity IDs
 *  Each supervised entity corresponds to one runnable or task.
 *====================================================================================*/
typedef uint16 WdgM_SupervisedEntityIdType;
#define WDGM_SE_SAFETY_ARBITRATOR     ((WdgM_SupervisedEntityIdType)0x0001U)
#define WDGM_SE_VEHICLE_DYNAMICS      ((WdgM_SupervisedEntityIdType)0x0002U)
#define WDGM_SE_SAFE_STATE_MGR        ((WdgM_SupervisedEntityIdType)0x0003U)
#define WDGM_SE_IPC_HANDLER           ((WdgM_SupervisedEntityIdType)0x0004U)

/*=====================================================================================
 * WdgM Checkpoint IDs
 *  Each checkpoint marks a specific program location within a supervised entity.
 *====================================================================================*/
typedef uint16 WdgM_CheckpointIdType;
/* Safety Arbitrator checkpoints */
#define WDGM_CP_SA_ENTRY              ((WdgM_CheckpointIdType)0x0001U) /**< Runnable entry  */
#define WDGM_CP_SA_ENVELOPE_CHECK     ((WdgM_CheckpointIdType)0x0002U) /**< After envelope  */
#define WDGM_CP_SA_EXIT               ((WdgM_CheckpointIdType)0x0003U) /**< Runnable exit   */

/* Vehicle Dynamics checkpoints */
#define WDGM_CP_VD_ENTRY              ((WdgM_CheckpointIdType)0x0011U)
#define WDGM_CP_VD_SENSOR_FUSION      ((WdgM_CheckpointIdType)0x0012U)
#define WDGM_CP_VD_EXIT               ((WdgM_CheckpointIdType)0x0013U)

/* Safe State Manager checkpoints */
#define WDGM_CP_SSM_ENTRY             ((WdgM_CheckpointIdType)0x0021U)
#define WDGM_CP_SSM_INTERPOLATION     ((WdgM_CheckpointIdType)0x0022U)
#define WDGM_CP_SSM_EXIT              ((WdgM_CheckpointIdType)0x0023U)

/* IPC handler checkpoints (scheduler-level supervised entity) */
#define WDGM_CP_IPC_RX                ((WdgM_CheckpointIdType)0x0031U)
#define WDGM_CP_IPC_TX                ((WdgM_CheckpointIdType)0x0032U)

/*=====================================================================================
 * WdgM Global Status
 *====================================================================================*/
typedef uint8 WdgM_GlobalStatusType;
#define WDGM_GLOBAL_STATUS_OK         ((WdgM_GlobalStatusType)0x00U)
#define WDGM_GLOBAL_STATUS_FAILED     ((WdgM_GlobalStatusType)0x01U)
#define WDGM_GLOBAL_STATUS_EXPIRED    ((WdgM_GlobalStatusType)0x02U)
#define WDGM_GLOBAL_STATUS_STOPPED    ((WdgM_GlobalStatusType)0x03U)
#define WDGM_GLOBAL_STATUS_DEACTIVATED ((WdgM_GlobalStatusType)0x04U)

/*=====================================================================================
 * WdgM Local Status (per supervised entity)
 *====================================================================================*/
typedef uint8 WdgM_LocalStatusType;
#define WDGM_LOCAL_STATUS_OK          ((WdgM_LocalStatusType)0x00U)
#define WDGM_LOCAL_STATUS_FAILED      ((WdgM_LocalStatusType)0x01U)
#define WDGM_LOCAL_STATUS_EXPIRED     ((WdgM_LocalStatusType)0x02U)
#define WDGM_LOCAL_STATUS_DEACTIVATED ((WdgM_LocalStatusType)0x03U)

/*=====================================================================================
 * Alive Supervision Configuration
 *====================================================================================*/
typedef struct
{
    uint32 ExpectedAliveIndications;  /**< Expected checkpoint calls per supervision window */
    uint32 MinMargin;                 /**< Minimum tolerance (indications below expected)   */
    uint32 MaxMargin;                 /**< Maximum tolerance (indications above expected)   */
    uint32 SupervisionCycle_ms;       /**< Supervision window duration in ms                */
} WdgM_AliveSupervisionType;

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Initialize Watchdog Manager. Starts hardware watchdog service.
 * @return E_OK on success.
 */
extern Std_ReturnType WdgM_Init(void);

/**
 * @brief  Main function — called every 5ms from OS task.
 *         Evaluates alive, deadline, and logical supervision.
 *         Triggers hardware watchdog refresh on global OK.
 *         Reports to DEM on supervision failure.
 */
extern void WdgM_MainFunction(void);

/**
 * @brief  Report a checkpoint reached event for a supervised entity.
 *         Central mechanism for LET monitoring per AUTOSAR R25-11 SWS_WdgM_00191.
 * @param  SEID   [in]  Supervised Entity ID.
 * @param  CPID   [in]  Checkpoint ID within entity.
 * @return E_OK if checkpoint accepted, E_NOT_OK if entity not active or params invalid.
 * @req    SWS_WdgM_00191
 */
extern Std_ReturnType WdgM_CheckpointReached(
    WdgM_SupervisedEntityIdType SEID,
    WdgM_CheckpointIdType       CPID);

/**
 * @brief  Get the current global status of the WdgM.
 * @param  Status [out] Current global status.
 * @return E_OK on success.
 */
extern Std_ReturnType WdgM_GetGlobalStatus(
    WdgM_GlobalStatusType * const Status);

/**
 * @brief  Get the local supervision status of a specific entity.
 * @param  SEID   [in]  Supervised Entity ID.
 * @param  Status [out] Local status of the entity.
 * @return E_OK on success, E_NOT_OK if SEID invalid.
 */
extern Std_ReturnType WdgM_GetLocalStatus(
    WdgM_SupervisedEntityIdType  SEID,
    WdgM_LocalStatusType        * const Status);

/**
 * @brief  Perform immediate hardware watchdog trigger (emergency path).
 *         Called by Safe State Manager during controlled shutdown to keep SWT alive.
 */
extern void WdgM_PerformReset(void);

#endif /* WDGM_H */
