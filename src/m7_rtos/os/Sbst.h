/**
 * =====================================================================================
 * @file        Sbst.h
 * @brief       Startup Built-In Self-Test — public interface.
 *              Pre-scheduling hardware diagnostics per ISO 26262-5 Table D.1:
 *              RAM March C- (subset) on a dedicated test region, vector table
 *              integrity check, and FPU availability check. Any failure prevents
 *              scheduler start and forces the hardware safe state.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-5 ASIL-D (Table D.1 — RAM test, DC ≈ 90% stuck-at),
 *              ISO 26262-6, AUTOSAR R25-11
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.2        2026-06-12  norxs-lab       Created (boot diagnostics)
 * =====================================================================================
 */

#ifndef SBST_H
#define SBST_H

#include "Platform_Types.h"

/*=====================================================================================
 * Result Codes
 *====================================================================================*/
typedef uint8 Sbst_ResultType;
#define SBST_OK                 ((Sbst_ResultType)0x00U)
#define SBST_E_RAM_MARCH        ((Sbst_ResultType)0x01U)  /**< RAM stuck-at/coupling   */
#define SBST_E_VECTOR_TABLE     ((Sbst_ResultType)0x02U)  /**< Vector table corrupted  */
#define SBST_E_FPU              ((Sbst_ResultType)0x03U)  /**< FPU not enabled         */
#define SBST_E_PARAM            ((Sbst_ResultType)0xFFU)  /**< Invalid arguments       */

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  March C- (subset) destructive RAM test over a word-aligned buffer:
 *         ⇑(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇑(r0).
 *         Detects stuck-at, transition, and a large class of coupling faults
 *         (diagnostic coverage ≈ 90% per ISO 26262-5 Table D.1 for March tests).
 * @param  Buffer  [io]  Word-aligned test region (contents destroyed, left = 0).
 * @param  Words   [in]  Region length in 32-bit words (> 0).
 * @return SBST_OK, SBST_E_RAM_MARCH on miscompare, SBST_E_PARAM on bad args.
 * @req    SSR-OS-004
 */
extern Sbst_ResultType Sbst_RamMarchC(volatile uint32 * const Buffer, uint32 Words);

/**
 * @brief  Run the full startup self-test sequence (RAM march on the dedicated
 *         scratch region, vector table entries 0/1 sanity, FPU CPACR check).
 *         Target-only checks (vector table, FPU) are compiled out under
 *         UNIT_TEST_BUILD; the RAM march logic is host-tested directly.
 * @return SBST_OK or the first failing check's result code.
 * @req    SSR-OS-004, SSR-OS-005
 */
extern Sbst_ResultType Sbst_Run(void);

#endif /* SBST_H */
