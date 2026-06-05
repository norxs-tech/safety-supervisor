/**
 * =====================================================================================
 * @file        IPC_RingBuffer.c
 * @brief       Lock-Free Shared SRAM IPC Ring Buffer — CDD Implementation
 *              Atomic LDREX/STREX based single-producer/single-consumer design.
 *              Cache maintenance: __clean_dcache_by_addr (producer) and
 *              __invalidate_dcache_by_addr (consumer) per ARM CMSIS-Core API.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_CDD
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "IPC_RingBuffer.h"
#include "E2E.h"
#include <stdint.h>    /* int32_t — required for host (UNIT_TEST) builds */

/*=====================================================================================
 * CMSIS-Core cache maintenance intrinsics (ARM Cortex-M7 with D-Cache)
 * These map to SCB_CleanDCache_by_Addr / SCB_InvalidateDCache_by_Addr
 *====================================================================================*/
#ifdef UNIT_TEST_BUILD
/* Host stubs: ARM Cortex-M7 cache/barrier intrinsics replaced for x86_64 unit tests */
static inline void SCB_CleanDCache_by_Addr(volatile void *a, int32_t s) { (void)a; (void)s; }
static inline void SCB_InvalidateDCache_by_Addr(volatile void *a, int32_t s) { (void)a; (void)s; }
#define IPC_DSB()   __asm volatile ("" ::: "memory")
#define IPC_DMB()   __asm volatile ("" ::: "memory")
#else
extern void SCB_CleanDCache_by_Addr(volatile void *addr, int32_t dsize);
extern void SCB_InvalidateDCache_by_Addr(volatile void *addr, int32_t dsize);
#define IPC_DSB()   __asm volatile ("dsb 0xF" ::: "memory")
#define IPC_DMB()   __asm volatile ("dmb 0xF" ::: "memory")
#endif
/* Compiler barrier — prevent reordering across this point */
#define IPC_COMPILER_BARRIER()  __asm volatile ("" ::: "memory")

/*=====================================================================================
 * Shared SRAM Ring Buffer Instances (one per channel)
 * Placed in .shared_sram section — visible to both A53 and M7 cores.
 * MPU on M7 grants read-write to this region; A53 MMU maps same physical address.
 *====================================================================================*/
#define IPC_START_SEC_VAR_SHARED
#include "MemMap.h"

static volatile IPC_RingBufferType IPC_Buffers[IPC_NUM_CHANNELS];

#define IPC_STOP_SEC_VAR_SHARED
#include "MemMap.h"

/*=====================================================================================
 * E2E Profile 22 Configuration for IPC frames
 *====================================================================================*/
#define IPC_START_SEC_CODE
#include "MemMap.h"

static const E2E_P22ConfigType IPC_E2E_Config =
{
    /* DataIDList: unique per-slot IDs for counter-indexed CRC mixing */
    .DataIDList = {
        0x1AU, 0x2BU, 0x3CU, 0x4DU, 0x5EU, 0x6FU, 0x7AU, 0x8BU,
        0x9CU, 0xADU, 0xBEU, 0xCFU, 0xD1U, 0xE2U, 0xF3U, 0x04U
    },
    .Offset         = 0U,
    .DataLength     = 16U,   /* bits — covers CRC byte + counter nibble byte */
    .MaxDeltaCounter = 1U
};

/*=====================================================================================
 * IPC_RingBuffer_Init
 *====================================================================================*/
IPC_ReturnType IPC_RingBuffer_Init(uint8 Channel)
{
    IPC_ReturnType retVal = IPC_E_PARAM;
    uint32         slotIdx;

    if (Channel < IPC_NUM_CHANNELS)
    {
        /* Reset producer / consumer indices */
        IPC_Buffers[Channel].WriteIdx = 0U;
        IPC_Buffers[Channel].ReadIdx  = 0U;

        /* Zero-fill all slots */
        for (slotIdx = 0U; slotIdx < IPC_RING_BUFFER_SIZE; slotIdx++)
        {
            uint32 byteIdx;
            IPC_Buffers[Channel].Slots[slotIdx].Magic          = 0U;
            IPC_Buffers[Channel].Slots[slotIdx].SequenceNumber = 0U;
            IPC_Buffers[Channel].Slots[slotIdx].PayloadLength  = 0U;
            IPC_Buffers[Channel].Slots[slotIdx].FrameType      = 0U;
            IPC_Buffers[Channel].Slots[slotIdx].Timestamp_us   = 0U;
            IPC_Buffers[Channel].Slots[slotIdx].E2E_CRC        = 0U;
            IPC_Buffers[Channel].Slots[slotIdx].E2E_Counter    = 0U;

            for (byteIdx = 0U; byteIdx < IPC_FRAME_PAYLOAD_BYTES; byteIdx++)
            {
                IPC_Buffers[Channel].Slots[slotIdx].Payload[byteIdx] = 0U;
            }
        }

        /* Clean entire buffer to shared SRAM so other core sees zeros */
        IPC_DMB();
        SCB_CleanDCache_by_Addr(
            (volatile void *)&IPC_Buffers[Channel],
            (sint32)sizeof(IPC_RingBufferType));
        IPC_DSB();

        retVal = IPC_OK;
    }

    return retVal;
}

