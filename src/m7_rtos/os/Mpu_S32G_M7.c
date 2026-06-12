/**
 * =====================================================================================
 * @file        Mpu_S32G_M7.c
 * @brief       ARMv7-M PMSAv7 MPU configuration — spatial freedom from interference.
 *
 *              Region map (mirrors MEMORY block in tools/s32g_m7_safety.ld):
 *              ─────────────────────────────────────────────────────────────────────
 *              # Base        Size    Attributes                       Purpose
 *              ─────────────────────────────────────────────────────────────────────
 *              0 0x00000000  256KB   RO, exec, normal WBWA            ITCM code
 *              1 0x20000000  256KB   RW, XN,   normal WBWA            DTCM data+stack
 *              2 0x24000000  256KB   RW, XN,   normal WBWA            AIPS+Standby SRAM
 *              3 0x24040000   64KB   RW, XN,   normal NON-CACHE, S    Shared SRAM (IPC)
 *              4 0x00400000  512KB   RO, XN,   normal WT              Calibration flash
 *              5 0x40000000  512MB   RW, XN,   device shareable       Peripherals
 *              ─────────────────────────────────────────────────────────────────────
 *
 *              Design notes:
 *              - PRIVDEFENA = 0: accesses outside the regions above raise
 *                MemManage, which the OS routes to safe state via watchdog expiry
 *                (see startup_s32g_m7.c). This converts wild pointers and code-area
 *                writes into a detected, FTTI-bounded fault (ISO 26262-6 §7.4.9).
 *              - Region 0 is non-writable: control-flow integrity against runtime
 *                code patching.
 *              - Region 3 non-cacheable removes the data-coherency window on the
 *                A53↔M7 IPC path (defense in depth alongside explicit cache
 *                maintenance in the CDD).
 *              - The per-SWC linker sections (.safety_arbitrator_bss etc.) are
 *                32-byte aligned so an integrator running unprivileged tasks can
 *                add task-switched subregions on top of this static base map.
 *
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

#include "Platform_Types.h"
#include "Mpu_S32G_M7.h"

/*=====================================================================================
 * ARMv7-M MPU Registers (ARM DDI 0403E §B3.5)
 *====================================================================================*/
#define MPU_TYPE    (*((volatile const uint32 *)0xE000ED90UL))
#define MPU_CTRL    (*((volatile uint32 *)0xE000ED94UL))
#define MPU_RNR     (*((volatile uint32 *)0xE000ED98UL))
#define MPU_RBAR    (*((volatile uint32 *)0xE000ED9CUL))
#define MPU_RASR    (*((volatile uint32 *)0xE000EDA0UL))

#define SCB_SHCSR   (*((volatile uint32 *)0xE000ED24UL))
#define SHCSR_MEMFAULTENA          (1UL << 16)

#define MPU_CTRL_ENABLE            (1UL << 0)
#define MPU_TYPE_DREGION_SHIFT     (8U)
#define MPU_TYPE_DREGION_MASK      (0xFFUL)

/* RASR field encoding */
#define RASR_ENABLE                (1UL << 0)
#define RASR_SIZE(log2bytes)       ((uint32)(((log2bytes) - 1UL) & 0x1FUL) << 1)
#define RASR_B                     (1UL << 16)
#define RASR_C                     (1UL << 17)
#define RASR_S                     (1UL << 18)
#define RASR_TEX(x)                ((uint32)((x) & 0x7UL) << 19)
#define RASR_AP_RW                 (3UL << 24)   /* full access                       */
#define RASR_AP_RO                 (6UL << 24)   /* read-only, any privilege          */
#define RASR_XN                    (1UL << 28)

/* Composite attribute sets */
#define ATTR_NORMAL_WBWA           (RASR_TEX(1U) | RASR_C | RASR_B)        /* WB-WA   */
#define ATTR_NORMAL_WT             (RASR_TEX(0U) | RASR_C)                 /* WT      */
#define ATTR_NORMAL_NONCACHE_SH    (RASR_TEX(1U) | RASR_S)                 /* NC, S   */
#define ATTR_DEVICE_SH             (RASR_B | RASR_S)                       /* Device  */

