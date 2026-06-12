/**
 * =====================================================================================
 * @file        OtaRollback.h
 * @brief       Dual-bank OTA rollback state machine — public interface.
 *              Bank validity is established via HSE RSA-4096 signature verification;
 *              automatic rollback triggers on WdgM deadline failure, E2E error
 *              accumulation, or boot failure (UN R156 software update integrity).
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-B, AUTOSAR R25-11, UN R156
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.1        2026-06-11  norxs-lab       Created (missing-prototype remediation)
 * =====================================================================================
 */

#ifndef OTA_ROLLBACK_H
#define OTA_ROLLBACK_H

#include "Platform_Types.h"

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  100ms background runnable — executes the dual-bank rollback FSM:
 *         IDLE → VERIFYING → HEALTH_GATE → COMMITTED / ROLLBACK.
 *         Reports DEM_EVENT_OTA_ROLLBACK_TRIGGERED on rollback activation.
 */
extern void OTA_RollbackStateMachine_Run(void);

/**
 * @brief  Query the currently active firmware bank status word.
 * @return Bank status (bank index + validity + health-gate flags).
 */
extern uint32 OTA_GetCurrentBankStatus(void);

#endif /* OTA_ROLLBACK_H */
