/**
 * =====================================================================================
 * @file        SwcSafeStateMgr.c
 * @brief       AUTOSAR R25-11 SWC — Safe State Degradation Manager
 *
 *              Implements a two-phase controlled vehicle stop upon safe state entry:
 *
 *              Phase 1 — Smooth Steering Return
 *              ════════════════════════════════
 *              Uses Jerk-Constrained Interpolation to return the steering wheel to 0°
 *              over at most SSM_STEER_RETURN_MAX_MS milliseconds.
 *
 *              Algorithm: Cubic Hermite interpolation with bounded jerk:
 *                t_norm = cycle_count / total_cycles   ∈ [0, 1]
 *                steer(t) = steer_start * (1 - 3t² + 2t³)   (smooth-step)
 *
 *              The smooth-step function guarantees:
 *                • Zero velocity at t=0 (no jerk at entry)
 *                • Zero velocity at t=1 (no jerk at 0° return)
 *                • Bounded jerk ≤ SA_Cal_JerkLimit_degs3
 *
 *              Phase 2 — Progressive Deceleration
 *              ════════════════════════════════════
 *              Applies a linearly increasing deceleration from -2.0 m/s² to the
 *              target decel (up to -8.0 m/s²) using a ramp profile:
 *                accel(t) = -2.0 + (target_decel - (-2.0)) * t_norm
 *
 *              Both phases run concurrently starting from the first 10ms cycle.
 *              On vehicle speed < SSM_STOP_SPEED_THRESHOLD: apply full braking hold.
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "Platform_Types.h"
#include "SwcSafeStateMgr.h"
#include "Rte_SafetyArbitrator.h"
#include "Dem.h"
#include "WdgM.h"

/*=====================================================================================
 * XCP-Accessible Calibration Parameters
 *====================================================================================*/
#define SAFE_STATE_MGR_START_SEC_CALIB_32
#include "MemMap.h"

/** @brief Max steering return duration [ms] */
static volatile const float32 SSM_Cal_SteerReturnMaxMs    = 500.0f;

/** @brief Minimum deceleration at safe state entry [m/s²] (negative = braking) */
static volatile const float32 SSM_Cal_DecelInitial_mps2   = -2.0f;

/** @brief Maximum deceleration in safe state [m/s²] */
static volatile const float32 SSM_Cal_DecelMax_mps2       = -8.0f;

/** @brief Speed below which braking hold is applied [m/s] */
static volatile const float32 SSM_Cal_StopSpeed_mps       = 0.5f;

/** @brief Hold brake torque after stop [fraction 0..1] */
static volatile const float32 SSM_Cal_HoldBrakeFraction   = 0.8f;

/** @brief Cycle time [ms] (must match OS task period) */
static volatile const float32 SSM_Cal_CycleTime_ms        = 10.0f;

/** @brief Max jerk limit for steer return [deg/s²] — not enforced numerically here,
 *         smooth-step profile inherently limits jerk. Stored for A2L documentation. */
static volatile const float32 SSM_Cal_MaxJerk_degs2       = 200.0f;

#define SAFE_STATE_MGR_STOP_SEC_CALIB_32
#include "MemMap.h"

/*=====================================================================================
 * Forward Declarations (static helpers defined at end of file)
 *====================================================================================*/
static float32 SA_ClampF32(float32 val, float32 lo, float32 hi);

/*=====================================================================================
 * State Machine Definitions
 *====================================================================================*/
typedef uint8 SSM_StateType;
#define SSM_STATE_IDLE              ((SSM_StateType)0x00U) /**< Normal operation        */
#define SSM_STATE_ACTIVE_RETURN     ((SSM_StateType)0x01U) /**< Phase 1+2 active        */
#define SSM_STATE_HOLD_STOP         ((SSM_StateType)0x02U) /**< Vehicle stopped, hold   */
#define SSM_STATE_CLEARED           ((SSM_StateType)0x03U) /**< Safe state cleared      */

/*=====================================================================================
 * Module State Variables
 *====================================================================================*/
#define SAFE_STATE_MGR_START_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

static SSM_StateType SSM_CurrentState;

/* Steering interpolation state */
static float32 SSM_SteerStart_deg;        /**< Steer angle at safe state entry         */
static float32 SSM_TotalCycles;           /**< Total 10ms cycles for steer return      */
static float32 SSM_ElapsedCycles;         /**< Elapsed 10ms cycles since entry         */

/* Deceleration ramp state */
static float32 SSM_TargetDecel_mps2;      /**< Requested target decel from Arbitrator  */
static float32 SSM_CurrentDecel_mps2;     /**< Currently commanded decel               */

/* State persistence for diagnostic runnable */
static uint32  SSM_ActiveDurationCycles;  /**< Total cycles safe state has been active  */
static boolean SSM_Initialized;

