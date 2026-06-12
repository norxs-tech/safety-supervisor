/**
 * =====================================================================================
 * @file        startup_s32g_m7.c
 * @brief       NXP S32G Cortex-M7 startup code and bare-metal cyclic OS scheduler.
 *
 *              Provides:
 *                1. ARMv7-M vector table (.isr_vector — placed at ITCM 0x00000000)
 *                2. Reset_Handler: .data copy, .bss + MPU-isolated SWC BSS zeroing
 *                3. SysTick-driven 1ms OS tick
 *                4. Static cyclic scheduler (rate-monotonic, non-preemptive):
 *                     5ms   — WdgM_MainFunction (hardware SWT service)
 *                     10ms  — VehicleDynamics → SafetyArbitrator → SafeStateMgr
 *                             + IPC RX/TX handling (supervised entity IPC_HANDLER)
 *                     100ms — SafetyArbitrator diagnostics + OTA rollback FSM
 *
 *              Scheduling rationale (ISO 26262-6 §7.4.6 — FTTI 20ms):
 *              The 10ms safety chain executes run-to-completion at the highest
 *              priority (no preemption exists). Worst-case fault reaction =
 *              one 10ms cycle (detection) + one 10ms cycle (safe state entry)
 *              = 20ms ≤ FTTI. AoU-1 in docs/HARA_ASS_SEooC_001.md.
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.1        2026-06-11  norxs-lab       Created (startup + scheduler remediation)
 * =====================================================================================
 */

#include <stdint.h>   /* int32_t for CMSIS-compatible cache API signatures */
#include "Platform_Types.h"
#include "WdgM.h"
#include "Dem.h"
#include "Csm.h"
#include "OtaRollback.h"
#include "Mpu_S32G_M7.h"
#include "Sbst.h"
#include "E2E.h"
#include "IPC_RingBuffer.h"
#include "Rte_SafetyArbitrator.h"
#include "SwcVehicleDynamics.h"
#include "SwcSafeStateMgr.h"

/*=====================================================================================
 * Linker-Provided Symbols (tools/s32g_m7_safety.ld)
 *====================================================================================*/
extern uint32 __stack_top[];
extern uint32 __data_load_addr[];
extern uint32 __data_start[];
extern uint32 __data_end[];
extern uint32 __bss_start[];
extern uint32 __bss_end[];
extern uint32 __sa_bss_start[];
extern uint32 __sa_bss_end[];
extern uint32 __vd_bss_start[];
extern uint32 __vd_bss_end[];
extern uint32 __ssm_bss_start[];
extern uint32 __ssm_bss_end[];

/*=====================================================================================
 * ARMv7-M SysTick Registers (ARM DDI 0403E §B3.3)
 *====================================================================================*/
#define SYST_CSR    (*((volatile uint32 *)0xE000E010UL))  /**< Control & Status        */
#define SYST_RVR    (*((volatile uint32 *)0xE000E014UL))  /**< Reload Value            */
#define SYST_CVR    (*((volatile uint32 *)0xE000E018UL))  /**< Current Value           */
#define SYST_CSR_ENABLE_TICKINT_CORECLK   (0x00000007UL)

/*=====================================================================================
 * ARMv7-M Coprocessor Access Control (FPU enable) — ARM DDI 0403E §B3.2.20
 *====================================================================================*/
#define SCB_CPACR   (*((volatile uint32 *)0xE000ED88UL))
#define SCB_CPACR_CP10_CP11_FULL          (0x00F00000UL)

/** S32G399A Cortex-M7 core clock (AoU-6: integrator must match clock tree config) */
#ifndef OS_CORE_CLOCK_HZ
#define OS_CORE_CLOCK_HZ    (400000000UL)
#endif
#define OS_TICK_HZ          (1000UL)                      /**< 1ms OS tick             */

/*=====================================================================================
 * Local Prototypes
 *====================================================================================*/
void Reset_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void Default_Handler(void);
int  main(void);

