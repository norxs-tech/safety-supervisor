/**
 * =====================================================================================
 * @file        Mpu_S32G_M7.h
 * @brief       ARMv7-M PMSAv7 Memory Protection Unit driver — public interface.
 *              Enforces the freedom-from-interference (FFI) memory partitioning
 *              documented in tools/s32g_m7_safety.ld (ISO 26262-6 Annex D, spatial
 *              isolation). With PRIVDEFENA disabled, any access outside the
 *              configured regions raises MemManage — routed to safe state.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11, ARMv7-M PMSAv7 (ARM DDI 0403E)
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.2        2026-06-12  norxs-lab       Created (MPU enforcement remediation)
 * =====================================================================================
 */

#ifndef MPU_S32G_M7_H
#define MPU_S32G_M7_H

#include "Platform_Types.h"

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Configure and enable the Cortex-M7 MPU with the static FFI region map:
 *         R0 ITCM code (RX, no-write), R1 DTCM data (RW, XN), R2 AIPS/Standby SRAM
 *         (RW, XN), R3 Shared SRAM (RW, XN, non-cacheable, shareable), R4 calibration
 *         flash (RO, XN), R5 peripheral space (RW, XN, device).
 *         PRIVDEFENA = 0: the default memory map is DISABLED — all accesses outside
 *         these regions fault (strict spatial FFI). HFNMIENA = 0.
 * @return E_OK if the MPU was present and enabled, E_NOT_OK if no MPU implemented.
 * @req    SSR-OS-003 (ISO 26262-6 §7.4.9 — freedom from interference, memory)
 */
extern Std_ReturnType Mpu_Init(void);

/**
 * @brief  Query the number of MPU regions implemented in hardware (MPU_TYPE.DREGION).
 * @return Region count (8 or 16 on Cortex-M7; 0 = no MPU).
 */
extern uint8 Mpu_GetRegionCount(void);

#endif /* MPU_S32G_M7_H */
