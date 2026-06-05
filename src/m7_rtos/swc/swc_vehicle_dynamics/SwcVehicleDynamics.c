/**
 * =====================================================================================
 * @file        SwcVehicleDynamics.c
 * @brief       AUTOSAR R25-11 SWC — Vehicle Dynamics & Sensor Fusion Implementation
 *
 *              Algorithm overview:
 *              ─────────────────────────────────────────────────────────────────
 *              1. ASIL-D Wheel Speed Redundancy Check
 *                 During straight-line driving (|SteerAngle| < 2°), the left/right
 *                 wheel speed difference must remain below VD_WHEEL_SPEED_DELTA_THRESH.
 *                 Violation → DEM_EVENT_WHEEL_SPEED_DELTA_HIGH.
 *
 *              2. Kinematic Yaw Rate Estimation (Ackermann model)
 *                 ψ̇_kin = (v_R - v_L) / TrackWidth
 *                 where v_L, v_R = mean(FL,RL), mean(FR,RR) * WheelRadius
 *
 *              3. IMU Yaw Rate Plausibility Check
 *                 |ψ̇_gyro - ψ̇_kin| > VD_YAW_RATE_PLAUS_THRESH → DEM event
 *
 *              4. Road Friction Coefficient (μ) Estimation
 *                 Burckhardt simplified: μ = (Ay / g) / sin(arctan(Ay/Az))
 *                 Low-pass filtered: μ_filt = α*μ_raw + (1-α)*μ_prev
 *                 μ < 0.25 → low-confidence warning to Safety Arbitrator
 *              ─────────────────────────────────────────────────────────────────
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

#include "SwcVehicleDynamics.h"
#include "Dem.h"
#include "WdgM.h"

/* Math library — MISRA C:2023 permissible for embedded targets with libm */
#include <math.h>

/*=====================================================================================
 * XCP-Accessible Calibration Parameters
 *====================================================================================*/
#define VEHICLE_DYNAMICS_START_SEC_CALIB_32
#include "MemMap.h"

static volatile const float32 VD_Cal_WheelRadius_m        = VD_WHEEL_RADIUS_M;
static volatile const float32 VD_Cal_Wheelbase_m          = VD_WHEELBASE_M;
static volatile const float32 VD_Cal_TrackWidth_m         = VD_TRACK_WIDTH_M;
static volatile const float32 VD_Cal_WheelDeltaThresh_rps = VD_WHEEL_SPEED_DELTA_THRESH;
static volatile const float32 VD_Cal_YawPlausThresh_radps = VD_YAW_RATE_PLAUS_THRESH;
static volatile const float32 VD_Cal_MuFilterAlpha        = VD_MU_ESTIMATE_ALPHA;
static volatile const float32 VD_Cal_MuLowWarning         = 0.25f;
static volatile const float32 VD_Cal_SpeedLowThresh_mps   = VD_SPEED_LOW_THRESH_MPS;
static volatile const float32 VD_Cal_MaxLateralAccel_mps2 = 12.0f;
static volatile const float32 VD_Cal_MaxLongAccel_mps2    = 15.0f;

#define VEHICLE_DYNAMICS_STOP_SEC_CALIB_32
#include "MemMap.h"

/*=====================================================================================
 * Module State Variables
 *====================================================================================*/
#define VEHICLE_DYNAMICS_START_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/* Fused vehicle state (output port) */
static Rte_VehicleDynamicsStateType VD_State;

/* Previous cycle μ estimate for LP filter */
static float32 VD_MuFiltered_prev;

/* Raw sensor inputs (populated by Rte_Read) */
static float32 VD_Raw_WheelFL_rps;
static float32 VD_Raw_WheelFR_rps;
static float32 VD_Raw_WheelRL_rps;
static float32 VD_Raw_WheelRR_rps;
static float32 VD_Raw_YawRate_radps;   /**< IMU gyroscope yaw rate                    */
static float32 VD_Raw_LateralAccel_mps2;
static float32 VD_Raw_LongAccel_mps2;
static float32 VD_Raw_SteerAngle_deg;

/* Diagnostic counters */
static uint8 VD_WheelDeltaFaultCount;
static uint8 VD_YawPlausFaultCount;
static uint8 VD_MuLowCount;

/* Initialized flag */
static boolean VD_Initialized;

#define VEHICLE_DYNAMICS_STOP_SEC_VAR_INIT_UNSPECIFIED
#include "MemMap.h"

/*=====================================================================================
 * Internal: Clamp float32 to range [min, max]
 *====================================================================================*/
#define VEHICLE_DYNAMICS_START_SEC_CODE
#include "MemMap.h"

