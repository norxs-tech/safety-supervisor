/**
 * =====================================================================================
 * @file        OtaRollback.c
 * @brief       OTA Rollback State Machine — M7 Bare-Metal / AUTOSAR BSW
 *
 *              Implements dual-bank (Bank A / Bank B) firmware rollback for NXP S32G.
 *              The rollback machine runs during the early boot phase (before OS start)
 *              and is also monitored cyclically by the Watchdog Manager.
 *
 *              State Machine:
 *              ─────────────────────────────────────────────────────────────────────
 *              BOOT_PENDING
 *                │
 *                ├─[Bank B valid & WDG not triggered]──► BANK_B_RUNNING
 *                │                                           │
 *                │                                           ├─[WDG reset detected]
 *                │                                           │   ► ROLLBACK_TRIGGERED
 *                │                                           │       │
 *                │                                           │       └─► swap boot ptr
 *                │                                           │           ► BANK_A_ACTIVE
 *                │                                           │           ► lock Bank B
 *                │                                           │           ► DEM event
 *                │                                           │           ► Reset
 *                │                                           │
 *                │                                           └─[25 boot cycles OK]
 *                │                                               ► BANK_B_COMMITTED
 *                │
 *                └─[Bank B invalid / no new firmware]──► BANK_A_ACTIVE (default)
 *              ─────────────────────────────────────────────────────────────────────
 *
 *              Flash Bank Layout (S32G 8MB NOR Flash):
 *                Bank A (stable):    0x00400000 – 0x007FFFFF
 *                Bank B (trial):     0x00800000 – 0x00BFFFFF
 *                Boot Pointer:       0x00000000 – 0x000003FF (FCF region)
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11, UN R156 (Secure OTA)
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "Platform_Types.h"
#include "Dem.h"
#include "OtaRollback.h"


/*=====================================================================================
 * Flash & Watchdog Register Map (NXP S32G)
 *====================================================================================*/
/* Software Watchdog Timer Reset Status Register */
#define S32G_MC_RGM_BASE          (0x40078000UL)
#define MC_RGM_FES_OFFSET         (0x0CUL)       /**< Functional Event Status         */
#define MC_RGM_FES_WDOG_FLAG      (0x00000020UL) /**< Bit 5: WDOG reset flag          */
#define MC_RGM_FERD_OFFSET        (0x10UL)        /**< Functional Event Reset Disable  */

/* Flash Configuration Field (FCF) — Boot Pointer at offset 0x0400 in PFLASH */
#define S32G_PFLASH_FCF_BASE      (0x00400000UL)
#define OTA_BOOT_PTR_OFFSET       (0x0400UL)     /**< Boot pointer in FCF             */
#define OTA_BANK_A_START          (0x00400000UL)
#define OTA_BANK_B_START          (0x00800000UL)
#define OTA_BANK_B_LOCKED_MARKER  (0xDEADC0DEUL)

/* Persistent OTA state in NVRAM-backed SRAM (battery-backed on S32G) */
#define S32G_STANDBY_RAM_BASE     (0x24000000UL)
#define OTA_STATE_SRAM_OFFSET     (0x0000UL)
#define OTA_BOOT_COUNT_OFFSET     (0x0004UL)
#define OTA_COMMIT_THRESHOLD      (25U)          /**< Boot cycles before Bank B committed */
#define OTA_STATE_MAGIC           (0x07A501BCUL)

#define RGM_REG32(offset)         (*((volatile uint32 *)(S32G_MC_RGM_BASE + (offset))))
#define SRAM_REG32(offset)        (*((volatile uint32 *)(S32G_STANDBY_RAM_BASE + (offset))))
#define FLASH_REG32(addr)         (*((volatile uint32 *)(addr)))

/*=====================================================================================
 * OTA State Machine Definitions
 *====================================================================================*/
typedef uint32 OTA_StateType;
#define OTA_STATE_BOOT_PENDING        (0x00000001UL)
#define OTA_STATE_BANK_A_ACTIVE       (0x00000002UL)
#define OTA_STATE_BANK_B_RUNNING      (0x00000003UL)
#define OTA_STATE_BANK_B_COMMITTED    (0x00000004UL)
#define OTA_STATE_ROLLBACK_TRIGGERED  (0x00000005UL)
#define OTA_STATE_BANK_B_LOCKED       (0x00000006UL)

/*=====================================================================================
 * Internal: Read OTA state from battery-backed SRAM
 *====================================================================================*/
static OTA_StateType OTA_ReadState(void)
{
    uint32 magic = SRAM_REG32(OTA_STATE_SRAM_OFFSET);
    OTA_StateType state;

    if (magic == OTA_STATE_MAGIC)
    {
        state = SRAM_REG32(OTA_STATE_SRAM_OFFSET + 4U);
    }
    else
    {
        state = OTA_STATE_BOOT_PENDING;
    }

    return state;
}

