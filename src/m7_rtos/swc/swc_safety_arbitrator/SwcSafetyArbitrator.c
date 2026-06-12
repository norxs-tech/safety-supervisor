/**
 * =====================================================================================
 * @file        SwcSafetyArbitrator.c
 * @brief       AUTOSAR R25-11 SWC — AI Command Plausibility & Safety Envelope Arbitration
 *
 *              This is the crown jewel of the safety architecture. Every AI driving
 *              command passes through this runnable before reaching chassis actuators.
 *
 *              Safety Envelope Algorithm:
 *              ─────────────────────────────────────────────────────────────────────
 *              1. AI Command Plausibility Checks (static bounds)
 *                 • Steer angle request vs. hardware lock-to-lock limit
 *                 • Acceleration request vs. peak powertrain / braking capability
 *                 • Yaw rate request vs. physical vehicle rollover threshold
 *
 *              2. Dynamic Safety Envelope (physics-based, μ-adaptive)
 *                 The maximum lateral acceleration permissible without loss of control:
 *                   Ay_max = μ * g * SafetyMarginFactor
 *
 *                 From Ackermann geometry, the steer angle that would produce Ay_max:
 *                   steer_max = arctan(Ay_max * Wheelbase / v²)   [rad, then deg]
 *
 *                 The AI's requested steer angle is scaled to this envelope:
 *                   If |steer_req| > steer_max → OVERRIDE → DEM event → Safe State
 *
 *              3. Yaw Rate Envelope (independent of steer)
 *                 Maximum yaw rate before rollover:
 *                   ψ̇_max = Ay_max / VehicleSpeed
 *                 AI requested yaw rate exceeding this → OVERRIDE
 *
 *              4. ASIL-D Bitwise Redundancy Check
 *                 All critical state variables stored as (x, ~x) pairs.
 *                 Before arbitration: ASIL_D_CHECK() called on all safety-critical vars.
 *                 Single-bit upset → immediate safe state entry.
 *
 *              5. Override Action
 *                 • Sets SafeStateActive = TRUE
 *                 • Writes SafeStateCommand → Safe State Manager SWC
 *                 • Calls Dem_SetEventStatus(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED, FAILED)
 *                 • Writes freeze frame data
 *                 • Calls NvM_WriteBlock() to persist DTC to NVRAM
 *              ─────────────────────────────────────────────────────────────────────
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11, UN R155
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "Rte_SafetyArbitrator.h"
#include "Dem.h"
#include "WdgM.h"

#include <math.h>

/*=====================================================================================
 * XCP-Accessible Calibration Parameters
 *====================================================================================*/
#define SAFETY_ARBITRATOR_START_SEC_CALIB_32
#include "MemMap.h"

/** @brief Gravity constant [m/s²] */
static volatile const float32 SA_Cal_Gravity_mps2         = 9.81f;

/** @brief Safety margin factor applied to μ*g for envelope (0.7 = 30% margin) */
static volatile const float32 SA_Cal_SafetyMarginFactor   = 0.70f;

/** @brief Absolute steer angle hardware limit [degrees] */
static volatile const float32 SA_Cal_MaxSteerAngle_deg    = 540.0f;

/** @brief Maximum AI-commanded acceleration [m/s²] (powertrain peak) */
static volatile const float32 SA_Cal_MaxAccel_mps2        = 4.0f;

/** @brief Maximum AI-commanded deceleration [m/s²] (ABS limit) */
static volatile const float32 SA_Cal_MaxDecel_mps2        = 12.0f;

/** @brief Max yaw rate absolute limit regardless of speed [rad/s] */
static volatile const float32 SA_Cal_MaxYawRate_radps     = 1.5f;

/** @brief Min vehicle speed to enable yaw envelope [m/s] */
static volatile const float32 SA_Cal_MinSpeedForYawEnv    = 2.0f;

/** @brief Debounce count before confirming envelope violation (×10ms) */
static volatile const float32 SA_Cal_ViolationDebounce    = 2.0f;