static void Os_InitRuntime(void);
static uint32 Os_AdvanceRelease(uint32 nextRelease, uint32 periodMs, uint32 now);
static void Os_Task10ms(void);
static void Os_Task100ms(void);
static void Os_IpcHandler10ms(void);
static void Os_CopyWord(uint32 * const dst, const uint32 * const src, uint32 words);
static void Os_ZeroWord(uint32 * const dst, uint32 words);

/*=====================================================================================
 * OS Tick (written only by SysTick_Handler, read by scheduler loop)
 *====================================================================================*/
static volatile uint32 Os_TickMs;

/*=====================================================================================
 * Stack-Smashing Protection (-fstack-protector-strong runtime hooks)
 *  Overrides the newlib stubs: a corrupted canary must drive the hardware safe
 *  state, not spin in _exit(). Guard value uses an address-derived constant;
 *  the integrator shall re-seed from the HSE TRNG during ECU production
 *  provisioning (AoU-7).
 *====================================================================================*/
uintptr_t __stack_chk_guard = 0xDEADC0DEUL;

void __stack_chk_fail(void);
void __stack_chk_fail(void)
{
    WdgM_PerformReset();
    for (;;)
    {
        __asm volatile ("wfi");
    }
}

/*=====================================================================================
 * Vector Table — ARMv7-M core exceptions (device IRQs default to Default_Handler)
 *====================================================================================*/
typedef union
{
    void (*Handler)(void);    /**< Exception handler entry                             */
    const void *Ptr;          /**< Initial stack pointer entry (vector 0)              */
} VectorEntryType;

__attribute__((section(".isr_vector"), used))
static const VectorEntryType Os_VectorTable[16U] =
{
    { .Ptr     = (const void *)__stack_top },   /*  0: Initial SP                      */
    { .Handler = Reset_Handler              },  /*  1: Reset                           */
    { .Handler = NMI_Handler                },  /*  2: NMI                             */
    { .Handler = HardFault_Handler          },  /*  3: HardFault                       */
    { .Handler = MemManage_Handler          },  /*  4: MemManage (MPU violation)       */
    { .Handler = BusFault_Handler           },  /*  5: BusFault                        */
    { .Handler = UsageFault_Handler         },  /*  6: UsageFault                      */
    { .Handler = Default_Handler            },  /*  7: Reserved                        */
    { .Handler = Default_Handler            },  /*  8: Reserved                        */
    { .Handler = Default_Handler            },  /*  9: Reserved                        */
    { .Handler = Default_Handler            },  /* 10: Reserved                        */
    { .Handler = SVC_Handler                },  /* 11: SVCall                          */
    { .Handler = Default_Handler            },  /* 12: DebugMonitor                    */
    { .Handler = Default_Handler            },  /* 13: Reserved                        */
    { .Handler = PendSV_Handler             },  /* 14: PendSV                          */
    { .Handler = SysTick_Handler            }   /* 15: SysTick                         */
};

/*=====================================================================================
 * Reset_Handler — C runtime initialization, then jump to main()
 *====================================================================================*/
