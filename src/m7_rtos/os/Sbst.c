/**
 * =====================================================================================
 * @file        Sbst.c
 * @brief       Startup Built-In Self-Test — RAM March C- (subset), vector table
 *              integrity, FPU availability. Executed by Os_InitRuntime() before
 *              any SWC initialization; failure prevents scheduler start.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-5 ASIL-D (Table D.1), ISO 26262-6, AUTOSAR R25-11
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.2        2026-06-12  norxs-lab       Created (boot diagnostics)
 * =====================================================================================
 */

#include "Platform_Types.h"
#include "Sbst.h"

/*=====================================================================================
 * Dedicated RAM test scratch region (DTCM .bss — exercised every boot)
 *====================================================================================*/
#define SBST_RAM_TEST_WORDS     (64U)

static volatile uint32 Sbst_RamTestRegion[SBST_RAM_TEST_WORDS];

#ifndef UNIT_TEST_BUILD
/* Vector table + FPU references (target build only) */
extern uint32 __stack_top[];
extern const volatile uint32 __isr_vector_start[];
extern void Reset_Handler(void);

#define SBST_CPACR  (*((volatile const uint32 *)0xE000ED88UL))
#define SBST_CPACR_CP10_CP11_FULL   (0x00F00000UL)
#endif

/*=====================================================================================
 * Sbst_RamMarchC — March C- subset (word granularity, 0/1 backgrounds)
 *====================================================================================*/
Sbst_ResultType Sbst_RamMarchC(volatile uint32 * const Buffer, uint32 Words)
{
    Sbst_ResultType result = SBST_E_PARAM;
    uint32          i;
    boolean         fail = FALSE;

    if ((Buffer != NULL_PTR) && (Words > 0U))
    {
        /* M1: ascending — write 0 */
        for (i = 0U; i < Words; i++)
        {
            Buffer[i] = 0x00000000UL;
        }

        /* M2: ascending — read 0, write all-1 */
        for (i = 0U; i < Words; i++)
        {
            if (Buffer[i] != 0x00000000UL) { fail = TRUE; }
            Buffer[i] = 0xFFFFFFFFUL;
        }

        /* M3: ascending — read 1, write 0 */
        for (i = 0U; i < Words; i++)
        {
            if (Buffer[i] != 0xFFFFFFFFUL) { fail = TRUE; }
            Buffer[i] = 0x00000000UL;
        }

        /* M4: descending — read 0, write 1 */
        for (i = Words; i > 0U; i--)
        {
            if (Buffer[i - 1U] != 0x00000000UL) { fail = TRUE; }
            Buffer[i - 1U] = 0xFFFFFFFFUL;
        }

        /* M5: descending — read 1, write 0 */
        for (i = Words; i > 0U; i--)
        {
            if (Buffer[i - 1U] != 0xFFFFFFFFUL) { fail = TRUE; }
            Buffer[i - 1U] = 0x00000000UL;
        }

        /* M6: ascending — read 0 (leave region zeroed) */
        for (i = 0U; i < Words; i++)
        {
            if (Buffer[i] != 0x00000000UL) { fail = TRUE; }
        }

        result = (fail == TRUE) ? SBST_E_RAM_MARCH : SBST_OK;
    }

    return result;
}

/*=====================================================================================
 * Sbst_Run — full startup sequence
 *====================================================================================*/
Sbst_ResultType Sbst_Run(void)
{
    Sbst_ResultType result;

    /* 1. RAM march on the dedicated DTCM scratch region */
    result = Sbst_RamMarchC(Sbst_RamTestRegion, SBST_RAM_TEST_WORDS);

#ifndef UNIT_TEST_BUILD
    /* 2. Vector table integrity: initial SP and Reset vector (thumb bit set) */
    if (result == SBST_OK)
    {
        /* Address taken via linker symbol (vector table base) — avoids literal
           address-0 access that ISO C / GCC treat as a null dereference. */
        if ((__isr_vector_start[0U] != (uint32)__stack_top) ||
            (__isr_vector_start[1U] != ((uint32)&Reset_Handler | 1UL)))
        {
            result = SBST_E_VECTOR_TABLE;
        }
    }

    /* 3. FPU enabled (CPACR CP10/CP11 full access) — hard-float ABI prerequisite */
    if (result == SBST_OK)
    {
        if ((SBST_CPACR & SBST_CPACR_CP10_CP11_FULL) != SBST_CPACR_CP10_CP11_FULL)
        {
            result = SBST_E_FPU;
        }
    }
#endif

    return result;
}
