/**
 * =====================================================================================
 * @file        SwcSafeStateMgr.h
 * @brief       AUTOSAR R25-11 SWC — Safe State Degradation Manager public interface.
 *              Exposes the init and 10ms cyclic runnables executed by the OS task
 *              scheduler. Implements the two-phase controlled vehicle stop (smooth-step
 *              steering return + progressive deceleration ramp) on safe state entry.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.1        2026-06-11  norxs-lab       Created (missing-prototype remediation)
 * =====================================================================================
 */

#ifndef SWC_SAFE_STATE_MGR_H
#define SWC_SAFE_STATE_MGR_H

#include "Platform_Types.h"

/*=====================================================================================
 * API Declarations — Runnable Entities (called by OS task scheduler)
 *====================================================================================*/

/**
 * @brief  Init runnable — resets the safe-state FSM and interpolation state.
 *         Must be called once at ECU init before the first 10ms cycle.
 */
extern void Rte_Runnable_SafeStateMgr_Init(void);

/**
 * @brief  10ms cyclic runnable — executes the safe-state FSM:
 *         IDLE → STEER_RETURN ∥ DECEL_RAMP → VEHICLE_HOLD.
 *         Instruments WdgM checkpoints SSM_ENTRY / SSM_INTERPOLATION / SSM_EXIT.
 */
extern void Rte_Runnable_SafeStateMgr_10ms(void);

#endif /* SWC_SAFE_STATE_MGR_H */