void Reset_Handler(void)
{
    /* 0. Enable FPU (CPACR CP10/CP11 full access) BEFORE any other code.
     *    The image is compiled with -mfloat-abi=hard: the compiler may emit FP
     *    instructions (including FP register spills) anywhere, so executing a
     *    single function without this enable would raise UsageFault on real
     *    silicon. Verified again later by Sbst_Run() (SBST_E_FPU). */
    SCB_CPACR |= SCB_CPACR_CP10_CP11_FULL;
    __asm volatile ("dsb 0xF" ::: "memory");
    __asm volatile ("isb 0xF" ::: "memory");

    /* Linker-symbol pointer arithmetic below is the canonical ARM startup idiom.
     * The symbols are guaranteed by tools/s32g_m7_safety.ld to delimit the same
     * contiguous memory region; ISO C object-identity rules do not apply to
     * linker-provided bounds. Deviation DEV-007 (see tools/cppcheck_suppressions.txt).
     */

    /* 1. Copy .data from ITCM load address to DTCM run address */
    /* cppcheck-suppress comparePointers */
    Os_CopyWord(__data_start, __data_load_addr, (uint32)(__data_end - __data_start));

    /* 2. Zero general .bss */
    /* cppcheck-suppress comparePointers */
    Os_ZeroWord(__bss_start, (uint32)(__bss_end - __bss_start));

    /* 3. Zero MPU-isolated SWC BSS sections (NOLOAD — startup responsibility) */
    /* cppcheck-suppress comparePointers */
    Os_ZeroWord(__sa_bss_start,  (uint32)(__sa_bss_end  - __sa_bss_start));
    /* cppcheck-suppress comparePointers */
    Os_ZeroWord(__vd_bss_start,  (uint32)(__vd_bss_end  - __vd_bss_start));
    /* cppcheck-suppress comparePointers */
    Os_ZeroWord(__ssm_bss_start, (uint32)(__ssm_bss_end - __ssm_bss_start));

    /* 4. Memory barrier before first function call into zeroed regions */
    __asm volatile ("dsb 0xF" ::: "memory");

    (void)main();

    /* main() never returns; trap defensively (ISO 26262-6 §7.4.13) */
    for (;;)
    {
        __asm volatile ("wfi");
    }
}

/*=====================================================================================
 * Exception Handlers — all faults force safe state via watchdog expiry
 *  Rationale: on an unrecoverable core fault, the safest action is to STOP
 *  servicing the SWT so the external hardware path drives actuators to safe state.
 *====================================================================================*/
void NMI_Handler(void)        { WdgM_PerformReset(); for (;;) { __asm volatile ("wfi"); } }
void HardFault_Handler(void)  { WdgM_PerformReset(); for (;;) { __asm volatile ("wfi"); } }
void MemManage_Handler(void)  { WdgM_PerformReset(); for (;;) { __asm volatile ("wfi"); } }
void BusFault_Handler(void)   { WdgM_PerformReset(); for (;;) { __asm volatile ("wfi"); } }
void UsageFault_Handler(void) { WdgM_PerformReset(); for (;;) { __asm volatile ("wfi"); } }
void SVC_Handler(void)        { /* No supervisor calls in bare-metal scheduler */ }
void PendSV_Handler(void)     { /* No context switching in run-to-completion model */ }
void Default_Handler(void)    { WdgM_PerformReset(); for (;;) { __asm volatile ("wfi"); } }

/*=====================================================================================
 * SysTick_Handler — 1ms OS tick
 *====================================================================================*/
void SysTick_Handler(void)
{
    Os_TickMs++;
}

/*=====================================================================================
 * main — runtime init + static cyclic scheduler
 *====================================================================================*/
int main(void)
{
    /* Per-task release counters: a task runs when (now - nextRelease) >= 0
     * (wrap-safe signed comparison). Unlike a `tick % period` scheme, no
     * activation is silently lost if the loop is delayed past a tick boundary —
     * a missed release is detected as an overrun and reported to DEM, and the
     * release point advances by whole periods so the schedule re-synchronizes
     * instead of bursting (ISO 26262-6 §7.4.6 temporal monitoring). */
    uint32 next5ms;
    uint32 next10ms;
    uint32 next100ms;

    Os_InitRuntime();

    next5ms   = Os_TickMs + 5U;
    next10ms  = Os_TickMs + 10U;
    next100ms = Os_TickMs + 100U;

    for (;;)
    {
        const uint32 now = Os_TickMs;

        if ((sint32)(now - next5ms) >= 0)
        {
            WdgM_MainFunction();
            next5ms = Os_AdvanceRelease(next5ms, 5U, now);
        }

        if ((sint32)(now - next10ms) >= 0)
        {
            Os_Task10ms();
            next10ms = Os_AdvanceRelease(next10ms, 10U, now);
        }

        if ((sint32)(now - next100ms) >= 0)
        {
            Os_Task100ms();
            next100ms = Os_AdvanceRelease(next100ms, 100U, now);
        }

        __asm volatile ("wfi");
    }
}