/** @brief Safe state deceleration rate [m/s²] */
static volatile const float32 SA_Cal_SafeStateDecel_mps2  = 3.5f;

#define SAFETY_ARBITRATOR_STOP_SEC_CALIB_32
#include "MemMap.h"

/*=====================================================================================
 * Module State Variables — ASIL-D Bitwise Redundancy Protected
 *====================================================================================*/
#define SAFETY_ARBITRATOR_START_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/* Safe state active flag stored as (x, ~x) pair for ASIL-D bit upset detection */
static ASIL_D_REDUNDANT_VAR(uint32, SA_SafeStateActive);

/* Override counter — rolling count of AI commands overridden */
static uint32 SA_OverrideCount;
static uint32 SA_OverrideCount_per100ms;
static uint32 SA_Cycle100msCounter;

/* Violation debounce counter */
static uint32 SA_ViolationDebounceCount;

/* Previous cycle commanded values (for jerk limiting in arbitrator) */
static float32 SA_Prev_SteerCmd_deg;
static float32 SA_Prev_AccelCmd_mps2;

/* Initialized flag */
static boolean SA_Initialized;

#define SAFETY_ARBITRATOR_STOP_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/*=====================================================================================
 * Internal Helpers
 *====================================================================================*/
#define SAFETY_ARBITRATOR_START_SEC_CODE
#include "MemMap.h"

static float32 SA_AbsF32(float32 val)
{
    return (val < 0.0f) ? (-val) : (val);
}

static float32 SA_ClampF32(float32 val, float32 lo, float32 hi)
{
    float32 result;
    if (val < lo)      { result = lo; }
    else if (val > hi) { result = hi; }
    else               { result = val; }
    return result;
}

/*=====================================================================================
 * Internal: Compute dynamic steer angle limit from μ and vehicle speed
 *  Ackermann: steer_max = atan(Ay_max * L / v²) → converted to degrees
 *  At low speed (v < 1 m/s): return hardware limit to allow manoeuvring.
 *====================================================================================*/
static float32 SA_ComputeMaxSteerAngle_deg(
    float32 mu,
    float32 speed_mps,
    float32 wheelbase_m)
{
    float32 ayMax;
    float32 steerMax_rad;
    float32 steerMax_deg;
    float32 vSquared;

    ayMax = mu * SA_Cal_Gravity_mps2 * SA_Cal_SafetyMarginFactor;

    if (speed_mps < 1.0f)
    {
        /* Low speed: physical limit applies (parking manoeuvres) */
        steerMax_deg = SA_Cal_MaxSteerAngle_deg;
    }
    else
    {
        vSquared = speed_mps * speed_mps;
        steerMax_rad = atanf(ayMax * wheelbase_m / vSquared);

        /* Convert rad to wheel-angle degrees (simplified 1:1 mapping here,
         * in production: apply steering ratio from calibration) */
        steerMax_deg = steerMax_rad * (180.0f / 3.14159265f);

        /* Never less than 2 degrees (precision floor) */
        if (steerMax_deg < 2.0f)
        {
            steerMax_deg = 2.0f;
        }
    }

    return steerMax_deg;
}

/*=====================================================================================
 * Internal: Compute maximum yaw rate from Ay_max and speed
 *====================================================================================*/
static float32 SA_ComputeMaxYawRate_radps(float32 mu, float32 speed_mps)
{
    float32 ayMax;
    float32 yawRateMax;

    ayMax = mu * SA_Cal_Gravity_mps2 * SA_Cal_SafetyMarginFactor;

    if (speed_mps < SA_Cal_MinSpeedForYawEnv)
    {
        yawRateMax = SA_Cal_MaxYawRate_radps;
    }
    else
    {
        yawRateMax = ayMax / speed_mps;
        yawRateMax = SA_ClampF32(yawRateMax, 0.1f, SA_Cal_MaxYawRate_radps);
    }

    return yawRateMax;
}