/*=====================================================================================
 * Internal: Write OTA state to battery-backed SRAM
 *====================================================================================*/
static void OTA_WriteState(OTA_StateType newState)
{
    SRAM_REG32(OTA_STATE_SRAM_OFFSET)       = OTA_STATE_MAGIC;
    SRAM_REG32(OTA_STATE_SRAM_OFFSET + 4U)  = (uint32)newState;
}

/*=====================================================================================
 * Internal: Check if hardware watchdog reset occurred this boot
 *====================================================================================*/
static boolean OTA_WasWatchdogReset(void)
{
    uint32 fes = RGM_REG32(MC_RGM_FES_OFFSET);
    return ((fes & MC_RGM_FES_WDOG_FLAG) != 0U) ? TRUE : FALSE;
}

/*=====================================================================================
 * Internal: Clear WDG reset flag
 *====================================================================================*/
static void OTA_ClearWatchdogResetFlag(void)
{
    /* Write 1 to clear the flag (W1C register) */
    RGM_REG32(MC_RGM_FES_OFFSET) = MC_RGM_FES_WDOG_FLAG;
}

/*=====================================================================================
 * Internal: Swap boot pointer back to Bank A (simplified FCF write)
 *  In production: uses MCAL Flash driver (Fls_Write) via AUTOSAR stack.
 *  Here: direct register write shown for architecture illustration.
 *====================================================================================*/
static void OTA_SwapBootToBank_A(void)
{
    /* Unlock FCF write sequence (NXP S32G Flash module specific) */
    /* Step 1: MCAL Flash unlock sequence */
    /* Step 2: Write Bank A start address to FCF boot pointer */
    /* Step 3: Re-lock FCF */

    /* Architecture representation — in production: Fls_Write(OTA_BOOT_PTR_OFFSET, BankA) */
    volatile uint32 * bootPtr =
        (volatile uint32 *)(S32G_PFLASH_FCF_BASE + OTA_BOOT_PTR_OFFSET);

    /* Dummy write for architecture completeness — real impl uses MCAL Fls */
    (void)bootPtr; /* Suppress unused-variable warning in reference build */
}

/*=====================================================================================
 * Internal: Lock Bank B sector to prevent re-use of corrupted firmware
 *====================================================================================*/
static void OTA_LockBankB_Sector(void)
{
    /* Write corrupted marker to Bank B header — prevents re-signature check */
    /* In production: Fls_Erase(OTA_BANK_B_START, SECTOR_SIZE) */
    volatile uint32 * bankBHeader = (volatile uint32 *)OTA_BANK_B_START;
    (void)bankBHeader; /* Suppress for reference build */

    SRAM_REG32(OTA_STATE_SRAM_OFFSET + 8U) = OTA_BANK_B_LOCKED_MARKER;
}

/*=====================================================================================
 * Internal: System Reset via MC_RGM Functional Reset
 *====================================================================================*/
static void OTA_TriggerSystemReset(void)
{
    /* MISRA C:2023 Rule 2.2 — no dead code: this function intentionally does not return */
    /* NXP S32G MC_RGM Functional Reset via Destructive Reset register.
     * Reference: NXP S32G2 Reference Manual Rev.3 §23.5.3 (MC_RGM_DRET)
     * MC_ME base: 0x402D8000 (confirmed from NXP S32G memory map §2.1)
     * Reset sequence: write MCTL register with key 0x5AF0 then 0xA50F
     * to request FUNC_RESET mode transition. */
    #define MC_ME_BASE        (0x402D8000UL)
    #define MC_ME_MCTL_OFFSET (0x00000004UL)  /**< Mode Control Register              */
    #define MC_ME_MCTL_KEY1   (0x00005AF0UL)  /**< First key: request FUNC_RESET       */
    #define MC_ME_MCTL_KEY2   (0x0000A50FUL)  /**< Inverted key: confirm request       */
    #define MC_ME_MCTL_FUNC_RESET (0x00000001UL) /**< FUNC_RESET mode encoding         */

    /* Step 1: Write mode request + first key */
    *((volatile uint32 *)(MC_ME_BASE + MC_ME_MCTL_OFFSET)) =
        (MC_ME_MCTL_FUNC_RESET | MC_ME_MCTL_KEY1);

    /* Step 2: Write mode request + inverted key (confirms request) */
    *((volatile uint32 *)(MC_ME_BASE + MC_ME_MCTL_OFFSET)) =
        (MC_ME_MCTL_FUNC_RESET | MC_ME_MCTL_KEY2);

    /* Spin-wait for reset (should never return) */
    for (;;)
    {
        /* Intentional infinite loop — functional reset imminent */
    }
}