/*=====================================================================================
 * IPC_RingBuffer_Write (Single-Producer, Lock-Free)
 *  1. Load WriteIdx
 *  2. Check not full (WriteIdx - ReadIdx < SIZE)
 *  3. Copy frame into slot[WriteIdx & MASK]
 *  4. Cache clean (this core's D-Cache → shared SRAM)
 *  5. DMB
 *  6. Atomic increment WriteIdx (LDREX/STREX loop)
 *====================================================================================*/
IPC_ReturnType IPC_RingBuffer_Write(
    uint8               Channel,
    const IPC_FrameType * const Frame)
{
    IPC_ReturnType retVal    = IPC_E_PARAM;
    uint32         writeIdx;
    uint32         readIdx;
    uint32         slotIdx;
    uint32         newWriteIdx;
    uint32         byteIdx;
    uint32         cas_result;

    if ((Channel >= IPC_NUM_CHANNELS) || (Frame == NULL_PTR))
    {
        retVal = IPC_E_PARAM;
    }
    else
    {
        /* Snapshot indices (compiler barrier prevents hoisting) */
        IPC_COMPILER_BARRIER();
        writeIdx = IPC_Buffers[Channel].WriteIdx;
        readIdx  = IPC_Buffers[Channel].ReadIdx;
        IPC_COMPILER_BARRIER();

        if ((writeIdx - readIdx) >= IPC_RING_BUFFER_SIZE)
        {
            /* Buffer full — producer must back-pressure */
            retVal = IPC_E_FULL;
        }
        else
        {
            slotIdx = writeIdx & IPC_RING_BUFFER_SIZE_MASK;

            /* Copy frame fields into volatile slot */
            IPC_Buffers[Channel].Slots[slotIdx].Magic          = Frame->Magic;
            IPC_Buffers[Channel].Slots[slotIdx].SequenceNumber = Frame->SequenceNumber;
            IPC_Buffers[Channel].Slots[slotIdx].PayloadLength  = Frame->PayloadLength;
            IPC_Buffers[Channel].Slots[slotIdx].FrameType      = Frame->FrameType;
            IPC_Buffers[Channel].Slots[slotIdx].Timestamp_us   = Frame->Timestamp_us;
            IPC_Buffers[Channel].Slots[slotIdx].E2E_CRC        = Frame->E2E_CRC;
            IPC_Buffers[Channel].Slots[slotIdx].E2E_Counter    = Frame->E2E_Counter;

            for (byteIdx = 0U; byteIdx < IPC_FRAME_PAYLOAD_BYTES; byteIdx++)
            {
                IPC_Buffers[Channel].Slots[slotIdx].Payload[byteIdx] =
                    Frame->Payload[byteIdx];
            }

            /* Clean D-Cache: push this slot to shared SRAM */
            SCB_CleanDCache_by_Addr(
                (volatile void *)&IPC_Buffers[Channel].Slots[slotIdx],
                (sint32)sizeof(IPC_FrameType));

            /* DMB: ensure slot data visible before WriteIdx update */
            IPC_DMB();

            /* Atomic increment WriteIdx using LDREX/STREX (ARM Cortex-M7).
             * Pattern: load-exclusive → verify loaded == expected → store-exclusive.
             * 3 distinct registers required per ARM Architecture Reference Manual:
             *   %0 = strex result (0=success, 1=fail) — status register
             *   %1 = address of WriteIdx
             *   %2 = new value to store
             * Note: For single-producer design, the LDREX loaded value is not
             * compared (no other writer exists). The exclusive monitor is used
             * solely to guarantee the store is atomic with respect to cache
             * line ownership across AXI bus on S32G. */
            newWriteIdx = writeIdx + 1U;
            do
            {
                uint32 tmp_loaded; /* Loaded value — discarded (single producer) */
#ifdef UNIT_TEST_BUILD
                    IPC_Buffers[Channel].WriteIdx = newWriteIdx;
                    cas_result = 0U;
                    (void)tmp_loaded;
#else
                    __asm volatile (
                        "ldrex %[loaded], [%[addr]]   \n"
                        "strex %[result], %[newval], [%[addr]] \n"
                        : [result] "=&r"(cas_result),
                          [loaded] "=&r"(tmp_loaded)
                        : [addr]   "r"(&IPC_Buffers[Channel].WriteIdx),
                          [newval] "r"(newWriteIdx)
                        : "memory"
                    );
#endif
            } while (cas_result != 0U);

            IPC_DSB();

            retVal = IPC_OK;
        }
    }

    return retVal;
}

/*=====================================================================================
 * IPC_RingBuffer_Read (Single-Consumer, Lock-Free)
 *  1. Load ReadIdx
 *  2. Check not empty (WriteIdx != ReadIdx)
 *  3. Invalidate D-Cache for slot (force reload from shared SRAM)
 *  4. Copy slot data to local frame
 *  5. Verify Magic and E2E Profile 22
 *  6. Atomic increment ReadIdx
 *====================================================================================*/