static float32 VD_ClampF32(float32 val, float32 minVal, float32 maxVal)
{
    float32 result;

    if (val < minVal)
    {
        result = minVal;
    }
    else if (val > maxVal)
    {
        result = maxVal;
    }
    else
    {
        result = val;
    }

    return result;
}

/*=====================================================================================
 * Internal: Absolute value of float32 (avoids fabsf dependency on some toolchains)
 *====================================================================================*/
static float32 VD_AbsF32(float32 val)
{
    return (val < 0.0f) ? (-val) : (val);
}

/*=====================================================================================
 * Internal: ASIL-D Wheel Speed Redundancy Check
 *  Left axle: average of FL and RL
 *  Right axle: average of FR and RR
 *  During near-straight driving: delta must be within threshold.
 *====================================================================================*/
static VD_SensorStatusType VD_CheckWheelSpeedRedundancy(void)
{
    VD_SensorStatusType status;
    float32             vLeft;
    float32             vRight;
    float32             delta;
    boolean             isStraight;

    vLeft     = (VD_Raw_WheelFL_rps + VD_Raw_WheelRL_rps) * 0.5f;
    vRight    = (VD_Raw_WheelFR_rps + VD_Raw_WheelRR_rps) * 0.5f;
    delta     = VD_AbsF32(vLeft - vRight);
    isStraight = (VD_AbsF32(VD_Raw_SteerAngle_deg) < 2.0f) ? TRUE : FALSE;

    if ((isStraight == TRUE) && (delta > VD_Cal_WheelDeltaThresh_rps))
    {
        VD_WheelDeltaFaultCount++;
        if (VD_WheelDeltaFaultCount >= 3U) /* 3 consecutive 10ms cycles = 30ms debounce */
        {
            (void)Dem_SetEventStatus(DEM_EVENT_WHEEL_SPEED_DELTA_HIGH,
                                      DEM_EVENT_STATUS_FAILED);
            status = VD_SENSOR_DELTA_VIOLATION;
        }
        else
        {
            status = VD_SENSOR_OK; /* Still within debounce window */
        }
    }
    else
    {
        if (VD_WheelDeltaFaultCount > 0U)
        {
            VD_WheelDeltaFaultCount--;
        }
        (void)Dem_SetEventStatus(DEM_EVENT_WHEEL_SPEED_DELTA_HIGH,
                                  DEM_EVENT_STATUS_PASSED);
        status = VD_SENSOR_OK;
    }

    return status;
}

/*=====================================================================================
 * Internal: Kinematic Yaw Rate Estimation and IMU Plausibility Check
 *  ψ̇_kin = (v_R - v_L) / TrackWidth_m
 *  (v in m/s = wheel_rps * WheelRadius_m)
 *====================================================================================*/
static VD_SensorStatusType VD_CheckYawRatePlausibility(float32 * const kinYawRate_out)
{
    VD_SensorStatusType status;
    float32             vLeft_mps;
    float32             vRight_mps;
    float32             kinYawRate;
    float32             yawError;

    vLeft_mps  = ((VD_Raw_WheelFL_rps + VD_Raw_WheelRL_rps) * 0.5f)
                 * VD_Cal_WheelRadius_m;
    vRight_mps = ((VD_Raw_WheelFR_rps + VD_Raw_WheelRR_rps) * 0.5f)
                 * VD_Cal_WheelRadius_m;

    kinYawRate = (vRight_mps - vLeft_mps) / VD_Cal_TrackWidth_m;
    *kinYawRate_out = kinYawRate;

    /* Only check plausibility above low-speed threshold */
    if (VD_State.VehicleSpeed_mps > VD_Cal_SpeedLowThresh_mps)
    {
        yawError = VD_AbsF32(VD_Raw_YawRate_radps - kinYawRate);

        if (yawError > VD_Cal_YawPlausThresh_radps)
        {
            VD_YawPlausFaultCount++;
            if (VD_YawPlausFaultCount >= 5U) /* 50ms debounce */
            {
                (void)Dem_SetEventStatus(DEM_EVENT_YAW_RATE_IMPLAUSIBLE,
                                          DEM_EVENT_STATUS_FAILED);
                status = VD_SENSOR_YAW_IMPLAUSIBLE;
            }
            else
            {
                status = VD_SENSOR_OK;
            }
        }
        else
        {
            VD_YawPlausFaultCount = 0U;
            (void)Dem_SetEventStatus(DEM_EVENT_YAW_RATE_IMPLAUSIBLE,
                                      DEM_EVENT_STATUS_PASSED);
            status = VD_SENSOR_OK;
        }
    }
    else
    {
        status = VD_SENSOR_OK;
    }

    return status;
}