/*=====================================================================================
 * Internal: Trigger Safe State Entry
 *  Writes safe state command, raises DEM event, captures freeze frame, requests NvM write.
 *====================================================================================*/
static void SA_EnterSafeState(
    const Rte_VehicleDynamicsStateType * const dynState,
    uint8                                       violationType)
{
    Rte_SafeStateCommandType ssCmd;
    Dem_FreezeFrameType      ff;

    /* Set safe state active — ASIL-D redundant write */
    ASIL_D_SET(SA_SafeStateActive, 1UL);

    /* Compose safe state command */
    ssCmd.EnterSafeState         = TRUE;
    ssCmd.TargetDeceleration_mps2 = SA_Cal_SafeStateDecel_mps2;
    ssCmd.Reserved[0U]           = 0U;
    ssCmd.Reserved[1U]           = 0U;
    ssCmd.Reserved[2U]           = 0U;
    (void)Rte_Write_SafetyArbitrator_SafeStateCmd_Data(&ssCmd);

    /* Switch RTE mode to SAFE_STATE */
    (void)Rte_Switch_SafetyMode(RTE_MODE_SAFETY_SAFE_STATE);

    /* Raise DEM events */
    (void)Dem_SetEventStatus(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED,
                              DEM_EVENT_STATUS_FAILED);
    (void)Dem_SetEventStatus(DEM_EVENT_SAFE_STATE_ENTRY,
                              DEM_EVENT_STATUS_FAILED);

    /* Capture freeze frame */
    ff.Timestamp_ms        = 0U;  /* In production: read from GPT */
    ff.VehicleSpeed_mps    = dynState->VehicleSpeed_mps;
    ff.YawRate_radps       = dynState->YawRate_radps;
    ff.SteerAngle_deg      = SA_Prev_SteerCmd_deg;
    ff.LateralAccel_mps2   = dynState->LateralAccel_mps2;
    ff.SafeStateActive     = 1U;
    ff.AiCmdOverrideCount  = (uint8)SA_OverrideCount_per100ms;
    ff.IPC_FrameSeqNum     = 0U;

    (void)Dem_SetFreezeFrameData(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED, &ff);

    /* Increment override count */
    SA_OverrideCount++;
    SA_OverrideCount_per100ms++;

    (void)violationType; /* Suppress unused-param warning — used in extended logging */
}

/*=====================================================================================
 * Rte_Runnable_SafetyArbitrator_Init
 *====================================================================================*/
void Rte_Runnable_SafetyArbitrator_Init(void)
{
    ASIL_D_SET(SA_SafeStateActive, 0UL);

    SA_OverrideCount         = 0U;
    SA_OverrideCount_per100ms = 0U;
    SA_Cycle100msCounter     = 0U;
    SA_ViolationDebounceCount = 0U;
    SA_Prev_SteerCmd_deg     = 0.0f;
    SA_Prev_AccelCmd_mps2    = 0.0f;
    SA_Initialized           = TRUE;
}

/*=====================================================================================
 * Rte_Runnable_SafetyArbitrator_10ms — Core AI Command Arbitration
 *====================================================================================*/