IPC_ReturnType IPC_RingBuffer_Read(
    uint8          Channel,
    IPC_FrameType * const Frame)
{
    IPC_ReturnType       retVal   = IPC_E_PARAM;
    uint32               readIdx;
    uint32               writeIdx;
    uint32               slotIdx;
    uint32               newReadIdx;
    uint32               byteIdx;
    uint32               cas_result;
    E2E_P22CheckStateType e2eState;
    E2E_PCheckStatusType  e2eStatus;
    uint8                 e2eData[2U];

    if ((Channel >= IPC_NUM_CHANNELS) || (Frame == NULL_PTR))
    {
        retVal = IPC_E_PARAM;
    }
    else
    {
        IPC_COMPILER_BARRIER();
        writeIdx = IPC_Buffers[Channel].WriteIdx;
        readIdx  = IPC_Buffers[Channel].ReadIdx;
        IPC_COMPILER_BARRIER();

        if (writeIdx == readIdx)
        {
            retVal = IPC_E_EMPTY;
        }
        else
        {
            slotIdx = readIdx & IPC_RING_BUFFER_SIZE_MASK;

            /* Invalidate D-Cache: force reload from shared SRAM */
            SCB_InvalidateDCache_by_Addr(
                (volatile void *)&IPC_Buffers[Channel].Slots[slotIdx],
                (sint32)sizeof(IPC_FrameType));
            IPC_DMB();

            /* Copy volatile slot → local frame */
            Frame->Magic          = IPC_Buffers[Channel].Slots[slotIdx].Magic;
            Frame->SequenceNumber = IPC_Buffers[Channel].Slots[slotIdx].SequenceNumber;
            Frame->PayloadLength  = IPC_Buffers[Channel].Slots[slotIdx].PayloadLength;
            Frame->FrameType      = IPC_Buffers[Channel].Slots[slotIdx].FrameType;
            Frame->Timestamp_us   = IPC_Buffers[Channel].Slots[slotIdx].Timestamp_us;
            Frame->E2E_CRC        = IPC_Buffers[Channel].Slots[slotIdx].E2E_CRC;
            Frame->E2E_Counter    = IPC_Buffers[Channel].Slots[slotIdx].E2E_Counter;

            for (byteIdx = 0U; byteIdx < IPC_FRAME_PAYLOAD_BYTES; byteIdx++)
            {
                Frame->Payload[byteIdx] =
                    IPC_Buffers[Channel].Slots[slotIdx].Payload[byteIdx];
            }

            /* Verify magic header */
            if (Frame->Magic != IPC_MAGIC_HEADER)
            {
                retVal = IPC_E_CRC;
            }
            else
            {
                /* E2E Profile 22 check over CRC byte + counter byte */
                e2eData[0U] = (uint8)(Frame->E2E_CRC & 0xFFU);
                e2eData[1U] = (uint8)((Frame->E2E_Counter & E2E_P22_COUNTER_MASK) << 4U);

                e2eState.Counter          = Frame->E2E_Counter;
                e2eState.OkCount          = 0U;
                e2eState.ErrorCount       = 0U;
                e2eState.NewDataAvailable = FALSE;

                (void)E2E_P22Check(&IPC_E2E_Config, &e2eState, e2eData, &e2eStatus);

                if ((e2eStatus == E2E_P_STATUS_OK) ||
                    (e2eStatus == E2E_P_STATUS_REPEATED))
                {
                    /* Atomic increment ReadIdx */
                    newReadIdx = readIdx + 1U;
                    do
                    {
                        uint32 tmp_loaded;
#ifdef UNIT_TEST_BUILD
                        IPC_Buffers[Channel].ReadIdx = newReadIdx;
                        cas_result = 0U;
                        (void)tmp_loaded;
#else
                        __asm volatile (
                            "ldrex %[loaded], [%[addr]]            \n"
                            "strex %[result], %[newval], [%[addr]] \n"
                            : [result] "=&r"(cas_result),
                              [loaded] "=&r"(tmp_loaded)
                            : [addr]   "r"(&IPC_Buffers[Channel].ReadIdx),
                              [newval] "r"(newReadIdx)
                            : "memory"
                        );
#endif
                    } while (cas_result != 0U);

                    IPC_DSB();
                    retVal = IPC_OK;
                }
                else
                {
                    retVal = IPC_E_CRC;
                }
            }
        }
    }

    return retVal;
}

/*=====================================================================================
 * IPC_RingBuffer_GetCount
 *====================================================================================*/
uint32 IPC_RingBuffer_GetCount(uint8 Channel)
{
    uint32 count = 0U;

    if (Channel < IPC_NUM_CHANNELS)
    {
        IPC_COMPILER_BARRIER();
        count = IPC_Buffers[Channel].WriteIdx - IPC_Buffers[Channel].ReadIdx;
        IPC_COMPILER_BARRIER();
    }

    return count;
}

#define IPC_STOP_SEC_CODE
#include "MemMap.h"