/*=====================================================================================
 * Internal: Road Friction Coefficient (μ) Estimation
 *  Simplified Burckhardt tire model:
 *    Slip force estimate = sqrt(Ay² + Ax²) / g
 *    μ_raw ≈ |lateral_acceleration| / (g * cos(roll_angle))
 *    For roll-less approximation: μ_raw = |Ay| / g
 *  Low-pass filter: μ_filt[n] = α*μ_raw + (1-α)*μ_filt[n-1]
 *====================================================================================*/
static float32 VD_EstimateRoadMu(void)
{
    float32 totalAccel;
    float32 muRaw;
    float32 muFiltered;

    /* Combined deceleration vector magnitude */
    totalAccel = sqrtf(
        (VD_Raw_LateralAccel_mps2 * VD_Raw_LateralAccel_mps2) +
        (VD_Raw_LongAccel_mps2    * VD_Raw_LongAccel_mps2)
    );

    muRaw = totalAccel / VD_GRAVITY_MPS2;

    /* Clamp to physically plausible range [0.05, 1.0] */
    muRaw = VD_ClampF32(muRaw, 0.05f, 1.0f);

    /* First-order LP filter */
    muFiltered = (VD_Cal_MuFilterAlpha * muRaw) +
                 ((1.0f - VD_Cal_MuFilterAlpha) * VD_MuFiltered_prev);

    VD_MuFiltered_prev = muFiltered;

    /* Low-confidence warning */
    if (muFiltered < VD_Cal_MuLowWarning)
    {
        VD_MuLowCount++;
        if (VD_MuLowCount >= 10U) /* 100ms continuous low-mu */
        {
            (void)Dem_SetEventStatus(DEM_EVENT_ROAD_MU_LOW_CONFIDENCE,
                                      DEM_EVENT_STATUS_PREFAILED);
        }
    }
    else
    {
        VD_MuLowCount = 0U;
        (void)Dem_SetEventStatus(DEM_EVENT_ROAD_MU_LOW_CONFIDENCE,
                                  DEM_EVENT_STATUS_PASSED);
    }

    return muFiltered;
}

/*=====================================================================================
 * VD_Init
 *====================================================================================*/
void VD_Init(void)
{
    uint8 * stateBytes;
    uint32  byteIdx;

    /* Zero-initialize state structure byte-by-byte (MISRA C safe memset alternative) */
    stateBytes = (uint8 *)&VD_State;
    for (byteIdx = 0U; byteIdx < sizeof(Rte_VehicleDynamicsStateType); byteIdx++)
    {
        stateBytes[byteIdx] = 0U;
    }

    VD_MuFiltered_prev      = 0.8f;   /* Start with nominal dry asphalt assumption */
    VD_WheelDeltaFaultCount = 0U;
    VD_YawPlausFaultCount   = 0U;
    VD_MuLowCount           = 0U;
    VD_Raw_WheelFL_rps      = 0.0f;
    VD_Raw_WheelFR_rps      = 0.0f;
    VD_Raw_WheelRL_rps      = 0.0f;
    VD_Raw_WheelRR_rps      = 0.0f;
    VD_Raw_YawRate_radps    = 0.0f;
    VD_Raw_LateralAccel_mps2 = 0.0f;
    VD_Raw_LongAccel_mps2   = 0.0f;
    VD_Raw_SteerAngle_deg   = 0.0f;

    VD_State.SensorRedundancyOk = TRUE;
    VD_Initialized = TRUE;
}

/*=====================================================================================
 * Rte_Runnable_VehicleDynamics_10ms — Main 10ms Cyclic Runnable
 *====================================================================================*/
