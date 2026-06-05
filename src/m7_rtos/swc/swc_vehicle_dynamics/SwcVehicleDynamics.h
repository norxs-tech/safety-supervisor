/**
 * =====================================================================================
 * @file        SwcVehicleDynamics.h
 * @brief       AUTOSAR R25-11 SWC — Vehicle Dynamics & Sensor Fusion
 *              ASIL-D sensor redundancy checks, wheel speed delta validation,
 *              yaw rate plausibility via Ackermann model, and road friction
 *              coefficient (μ) estimation using Burckhardt tire model.
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

#ifndef SWC_VEHICLE_DYNAMICS_H
#define SWC_VEHICLE_DYNAMICS_H

#include "Platform_Types.h"
#include "Rte_SafetyArbitrator.h"

/*=====================================================================================
 * Vehicle Physical Constants (calibration — XCP accessible via A2L)
 *====================================================================================*/
#define VD_WHEEL_RADIUS_M             (0.32f)    /**< Nominal wheel radius [m]          */
#define VD_WHEELBASE_M                (2.85f)    /**< Axle-to-axle distance [m]         */
#define VD_TRACK_WIDTH_M              (1.60f)    /**< Left-to-right wheel distance [m]  */
#define VD_GRAVITY_MPS2               (9.81f)    /**< Gravitational constant [m/s²]     */
#define VD_WHEEL_SPEED_DELTA_THRESH   (2.0f)     /**< Max L/R wheel speed delta [rad/s] */
#define VD_YAW_RATE_PLAUS_THRESH      (0.3f)     /**< Max yaw rate estimation error [rad/s] */
#define VD_SPEED_LOW_THRESH_MPS       (1.5f)     /**< Below this: skip yaw plausibility */
#define VD_MU_ESTIMATE_ALPHA          (0.05f)    /**< 1st order filter coeff for μ      */

/*=====================================================================================
 * Sensor Redundancy Check Status
 *====================================================================================*/
typedef uint8 VD_SensorStatusType;
#define VD_SENSOR_OK                  ((VD_SensorStatusType)0x00U)
#define VD_SENSOR_DELTA_VIOLATION     ((VD_SensorStatusType)0x01U)
#define VD_SENSOR_YAW_IMPLAUSIBLE     ((VD_SensorStatusType)0x02U)
#define VD_SENSOR_INVALID_RANGE       ((VD_SensorStatusType)0x03U)

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Initialize Vehicle Dynamics SWC. Resets all state and calibration.
 * @req    AUTOSAR R25-11 SWS_RTE Init Runnable
 */
extern void VD_Init(void);

/**
 * @brief  10ms cyclic runnable — Sensor Fusion and Dynamics Estimation.
 *         1. Reads raw wheel speed sensors via Rte_Read
 *         2. Performs ASIL-D left/right delta check
 *         3. Computes kinematic yaw rate from wheel speeds
 *         4. Validates against IMU gyro yaw rate
 *         5. Estimates road friction μ via Burckhardt model
 *         6. Writes fused state via Rte_Write
 * @req    SWS_RTE_00151
 */
extern void Rte_Runnable_VehicleDynamics_10ms(void);

#endif /* SWC_VEHICLE_DYNAMICS_H */