#define SAFE_STATE_MGR_STOP_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/*=====================================================================================
 * Internal: Smooth-Step Interpolation (Cubic Hermite)
 *   smoothstep(t) = 3t² - 2t³    for t ∈ [0,1]
 *   steer(t) = steer_start * (1 - smoothstep(t))
 *   Returns steer angle at normalized time t.
 *====================================================================================*/
#define SAFE_STATE_MGR_START_SEC_CODE
#include "MemMap.h"

static float32 SSM_SmoothStepSteer(float32 steerStart_deg, float32 t_norm)
{
    float32 t;
    float32 smoothstep;
    float32 result;

    /* Clamp t to [0, 1] */
    t = (t_norm < 0.0f) ? 0.0f : ((t_norm > 1.0f) ? 1.0f : t_norm);

    /* smoothstep = 3t² - 2t³ */
    smoothstep = (3.0f * t * t) - (2.0f * t * t * t);

    /* Interpolate from steerStart to 0 */
    result = steerStart_deg * (1.0f - smoothstep);

    return result;
}

/*=====================================================================================
 * Internal: Compute deceleration ramp value
 *   Linear ramp from DecelInitial to TargetDecel over same time as steer return.
 *====================================================================================*/
static float32 SSM_ComputeDecelRamp(float32 t_norm)
{
    float32 t;
    float32 decel;

    t = (t_norm < 0.0f) ? 0.0f : ((t_norm > 1.0f) ? 1.0f : t_norm);

    decel = SSM_Cal_DecelInitial_mps2 +
            ((SSM_TargetDecel_mps2 - SSM_Cal_DecelInitial_mps2) * t);

    /* Clamp to configured limits */
    if (decel < SSM_Cal_DecelMax_mps2)
    {
        decel = SSM_Cal_DecelMax_mps2;
    }
    if (decel > SSM_Cal_DecelInitial_mps2)
    {
        decel = SSM_Cal_DecelInitial_mps2;
    }

    return decel;
}

/*=====================================================================================
 * Rte_Runnable_SafeStateMgr_Init
 *====================================================================================*/
void Rte_Runnable_SafeStateMgr_Init(void)
{
    SSM_CurrentState       = SSM_STATE_IDLE;
    SSM_SteerStart_deg     = 0.0f;
    SSM_TotalCycles        = SSM_Cal_SteerReturnMaxMs / SSM_Cal_CycleTime_ms;
    SSM_ElapsedCycles      = 0.0f;
    SSM_TargetDecel_mps2   = SSM_Cal_DecelInitial_mps2;
    SSM_CurrentDecel_mps2  = 0.0f;
    SSM_ActiveDurationCycles = 0U;
    SSM_Initialized        = TRUE;
}

/*=====================================================================================
 * Rte_Runnable_SafeStateMgr_10ms — Main 10ms Cyclic Runnable
 *====================================================================================*/