/*=====================================================================================
 * Os_AdvanceRelease — advance a task's next-release point past `now`
 *  Returns the next release tick. If more than one period elapsed, the missed
 *  activation(s) are reported as DEM_EVENT_OS_TASK_OVERRUN (debounced).
 *====================================================================================*/
static uint32 Os_AdvanceRelease(uint32 nextRelease, uint32 periodMs, uint32 now)
{
    uint32  next    = nextRelease + periodMs;
    boolean overrun = FALSE;

    while ((sint32)(now - next) >= 0)
    {
        next   += periodMs;
        overrun = TRUE;
    }

    if (overrun == TRUE)
    {
        (void)Dem_SetEventStatus(DEM_EVENT_OS_TASK_OVERRUN,
                                 DEM_EVENT_STATUS_PREFAILED);
    }

    return next;
}

/*=====================================================================================
 * Os_InitRuntime — one-shot BSW + SWC initialization sequence
 *====================================================================================*/
static void Os_InitRuntime(void)
{
    /* 1. Spatial FFI first: program and enable the MPU (ISO 26262-6 §7.4.9) */
    (void)Mpu_Init();

    /* 2. Startup self-test: RAM march, vector table, FPU. A failed self-test
     *    means the hardware cannot be trusted to host the safety function —
     *    do not start the scheduler; let the SWT expire into the hardware
     *    safe state (ISO 26262-5 Table D.1 diagnostics at power-up). */
    if (Sbst_Run() != SBST_OK)
    {
        (void)Dem_SetEventStatus(DEM_EVENT_SBST_FAILURE, DEM_EVENT_STATUS_FAILED);
        WdgM_PerformReset();
        for (;;)
        {
            __asm volatile ("wfi");
        }
    }

    /* 3. BSW: supervision, diagnostics, crypto, IPC */
    (void)WdgM_Init();
    (void)Csm_Init();
    (void)IPC_RingBuffer_Init(IPC_CHANNEL_A53_TO_M7);
    (void)IPC_RingBuffer_Init(IPC_CHANNEL_M7_TO_A53);

    /* 4. SWC init runnables */
    Rte_Runnable_SafetyArbitrator_Init();
    VD_Init();
    Rte_Runnable_SafeStateMgr_Init();

    /* 5. Start 1ms SysTick last — scheduling begins only after full init */
    SYST_RVR = (uint32)((OS_CORE_CLOCK_HZ / OS_TICK_HZ) - 1UL);
    SYST_CVR = 0U;
    SYST_CSR = SYST_CSR_ENABLE_TICKINT_CORECLK;
}

/*=====================================================================================
 * Os_Task10ms — safety chain, run-to-completion, fixed execution order
 *====================================================================================*/
static void Os_Task10ms(void)
{
    Os_IpcHandler10ms();
    Rte_Runnable_VehicleDynamics_10ms();
    Rte_Runnable_SafetyArbitrator_10ms();
    Rte_Runnable_SafeStateMgr_10ms();
}

/*=====================================================================================
 * Os_Task100ms — diagnostics and OTA background FSM
 *====================================================================================*/
static void Os_Task100ms(void)
{
    Rte_Runnable_SafetyArbitrator_Diag_100ms();
    OTA_RollbackStateMachine_Run();
}

/*=====================================================================================
 * Os_IpcHandler10ms — supervised entity WDGM_SE_IPC_HANDLER
 *  Drains the A53→M7 channel; only E2E-validated AI command frames reach the RTE.
 *====================================================================================*/
