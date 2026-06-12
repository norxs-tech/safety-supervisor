/**
 * =====================================================================================
 * @file        s32g_m7_safety.ld
 * @brief       GNU LD Linker Script — NXP S32G Cortex-M7 Safety Core
 *              Partitions physical memory into MPU-enforced, AUTOSAR MemMap-aligned
 *              sections. Each safety subsystem (Safety Arbitrator, Vehicle Dynamics,
 *              Safe State Manager) occupies its own guarded RAM region.
 *
 *              Memory Map (NXP S32G Physical):
 *              ─────────────────────────────────────────────────────────────────────
 *              Region              Start        Size    Type        MPU Permission
 *              ─────────────────────────────────────────────────────────────────────
 *              M7 ITCM (Code)      0x00000000   256KB   Flash-exec  RX (M7 only)
 *              M7 DTCM (Data)      0x20000000   256KB   SRAM        RW (M7 only)
 *              AIPS SRAM (BSW)     0x24000000   256KB   SRAM        RW (M7+A53 via MPU)
 *              Shared SRAM (IPC)   0x24040000   64KB    SRAM-NC     RW (M7+A53)
 *              Calibration Flash   0x00400000   512KB   Flash-data  R  (M7+XCP)
 *              ─────────────────────────────────────────────────────────────────────
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11, ARM MPU Architecture
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 * =====================================================================================
 */

MEMORY
{
    /* M7 Instruction Tightly Coupled Memory — code execution, RX only */
    M7_ITCM     (RX)  : ORIGIN = 0x00000000, LENGTH = 256K

    /* M7 Data Tightly Coupled Memory — safety-critical variables, RW */
    M7_DTCM     (RW)  : ORIGIN = 0x20000000, LENGTH = 256K

    /* AIPS SRAM — BSW, DEM freeze frames, NvM mirrors */
    AIPS_SRAM   (RW)  : ORIGIN = 0x24000000, LENGTH = 192K

    /* Standby SRAM — battery-backed, OTA rollback state */
    STANDBY_SRAM (RW) : ORIGIN = 0x24030000, LENGTH = 64K

    /* Shared SRAM — non-cached IPC ring buffers (A53 ↔ M7) */
    SHARED_SRAM (RW)  : ORIGIN = 0x24040000, LENGTH = 64K

    /* Calibration Flash — XCP-accessible read-only parameters */
    CALIB_FLASH (R)   : ORIGIN = 0x00400000, LENGTH = 512K
}

/*=====================================================================================
 * Entry Point
 *====================================================================================*/
ENTRY(Reset_Handler)