/*=====================================================================================
 * Static Region Table
 *====================================================================================*/
typedef struct
{
    uint32 BaseAddress;     /**< Region base (must be size-aligned)                    */
    uint32 SizeLog2;        /**< log2(region size in bytes)                            */
    uint32 Attributes;      /**< AP | XN | TEX/C/B/S composite                         */
} Mpu_RegionConfigType;

static const Mpu_RegionConfigType Mpu_Regions[6U] =
{
    /* 0: ITCM code — execute + read, NO write (runtime code-patch protection)        */
    { 0x00000000UL, 18U, RASR_AP_RO | ATTR_NORMAL_WBWA                  },
    /* 1: DTCM data + stack — RW, never executable                                    */
    { 0x20000000UL, 18U, RASR_AP_RW | RASR_XN | ATTR_NORMAL_WBWA        },
    /* 2: AIPS SRAM + Standby SRAM (contiguous 256KB) — RW, XN                        */
    { 0x24000000UL, 18U, RASR_AP_RW | RASR_XN | ATTR_NORMAL_WBWA        },
    /* 3: Shared SRAM (A53↔M7 IPC) — RW, XN, non-cacheable, shareable                 */
    { 0x24040000UL, 16U, RASR_AP_RW | RASR_XN | ATTR_NORMAL_NONCACHE_SH },
    /* 4: Calibration flash — read-only data, XN                                      */
    { 0x00400000UL, 19U, RASR_AP_RO | RASR_XN | ATTR_NORMAL_WT          },
    /* 5: Peripheral space — RW, XN, device-ordered                                   */
    { 0x40000000UL, 29U, RASR_AP_RW | RASR_XN | ATTR_DEVICE_SH          },
};

#define MPU_REGION_COUNT_USED      (6U)

/*=====================================================================================
 * Mpu_GetRegionCount
 *====================================================================================*/
uint8 Mpu_GetRegionCount(void)
{
    return (uint8)((MPU_TYPE >> MPU_TYPE_DREGION_SHIFT) & MPU_TYPE_DREGION_MASK);
}

/*=====================================================================================
 * Mpu_Init
 *====================================================================================*/
Std_ReturnType Mpu_Init(void)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint32         region;
    const uint8    hwRegions = Mpu_GetRegionCount();

    if (hwRegions >= (uint8)MPU_REGION_COUNT_USED)
    {
        /* Disable MPU during reconfiguration */
        MPU_CTRL = 0U;
        __asm volatile ("dsb 0xF" ::: "memory");

        /* Program the static region map */
        for (region = 0U; region < MPU_REGION_COUNT_USED; region++)
        {
            MPU_RNR  = region;
            MPU_RBAR = Mpu_Regions[region].BaseAddress;
            MPU_RASR = Mpu_Regions[region].Attributes
                     | RASR_SIZE(Mpu_Regions[region].SizeLog2)
                     | RASR_ENABLE;
        }

        /* Disable all remaining hardware regions */
        for (region = MPU_REGION_COUNT_USED; region < (uint32)hwRegions; region++)
        {
            MPU_RNR  = region;
            MPU_RASR = 0U;
            MPU_RBAR = 0U;
        }

        /* Enable MemManage fault (dedicated handler instead of HardFault escalation) */
        SCB_SHCSR |= SHCSR_MEMFAULTENA;

        /* Enable: PRIVDEFENA=0 (no background map), HFNMIENA=0 */
        MPU_CTRL = MPU_CTRL_ENABLE;
        __asm volatile ("dsb 0xF" ::: "memory");
        __asm volatile ("isb 0xF" ::: "memory");

        retVal = E_OK;
    }

    return retVal;
}