static void Os_IpcHandler10ms(void)
{
    IPC_FrameType frame;
    uint32        budget = IPC_RING_BUFFER_SIZE;

    (void)WdgM_CheckpointReached(WDGM_SE_IPC_HANDLER, WDGM_CP_IPC_RX);

    while (budget > 0U)
    {
        const IPC_ReturnType rd = IPC_RingBuffer_Read(IPC_CHANNEL_A53_TO_M7, &frame);

        if (rd == IPC_OK)
        {
            if (((IPC_FrameTypeId)frame.FrameType == IPC_FRAME_AI_STEER_CMD) ||
                ((IPC_FrameTypeId)frame.FrameType == IPC_FRAME_AI_ACCEL_CMD))
            {
                if ((uint16)frame.PayloadLength >= (uint16)sizeof(Rte_AiCommandType))
                {
                    Rte_AiCommandType aiCmd;
                    uint8            *dst = (uint8 *)&aiCmd;
                    uint32            i;

                    for (i = 0U; i < (uint32)sizeof(Rte_AiCommandType); i++)
                    {
                        dst[i] = frame.Payload[i];
                    }
                    Rte_UpdateAiCommandFromIpc(&aiCmd);
                }
            }
        }
        else
        {
            if (rd == IPC_E_CRC)
            {
                (void)Dem_SetEventStatus(DEM_EVENT_IPC_E2E_ERROR,
                                         DEM_EVENT_STATUS_PREFAILED);
            }
            break;  /* IPC_E_EMPTY or fault — stop draining this cycle */
        }

        budget--;
    }

    (void)WdgM_CheckpointReached(WDGM_SE_IPC_HANDLER, WDGM_CP_IPC_TX);
}

/*=====================================================================================
 * ARMv7-M Cache Maintenance Operations (ARM DDI 0403E §B2.2)
 *  Referenced by the IPC ring buffer CDD for A53↔M7 shared SRAM coherency.
 *  Equivalent to CMSIS-Core SCB_CleanDCache_by_Addr / SCB_InvalidateDCache_by_Addr.
 *====================================================================================*/
#define SCB_DCCMVAC (*((volatile uint32 *)0xE000EF68UL))  /**< D-cache clean by MVA    */
#define SCB_DCIMVAC (*((volatile uint32 *)0xE000EF5CUL))  /**< D-cache invalidate MVA  */
#define M7_DCACHE_LINE_BYTES   (32UL)

void SCB_CleanDCache_by_Addr(volatile void *addr, int32_t dsize);
void SCB_InvalidateDCache_by_Addr(volatile void *addr, int32_t dsize);

void SCB_CleanDCache_by_Addr(volatile void *addr, int32_t dsize)
{
    if ((addr != (volatile void *)0) && (dsize > 0))
    {
        uint32       mva    = (uint32)addr & ~(M7_DCACHE_LINE_BYTES - 1UL);
        const uint32 endMva = (uint32)addr + (uint32)dsize;

        __asm volatile ("dsb 0xF" ::: "memory");
        while (mva < endMva)
        {
            SCB_DCCMVAC = mva;
            mva += M7_DCACHE_LINE_BYTES;
        }
        __asm volatile ("dsb 0xF" ::: "memory");
        __asm volatile ("isb 0xF" ::: "memory");
    }
}

void SCB_InvalidateDCache_by_Addr(volatile void *addr, int32_t dsize)
{
    if ((addr != (volatile void *)0) && (dsize > 0))
    {
        uint32       mva    = (uint32)addr & ~(M7_DCACHE_LINE_BYTES - 1UL);
        const uint32 endMva = (uint32)addr + (uint32)dsize;

        __asm volatile ("dsb 0xF" ::: "memory");
        while (mva < endMva)
        {
            SCB_DCIMVAC = mva;
            mva += M7_DCACHE_LINE_BYTES;
        }
        __asm volatile ("dsb 0xF" ::: "memory");
        __asm volatile ("isb 0xF" ::: "memory");
    }
}

/*=====================================================================================
 * Word-wise copy / zero helpers (no libc dependency — -ffreestanding)
 *====================================================================================*/
static void Os_CopyWord(uint32 * const dst, const uint32 * const src, uint32 words)
{
    uint32 i;
    for (i = 0U; i < words; i++)
    {
        dst[i] = src[i];
    }
}

static void Os_ZeroWord(uint32 * const dst, uint32 words)
{
    uint32 i;
    for (i = 0U; i < words; i++)
    {
        dst[i] = 0U;
    }
}