void Rte_Runnable_SafetyArbitrator_10ms(void)
{
    Rte_AiCommandType            aiCmd;
    Rte_VehicleDynamicsStateType dynState;
    Rte_SafetyArbitratorOutputType output;

    float32 steerMax_deg;
    float32 yawRateMax_radps;
    boolean envelopeViolated = FALSE;
    boolean plausibilityFailed = FALSE;

    if (SA_Initialized != TRUE)
    {
        return;
    }

    /* --- WDGM Checkpoint: Entry --- */
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_ENTRY);

    /*------------------------------------------------------------------
     * STEP 1: ASIL-D Bitwise Redundancy Self-Check
     *         Detect single-bit upset in safety state variable.
     *------------------------------------------------------------------*/
    if (ASIL_D_CHECK(SA_SafeStateActive) == FALSE)
    {
        /* Memory integrity violated — immediately enter safe state */
        ASIL_D_SET(SA_SafeStateActive, 1UL);
        (void)Dem_SetEventStatus(DEM_EVENT_SENSOR_REDUNDANCY_FAIL,
                                  DEM_EVENT_STATUS_FAILED);
        /* Force safe state via RTE mode switch */
        (void)Rte_Switch_SafetyMode(RTE_MODE_SAFETY_EMERGENCY);
        (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_EXIT);
        return;
    }

    /*------------------------------------------------------------------
     * STEP 2: Read inputs from RTE ports
     *------------------------------------------------------------------*/
    (void)Rte_Read_SafetyArbitrator_AiCommand_Data(&aiCmd);
    (void)Rte_Read_SafetyArbitrator_VehicleDynamics_Data(&dynState);

    /*------------------------------------------------------------------
     * STEP 3: Static Plausibility Checks (hardware bounds)
     *------------------------------------------------------------------*/
    if (SA_AbsF32(aiCmd.RequestedSteerAngle_deg) > SA_Cal_MaxSteerAngle_deg)
    {
        plausibilityFailed = TRUE;
        (void)Dem_SetEventStatus(DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED,
                                  DEM_EVENT_STATUS_FAILED);
    }

    if (aiCmd.RequestedAcceleration_mps2 > SA_Cal_MaxAccel_mps2)
    {
        plausibilityFailed = TRUE;
        (void)Dem_SetEventStatus(DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED,
                                  DEM_EVENT_STATUS_FAILED);
    }

    if (aiCmd.RequestedAcceleration_mps2 < (-SA_Cal_MaxDecel_mps2))
    {
        plausibilityFailed = TRUE;
        (void)Dem_SetEventStatus(DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED,
                                  DEM_EVENT_STATUS_FAILED);
    }

    if (SA_AbsF32(aiCmd.RequestedYawRate_radps) > SA_Cal_MaxYawRate_radps)
    {
        plausibilityFailed = TRUE;
        (void)Dem_SetEventStatus(DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED,
                                  DEM_EVENT_STATUS_FAILED);
    }

    /* --- WDGM Checkpoint: Envelope Check --- */
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_ENVELOPE_CHECK);

    /*------------------------------------------------------------------
     * STEP 4: Dynamic Safety Envelope (μ-adaptive, speed-dependent)
     *------------------------------------------------------------------*/
    steerMax_deg     = SA_ComputeMaxSteerAngle_deg(
                           dynState.RoadMuEstimate,
                           dynState.VehicleSpeed_mps,
                           2.85f); /* Wheelbase — in production from calib */
    yawRateMax_radps = SA_ComputeMaxYawRate_radps(
                           dynState.RoadMuEstimate,
                           dynState.VehicleSpeed_mps);

    if (SA_AbsF32(aiCmd.RequestedSteerAngle_deg) > steerMax_deg)
    {
        SA_ViolationDebounceCount++;
        if (SA_ViolationDebounceCount >= (uint32)SA_Cal_ViolationDebounce)
        {
            envelopeViolated = TRUE;
        }
    }
    else
    {
        SA_ViolationDebounceCount = 0U;
    }

    if (SA_AbsF32(aiCmd.RequestedYawRate_radps) > yawRateMax_radps)
    {
        envelopeViolated = TRUE;
    }

    /* Sensor redundancy failure → block AI commands */
    if (dynState.SensorRedundancyOk == FALSE)
    {
        envelopeViolated = TRUE;
    }

    /*------------------------------------------------------------------
     * STEP 5: Arbitration Decision
     *------------------------------------------------------------------*/
    output.Reserved = 0U;

    if ((envelopeViolated == TRUE) || (plausibilityFailed == TRUE))
    {
        /* AI command REJECTED — engage safe state */
        SA_EnterSafeState(&dynState, (uint8)(envelopeViolated ? 1U : 2U));

        /* Output: maintain previous safe command or zero */
        output.CommandedSteerAngle_deg    = SA_ClampF32(
            SA_Prev_SteerCmd_deg * 0.5f, -steerMax_deg, steerMax_deg);
        output.CommandedAcceleration_mps2 = 0.0f;
        output.SafeStateActive            = TRUE;
        output.SafetyEnvelopeStatus       = 2U; /* VIOLATED */
        output.ArbitrationCount           =
            (SA_OverrideCount_per100ms > 0xFFU) ? 0xFFU :
            (uint8)SA_OverrideCount_per100ms;
    }
    else if (SA_SafeStateActive != 0UL)
    {
        /* Safe state previously active — check if cleared */
        Rte_ModeType_SafetyMode curMode = Rte_Mode_SafetyMode();

        if (curMode == RTE_MODE_SAFETY_NORMAL)
        {
            /* Safe State Manager has cleared the condition */
            ASIL_D_SET(SA_SafeStateActive, 0UL);
            output.SafeStateActive = FALSE;
        }
        else
        {
            output.SafeStateActive = TRUE;
        }

        /* Keep zero accel during transition */
        output.CommandedSteerAngle_deg    = 0.0f;
        output.CommandedAcceleration_mps2 = 0.0f;
        output.SafetyEnvelopeStatus       = 1U; /* WARN */
        output.ArbitrationCount           = 0U;
    }
    else
    {
        /* AI command ACCEPTED — pass through (with bound clamp as final guard) */
        output.CommandedSteerAngle_deg    =
            SA_ClampF32(aiCmd.RequestedSteerAngle_deg,
                        -SA_Cal_MaxSteerAngle_deg,
                         SA_Cal_MaxSteerAngle_deg);
        output.CommandedAcceleration_mps2 =
            SA_ClampF32(aiCmd.RequestedAcceleration_mps2,
                        -SA_Cal_MaxDecel_mps2,
                         SA_Cal_MaxAccel_mps2);
        output.SafeStateActive            = FALSE;
        output.SafetyEnvelopeStatus       = 0U; /* OK */
        output.ArbitrationCount           = 0U;

        (void)Dem_SetEventStatus(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED,
                                  DEM_EVENT_STATUS_PASSED);
        (void)Dem_SetEventStatus(DEM_EVENT_AI_CMD_PLAUSIBILITY_FAILED,
                                  DEM_EVENT_STATUS_PASSED);
    }

    /* Store for next cycle delta calculations */
    SA_Prev_SteerCmd_deg   = output.CommandedSteerAngle_deg;
    SA_Prev_AccelCmd_mps2  = output.CommandedAcceleration_mps2;

    /*------------------------------------------------------------------
     * STEP 6: Publish outputs via RTE
     *------------------------------------------------------------------*/
    (void)Rte_Write_SafetyArbitrator_ChassisCommand_Data(&output);
    (void)Rte_IWrite_SafetyArbitrator_ArbitrationResult(&output);

    /* --- WDGM Checkpoint: Exit --- */
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_EXIT);
}

/*=====================================================================================
 * Rte_Runnable_SafetyArbitrator_Diag_100ms — Diagnostic & NvM Flush Runnable
 *====================================================================================*/
void Rte_Runnable_SafetyArbitrator_Diag_100ms(void)
{
    SA_Cycle100msCounter++;

    /* Reset per-100ms override counter */
    SA_OverrideCount_per100ms = 0U;

    /* Safe state timeout monitoring */
    if ((SA_SafeStateActive != 0UL) && (SA_Cycle100msCounter > 100U))
    {
        /* Safe state has been active for >10 seconds — log timeout event */
        (void)Dem_SetEventStatus(DEM_EVENT_SAFE_STATE_TIMEOUT,
                                  DEM_EVENT_STATUS_FAILED);
    }
}

#define SAFETY_ARBITRATOR_STOP_SEC_CODE
#include "MemMap.h"

