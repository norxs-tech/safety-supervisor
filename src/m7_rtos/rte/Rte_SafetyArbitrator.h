/**
 * =====================================================================================
 * @file        Rte_SafetyArbitrator.h
 * @brief       AUTOSAR R25-11 RTE Auto-Generated Interface — Safety Arbitrator SWC
 *              Defines all Rte_Read / Rte_Write / Rte_IRead / Rte_IWrite macros and
 *              inter-runnable variable (IRV) types for the Safety Arbitrator component.
 *              This file is auto-generated from the ARXML system description and must
 *              not be manually edited in production — modifications tracked via HARA.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_RTE
 * @author      norxs-lab (RTE Generator v3.0-R25-11)
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#ifndef RTE_SAFETYARBITRATOR_H
#define RTE_SAFETYARBITRATOR_H

#include "Platform_Types.h"

/*=====================================================================================
 * Port Data Types — AI Command Frame (from A53 via IPC)
 *====================================================================================*/
typedef struct
{
    float32 RequestedSteerAngle_deg;   /**< AI requested steer angle  [-720..+720 deg] */
    float32 RequestedAcceleration_mps2;/**< AI requested accel/decel  [-12..+4  m/s²]  */
    float32 RequestedYawRate_radps;    /**< AI requested yaw rate     [-1.5..+1.5 rad/s]*/
    uint8   AutonomyLevel;             /**< SAE L0..L4                                  */
    uint8   Reserved[3U];              /**< Alignment padding                           */
} Rte_AiCommandType;

/*=====================================================================================
 * Port Data Types — Vehicle Dynamics State (from VehicleDynamics SWC)
 *====================================================================================*/
typedef struct
{
    float32 VehicleSpeed_mps;          /**< Fused longitudinal speed   [0..83 m/s]      */
    float32 LateralAccel_mps2;         /**< Lateral acceleration       [-15..+15 m/s²]  */
    float32 YawRate_radps;             /**< Gyro-fused yaw rate        [-3.0..+3.0 rad/s]*/
    float32 LongAccel_mps2;            /**< Longitudinal acceleration  [-15..+4 m/s²]   */
    float32 RoadMuEstimate;            /**< Road friction coefficient  [0.05..1.0]       */
    float32 WheelSpeedFL_rps;          /**< Front-Left wheel speed     [rad/s]           */
    float32 WheelSpeedFR_rps;          /**< Front-Right wheel speed    [rad/s]           */
    float32 WheelSpeedRL_rps;          /**< Rear-Left wheel speed      [rad/s]           */
    float32 WheelSpeedRR_rps;          /**< Rear-Right wheel speed     [rad/s]           */
    uint8   SensorRedundancyOk;        /**< All redundant sensors consistent             */
    uint8   Reserved[3U];
} Rte_VehicleDynamicsStateType;

/*=====================================================================================
 * Port Data Types — Safety Arbitrator Output (to actuators)
 *====================================================================================*/
typedef struct
{
    float32 CommandedSteerAngle_deg;   /**< Final commanded steer angle (post-arbitration)*/
    float32 CommandedAcceleration_mps2;/**< Final commanded acceleration                  */
    uint8   SafeStateActive;           /**< 1 = safe state engaged, AI overridden         */
    uint8   ArbitrationCount;          /**< Rolling count of AI overrides (per 1s)        */
    uint8   SafetyEnvelopeStatus;      /**< 0=OK, 1=WARN, 2=VIOLATED                     */
    uint8   Reserved;
} Rte_SafetyArbitratorOutputType;

/*=====================================================================================
 * Port Data Types — Safe State Manager Command (to Safe State SWC)
 *====================================================================================*/
typedef struct
{
    uint8   EnterSafeState;            /**< 1 = trigger safe state entry                  */
    float32 TargetDeceleration_mps2;   /**< Requested decel rate for smooth stop          */
    uint8   Reserved[3U];
} Rte_SafeStateCommandType;

/*=====================================================================================
 * RTE Read Macros — Safety Arbitrator Runnable Inputs
 * Prototype: Std_ReturnType Rte_Read_<PortName>_<ElementName>(type *data)
 *====================================================================================*/

/** @brief Read latest AI command from IPC port (updated by IPC handler runnable) */
extern Std_ReturnType Rte_Read_SafetyArbitrator_AiCommand_Data(
    Rte_AiCommandType * const data);

/** @brief Read vehicle dynamics state from VehicleDynamics SWC via VFB */
extern Std_ReturnType Rte_Read_SafetyArbitrator_VehicleDynamics_Data(
    Rte_VehicleDynamicsStateType * const data);

/*=====================================================================================
 * RTE Write Macros — Safety Arbitrator Runnable Outputs
 *====================================================================================*/

/** @brief Write arbitrated chassis command to actuator port */
extern Std_ReturnType Rte_Write_SafetyArbitrator_ChassisCommand_Data(
    const Rte_SafetyArbitratorOutputType * const data);

/** @brief Write safe state command to Safe State Manager SWC */
extern Std_ReturnType Rte_Write_SafetyArbitrator_SafeStateCmd_Data(
    const Rte_SafeStateCommandType * const data);

/*=====================================================================================
 * RTE Inter-Runnable Variable (IRV) Access — shared within component
 *====================================================================================*/

/** @brief Read current arbitration output IRV (for diagnostics runnable) */
extern Std_ReturnType Rte_IRead_SafetyArbitrator_ArbitrationResult(
    Rte_SafetyArbitratorOutputType * const data);

/** @brief Write arbitration result to IRV */
extern Std_ReturnType Rte_IWrite_SafetyArbitrator_ArbitrationResult(
    const Rte_SafetyArbitratorOutputType * const data);

/*=====================================================================================
 * RTE Mode Switch — used by Safe State Manager to notify all SWCs
 *====================================================================================*/
typedef uint8 Rte_ModeType_SafetyMode;
#define RTE_MODE_SAFETY_NORMAL        ((Rte_ModeType_SafetyMode)0U)
#define RTE_MODE_SAFETY_DEGRADED      ((Rte_ModeType_SafetyMode)1U)
#define RTE_MODE_SAFETY_SAFE_STATE    ((Rte_ModeType_SafetyMode)2U)
#define RTE_MODE_SAFETY_EMERGENCY     ((Rte_ModeType_SafetyMode)3U)

extern Std_ReturnType Rte_Switch_SafetyMode(Rte_ModeType_SafetyMode mode);
extern Rte_ModeType_SafetyMode Rte_Mode_SafetyMode(void);

/*=====================================================================================
 * RTE Runnable Prototypes (called by OS task scheduler)
 *====================================================================================*/

/** @brief Safety Arbitrator 10ms cyclic runnable — main AI command plausibility check */
extern void Rte_Runnable_SafetyArbitrator_10ms(void);

/** @brief Safety Arbitrator 100ms diagnostic runnable — DEM reporting & NvM flush */
extern void Rte_Runnable_SafetyArbitrator_Diag_100ms(void);

/** @brief Safety Arbitrator init runnable — called once at ECU init */
extern void Rte_Runnable_SafetyArbitrator_Init(void);

#endif /* RTE_SAFETYARBITRATOR_H */