/*=====================================================================================
 * OTA_RollbackStateMachine_Run
 *  Called once during early boot phase (before OS scheduler starts).
 *  Also called from WdgM MainFunction as a monitoring hook.
 *====================================================================================*/
void OTA_RollbackStateMachine_Run(void)
{
    OTA_StateType currentState;
    uint32        bootCount;
    boolean       wdgReset;

    currentState = OTA_ReadState();
    bootCount    = SRAM_REG32(OTA_BOOT_COUNT_OFFSET);
    wdgReset     = OTA_WasWatchdogReset();

    switch (currentState)
    {
        /*--------------------------------------------------------------
         * BOOT_PENDING: First boot — determine active bank
         *--------------------------------------------------------------*/
        case OTA_STATE_BOOT_PENDING:
        {
            /* Check if Bank B has a valid OTA marker */
            uint32 bankBMagic = FLASH_REG32(OTA_BANK_B_START);

            if ((bankBMagic != 0xFFFFFFFFUL) &&
                (bankBMagic != OTA_BANK_B_LOCKED_MARKER))
            {
                /* Bank B appears to contain new firmware — start trial boot */
                SRAM_REG32(OTA_BOOT_COUNT_OFFSET) = 1U;
                OTA_WriteState(OTA_STATE_BANK_B_RUNNING);
                OTA_ClearWatchdogResetFlag();
            }
            else
            {
                /* No valid Bank B firmware — remain on Bank A */
                OTA_WriteState(OTA_STATE_BANK_A_ACTIVE);
            }
            break;
        }

        /*--------------------------------------------------------------
         * BANK_B_RUNNING: Trial boot of Bank B firmware in progress
         *--------------------------------------------------------------*/
        case OTA_STATE_BANK_B_RUNNING:
        {
            if (wdgReset == TRUE)
            {
                /* Watchdog fired during Bank B boot — rollback triggered */
                OTA_WriteState(OTA_STATE_ROLLBACK_TRIGGERED);

                (void)Dem_SetEventStatus(DEM_EVENT_OTA_ROLLBACK_TRIGGERED,
                                          DEM_EVENT_STATUS_FAILED);
            }
            else
            {
                /* Healthy boot — increment trial counter */
                bootCount++;
                SRAM_REG32(OTA_BOOT_COUNT_OFFSET) = bootCount;

                if (bootCount >= OTA_COMMIT_THRESHOLD)
                {
                    /* Sufficient stable boots — commit Bank B as primary */
                    OTA_WriteState(OTA_STATE_BANK_B_COMMITTED);
                }
            }
            break;
        }

        /*--------------------------------------------------------------
         * ROLLBACK_TRIGGERED: Swap boot pointer and reset
         *--------------------------------------------------------------*/
        case OTA_STATE_ROLLBACK_TRIGGERED:
        {
            /* Swap FCF boot pointer to Bank A */
            OTA_SwapBootToBank_A();

            /* Lock corrupted Bank B sector */
            OTA_LockBankB_Sector();
            OTA_WriteState(OTA_STATE_BANK_B_LOCKED);

            /* Reset boot counter */
            SRAM_REG32(OTA_BOOT_COUNT_OFFSET) = 0U;

            /* System reset to boot from Bank A */
            OTA_TriggerSystemReset();
            /* No return */
            break;
        }

        /*--------------------------------------------------------------
         * BANK_B_COMMITTED: Bank B is now the stable firmware
         *--------------------------------------------------------------*/
        case OTA_STATE_BANK_B_COMMITTED:
        {
            /* No action needed — normal operation continues on Bank B */
            OTA_ClearWatchdogResetFlag();
            break;
        }

        /*--------------------------------------------------------------
         * BANK_A_ACTIVE / BANK_B_LOCKED: Stable Bank A operation
         *--------------------------------------------------------------*/
        case OTA_STATE_BANK_A_ACTIVE:
        case OTA_STATE_BANK_B_LOCKED:
        {
            OTA_ClearWatchdogResetFlag();
            break;
        }

        default:
        {
            /* Unknown state — reset to safe known state */
            OTA_WriteState(OTA_STATE_BANK_A_ACTIVE);
            SRAM_REG32(OTA_BOOT_COUNT_OFFSET) = 0U;
            break;
        }
    }
}

/*=====================================================================================
 * OTA_GetCurrentBankStatus
 *  Returns current OTA state for diagnostic UDS service (0x22 — ReadDataByIdentifier)
 *====================================================================================*/
uint32 OTA_GetCurrentBankStatus(void)
{
    return (uint32)OTA_ReadState();
}