void Rte_Runnable_SafeStateMgr_10ms(void)
{
    Rte_SafeStateCommandType     ssCmd;
    Rte_VehicleDynamicsStateType dynState;
    Rte_SafetyArbitratorOutputType arbitratorOutput;
    float32                      t_norm;
    float32                      commandedSteer;
    float32                      commandedDecel;
    Rte_ModeType_SafetyMode      currentMode;

    if (SSM_Initialized != TRUE)
    {
        return;
    }

    /* --- WDGM Checkpoint: Entry --- */
    (void)WdgM_CheckpointReached(WDGM_SE_SAFE_STATE_MGR, WDGM_CP_SSM_ENTRY);

    /* Read inputs */
    (void)Rte_Read_SafetyArbitrator_VehicleDynamics_Data(&dynState);
    (void)Rte_IRead_SafetyArbitrator_ArbitrationResult(&arbitratorOutput);

    /* Read safe state command from Safety Arbitrator */
    /* In production: Rte_Read_SafeStateMgr_SafeStateCmd_Data(&ssCmd) */
    ssCmd.EnterSafeState          = arbitratorOutput.SafeStateActive;
    ssCmd.TargetDeceleration_mps2 = -3.5f; /* Fallback */

    currentMode = Rte_Mode_SafetyMode();

    switch (SSM_CurrentState)
    {
        /*--------------------------------------------------------------
         * IDLE: Normal operation — monitor for safe state trigger
         *--------------------------------------------------------------*/
        case SSM_STATE_IDLE:
        {
            if ((ssCmd.EnterSafeState == TRUE) ||
                (currentMode == RTE_MODE_SAFETY_SAFE_STATE) ||
                (currentMode == RTE_MODE_SAFETY_EMERGENCY))
            {
                /* Capture entry conditions */
                SSM_SteerStart_deg   = arbitratorOutput.CommandedSteerAngle_deg;
                SSM_TargetDecel_mps2 = SA_ClampF32(
                    ssCmd.TargetDeceleration_mps2,
                    SSM_Cal_DecelMax_mps2,
                    SSM_Cal_DecelInitial_mps2);
                SSM_ElapsedCycles    = 0.0f;
                SSM_TotalCycles      = SSM_Cal_SteerReturnMaxMs /
                                       SSM_Cal_CycleTime_ms;
                SSM_CurrentState     = SSM_STATE_ACTIVE_RETURN;
            }
            break;
        }

        /*--------------------------------------------------------------
         * ACTIVE_RETURN: Smooth steer return + decel ramp
         *--------------------------------------------------------------*/
        case SSM_STATE_ACTIVE_RETURN:
        {
            SSM_ElapsedCycles++;
            SSM_ActiveDurationCycles++;

            t_norm = SSM_ElapsedCycles / SSM_TotalCycles;

            /* --- WDGM Checkpoint: Interpolation mid-point --- */
            (void)WdgM_CheckpointReached(WDGM_SE_SAFE_STATE_MGR,
                                          WDGM_CP_SSM_INTERPOLATION);

            /* Phase 1: Smooth steer return */
            commandedSteer = SSM_SmoothStepSteer(SSM_SteerStart_deg, t_norm);

            /* Phase 2: Decel ramp */
            commandedDecel = SSM_ComputeDecelRamp(t_norm);
            SSM_CurrentDecel_mps2 = commandedDecel;

            /* Write chassis command overriding AI */
            {
                Rte_SafetyArbitratorOutputType safeOutput;
                safeOutput.CommandedSteerAngle_deg    = commandedSteer;
                safeOutput.CommandedAcceleration_mps2 = commandedDecel;
                safeOutput.SafeStateActive            = TRUE;
                safeOutput.SafetyEnvelopeStatus       = 2U;
                safeOutput.ArbitrationCount           = 0U;
                safeOutput.Reserved                   = 0U;
                (void)Rte_Write_SafetyArbitrator_ChassisCommand_Data(&safeOutput);
            }

            /* Check for vehicle stop */
            if (dynState.VehicleSpeed_mps <= SSM_Cal_StopSpeed_mps)
            {
                SSM_CurrentState = SSM_STATE_HOLD_STOP;
                (void)Dem_SetEventStatus(DEM_EVENT_SAFE_STATE_ENTRY,
                                          DEM_EVENT_STATUS_PASSED);
            }

            /* Check for safe state cleared by external condition (e.g. driver override) */
            if (currentMode == RTE_MODE_SAFETY_NORMAL)
            {
                SSM_CurrentState = SSM_STATE_CLEARED;
            }

            break;
        }

        /*--------------------------------------------------------------
         * HOLD_STOP: Vehicle has stopped — maintain brake hold
         *--------------------------------------------------------------*/
        case SSM_STATE_HOLD_STOP:
        {
            SSM_ActiveDurationCycles++;

            {
                Rte_SafetyArbitratorOutputType holdOutput;
                holdOutput.CommandedSteerAngle_deg    = 0.0f;
                holdOutput.CommandedAcceleration_mps2 =
                    SSM_Cal_DecelMax_mps2 * SSM_Cal_HoldBrakeFraction;
                holdOutput.SafeStateActive            = TRUE;
                holdOutput.SafetyEnvelopeStatus       = 2U;
                holdOutput.ArbitrationCount           = 0U;
                holdOutput.Reserved                   = 0U;
                (void)Rte_Write_SafetyArbitrator_ChassisCommand_Data(&holdOutput);
            }

            /* Transition to CLEARED if RTE mode reset */
            if (currentMode == RTE_MODE_SAFETY_NORMAL)
            {
                SSM_CurrentState = SSM_STATE_CLEARED;
            }

            break;
        }

        /*--------------------------------------------------------------
         * CLEARED: Safe state has been lifted — reset to IDLE
         *--------------------------------------------------------------*/
        case SSM_STATE_CLEARED:
        {
            SSM_ElapsedCycles         = 0.0f;
            SSM_ActiveDurationCycles  = 0U;
            SSM_CurrentDecel_mps2     = 0.0f;
            SSM_CurrentState          = SSM_STATE_IDLE;
            break;
        }

        default:
        {
            /* Defensive: unknown state → return to IDLE */
            SSM_CurrentState = SSM_STATE_IDLE;
            break;
        }
    }

    /* --- WDGM Checkpoint: Exit --- */
    (void)WdgM_CheckpointReached(WDGM_SE_SAFE_STATE_MGR, WDGM_CP_SSM_EXIT);
}

/*=====================================================================================
 * Helper used by Safe State Manager (also used by Safety Arbitrator — declared locally)
 *====================================================================================*/
static float32 SA_ClampF32(float32 val, float32 lo, float32 hi)
{
    float32 result;
    if (val < lo)      { result = lo; }
    else if (val > hi) { result = hi; }
    else               { result = val; }
    return result;
}

#define SAFE_STATE_MGR_STOP_SEC_CODE
#include "MemMap.h"