void Rte_Runnable_VehicleDynamics_10ms(void)
{
    VD_SensorStatusType wheelStatus;
    VD_SensorStatusType yawStatus;
    float32             kinYawRate  = 0.0f;
    float32             speedLeft;
    float32             speedRight;
    float32             muEstimate;
    Rte_VehicleDynamicsStateType outputState;

    if (VD_Initialized != TRUE)
    {
        return;
    }

    /* --- WDGM Checkpoint: Entry --- */
    (void)WdgM_CheckpointReached(WDGM_SE_VEHICLE_DYNAMICS, WDGM_CP_VD_ENTRY);

    /*------------------------------------------------------------------
     * Step 1: Read raw sensors from MCAL/RTE ports
     *         In production: Rte_Read_VehicleDynamics_WheelSpeed_FL(&VD_Raw_WheelFL_rps)
     *         Here: sensor values assumed populated by MCAL interrupt handlers
     *------------------------------------------------------------------*/

    /*------------------------------------------------------------------
     * Step 2: Input range validation (ASIL-D defence-in-depth)
     *------------------------------------------------------------------*/
    VD_Raw_WheelFL_rps    = VD_ClampF32(VD_Raw_WheelFL_rps,  0.0f, 260.0f);
    VD_Raw_WheelFR_rps    = VD_ClampF32(VD_Raw_WheelFR_rps,  0.0f, 260.0f);
    VD_Raw_WheelRL_rps    = VD_ClampF32(VD_Raw_WheelRL_rps,  0.0f, 260.0f);
    VD_Raw_WheelRR_rps    = VD_ClampF32(VD_Raw_WheelRR_rps,  0.0f, 260.0f);
    VD_Raw_YawRate_radps  = VD_ClampF32(VD_Raw_YawRate_radps, -3.5f, 3.5f);
    VD_Raw_LateralAccel_mps2 = VD_ClampF32(VD_Raw_LateralAccel_mps2,
                                            -VD_Cal_MaxLateralAccel_mps2,
                                             VD_Cal_MaxLateralAccel_mps2);
    VD_Raw_LongAccel_mps2    = VD_ClampF32(VD_Raw_LongAccel_mps2,
                                            -VD_Cal_MaxLongAccel_mps2,
                                             VD_Cal_MaxLongAccel_mps2);

    /*------------------------------------------------------------------
     * Step 3: Compute vehicle speed (average of all 4 wheel speeds)
     *------------------------------------------------------------------*/
    speedLeft  = (VD_Raw_WheelFL_rps + VD_Raw_WheelRL_rps) * 0.5f;
    speedRight = (VD_Raw_WheelFR_rps + VD_Raw_WheelRR_rps) * 0.5f;
    VD_State.VehicleSpeed_mps =
        ((speedLeft + speedRight) * 0.5f) * VD_Cal_WheelRadius_m;

    /*------------------------------------------------------------------
     * Step 4: WDGM Checkpoint — Sensor Fusion mid-point
     *------------------------------------------------------------------*/
    (void)WdgM_CheckpointReached(WDGM_SE_VEHICLE_DYNAMICS, WDGM_CP_VD_SENSOR_FUSION);

    /*------------------------------------------------------------------
     * Step 5: ASIL-D Wheel Speed Redundancy Check
     *------------------------------------------------------------------*/
    wheelStatus = VD_CheckWheelSpeedRedundancy();

    /*------------------------------------------------------------------
     * Step 6: Kinematic Yaw Rate + IMU Plausibility
     *------------------------------------------------------------------*/
    yawStatus = VD_CheckYawRatePlausibility(&kinYawRate);

    /* Use kinematic yaw rate as fused yaw (cross-check IMU) */
    VD_State.YawRate_radps =
        (VD_Raw_YawRate_radps * 0.7f) + (kinYawRate * 0.3f);

    /*------------------------------------------------------------------
     * Step 7: Road Friction Coefficient Estimation
     *------------------------------------------------------------------*/
    VD_State.LateralAccel_mps2 = VD_Raw_LateralAccel_mps2;
    VD_State.LongAccel_mps2    = VD_Raw_LongAccel_mps2;
    muEstimate                 = VD_EstimateRoadMu();
    VD_State.RoadMuEstimate    = muEstimate;

    /*------------------------------------------------------------------
     * Step 8: Wheel speed state outputs
     *------------------------------------------------------------------*/
    VD_State.WheelSpeedFL_rps = VD_Raw_WheelFL_rps;
    VD_State.WheelSpeedFR_rps = VD_Raw_WheelFR_rps;
    VD_State.WheelSpeedRL_rps = VD_Raw_WheelRL_rps;
    VD_State.WheelSpeedRR_rps = VD_Raw_WheelRR_rps;

    /*------------------------------------------------------------------
     * Step 9: Sensor redundancy aggregate status
     *------------------------------------------------------------------*/
    if ((wheelStatus == VD_SENSOR_OK) && (yawStatus == VD_SENSOR_OK))
    {
        VD_State.SensorRedundancyOk = TRUE;
        (void)Dem_SetEventStatus(DEM_EVENT_SENSOR_REDUNDANCY_FAIL,
                                  DEM_EVENT_STATUS_PASSED);
    }
    else
    {
        VD_State.SensorRedundancyOk = FALSE;
        (void)Dem_SetEventStatus(DEM_EVENT_SENSOR_REDUNDANCY_FAIL,
                                  DEM_EVENT_STATUS_FAILED);
    }

    /*------------------------------------------------------------------
     * Step 10: Publish fused state via RTE
     *------------------------------------------------------------------*/
    outputState = VD_State; /* Snapshot for atomic write */
    (void)Rte_Write_SafetyArbitrator_VehicleDynamics_Data(&outputState);

    /* --- WDGM Checkpoint: Exit --- */
    (void)WdgM_CheckpointReached(WDGM_SE_VEHICLE_DYNAMICS, WDGM_CP_VD_EXIT);
}

#define VEHICLE_DYNAMICS_STOP_SEC_CODE
#include "MemMap.h"