SECTIONS
{
    /*------------------------------------------------------------------
     * Vector Table — must be at 0x00000000 for VTOR
     *------------------------------------------------------------------*/
    .vectors :
    {
        __isr_vector_start = .;
        KEEP(*(.isr_vector))
    } > M7_ITCM

    /*------------------------------------------------------------------
     * Safety Arbitrator Code — isolated section with MPU guard
     *------------------------------------------------------------------*/
    .safety_arbitrator_code :
    {
        *SwcSafetyArbitrator.o(.text*)
    } > M7_ITCM

    /*------------------------------------------------------------------
     * Vehicle Dynamics Code
     *------------------------------------------------------------------*/
    .vehicle_dynamics_code :
    {
        *SwcVehicleDynamics.o(.text*)
    } > M7_ITCM

    /*------------------------------------------------------------------
     * Safe State Manager Code
     *------------------------------------------------------------------*/
    .safe_state_mgr_code :
    {
        *SwcSafeStateMgr.o(.text*)
    } > M7_ITCM

    /*------------------------------------------------------------------
     * BSW Code (E2E, WdgM, CSM, DEM, IPC CDD)
     *------------------------------------------------------------------*/
    .bsw_code :
    {
        *E2E.o(.text*)
        *WdgM.o(.text*)
        *Csm.o(.text*)
        *IPC_RingBuffer.o(.text*)
        *OtaRollback.o(.text*)
    } > M7_ITCM

    /*------------------------------------------------------------------
     * General Code (remaining objects)
     *------------------------------------------------------------------*/
    .text :
    {
        *(.text*)
        *(.rodata*)
    } > M7_ITCM

    /* ARM exception unwind tables */
    .ARM.extab : { *(.ARM.extab*) } > M7_ITCM
    .ARM.exidx : { *(.ARM.exidx*) } > M7_ITCM

    /* Copy table for .data init */
    __data_load_addr = LOADADDR(.data);

    /*------------------------------------------------------------------
     * Safety Arbitrator BSS — guarded by MPU region 3
     *------------------------------------------------------------------*/
    .safety_arbitrator_bss (NOLOAD) :
    {
        . = ALIGN(32);
        __sa_bss_start = .;
        *SwcSafetyArbitrator.o(.bss*)
        *SwcSafetyArbitrator.o(COMMON)
        . = ALIGN(32);
        __sa_bss_end = .;
    } > M7_DTCM

    /*------------------------------------------------------------------
     * Vehicle Dynamics BSS — guarded by MPU region 4
     *------------------------------------------------------------------*/
    .vehicle_dynamics_bss (NOLOAD) :
    {
        . = ALIGN(32);
        __vd_bss_start = .;
        *SwcVehicleDynamics.o(.bss*)
        *SwcVehicleDynamics.o(COMMON)
        . = ALIGN(32);
        __vd_bss_end = .;
    } > M7_DTCM

    /*------------------------------------------------------------------
     * Safe State Manager BSS — guarded by MPU region 5
     *------------------------------------------------------------------*/
    .safe_state_mgr_bss (NOLOAD) :
    {
        . = ALIGN(32);
        __ssm_bss_start = .;
        *SwcSafeStateMgr.o(.bss*)
        *SwcSafeStateMgr.o(COMMON)
        . = ALIGN(32);
        __ssm_bss_end = .;
    } > M7_DTCM

    /*------------------------------------------------------------------
     * General BSS and initialized data
     *------------------------------------------------------------------*/
    .data :
    {
        . = ALIGN(4);
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > M7_DTCM AT > M7_ITCM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > M7_DTCM

    /*------------------------------------------------------------------
     * E2E CRC Lookup Table (ROM — placed in ITCM for fast access)
     *------------------------------------------------------------------*/
    .e2e_rom :
    {
        *E2E.o(.rodata*)
    } > M7_ITCM

    /*------------------------------------------------------------------
     * Calibration Data (XCP-accessible, Flash)
     *------------------------------------------------------------------*/
    .safety_arbitrator_calib :
    {
        . = ALIGN(4);
        *SwcSafetyArbitrator.o(.rodata.calib*)
        *SwcSafetyArbitrator.o(.rodata.*)
    } > CALIB_FLASH

    .vehicle_dynamics_calib :
    {
        . = ALIGN(4);
        *SwcVehicleDynamics.o(.rodata.*)
    } > CALIB_FLASH

    .safe_state_mgr_calib :
    {
        . = ALIGN(4);
        *SwcSafeStateMgr.o(.rodata.*)
    } > CALIB_FLASH

    /*------------------------------------------------------------------
     * Shared SRAM — IPC Ring Buffers (non-cached, no MPU write-protect)
     * Physically shared between A53 MMU and M7 MPU.
     *------------------------------------------------------------------*/
    .shared_sram (NOLOAD) :
    {
        . = ALIGN(64);
        __shared_sram_start = .;
        *IPC_RingBuffer.o(.bss*)
        *(.shared_sram)
        . = ALIGN(64);
        __shared_sram_end = .;
    } > SHARED_SRAM

    /*------------------------------------------------------------------
     * Stack (M7 DTCM — high end)
     *------------------------------------------------------------------*/
    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        __stack_limit = .;
        . += 0x4000;    /* 16KB M7 supervisor stack */
        __stack_top = .;
    } > M7_DTCM

    /*------------------------------------------------------------------
     * Heap (not used — MISRA C:2023 prohibits dynamic allocation)
     *------------------------------------------------------------------*/
    __heap_size = 0x0;
    HEAP_SIZE   = 0x0;
    /* 'end' required by libnosys _sbrk stub; heap size is zero so any
       dynamic allocation attempt fails immediately at the stack limit. */
    PROVIDE(end = __stack_limit);

    /*------------------------------------------------------------------
     * Debug symbols
     *------------------------------------------------------------------*/
    .debug_info     0 : { *(.debug_info) }
    .debug_abbrev   0 : { *(.debug_abbrev) }
    .debug_aranges  0 : { *(.debug_aranges) }
    .debug_line     0 : { *(.debug_line) }
    .debug_str      0 : { *(.debug_str) }
}
