/**
 * =====================================================================================
 * @file        IPC_RingBuffer.h
 * @brief       Lock-Free Shared SRAM IPC Ring Buffer — Complex Device Driver (CDD)
 *              Provides deterministic, mutex-free cross-core communication between
 *              the QNX/A53 network processor and the AUTOSAR/M7 safety core.
 *              Uses atomic operations and CMSIS memory barriers to guarantee
 *              cache coherency across ARM Cortex-A53 (64-bit) and M7 (32-bit) domains.
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

#ifndef IPC_RINGBUFFER_H
#define IPC_RINGBUFFER_H

#include "Platform_Types.h"

/*=====================================================================================
 * IPC Ring Buffer Configuration
 *====================================================================================*/
#define IPC_RING_BUFFER_SIZE        (16U)   /**< Number of slots (must be power of 2)   */
#define IPC_RING_BUFFER_SIZE_MASK   (IPC_RING_BUFFER_SIZE - 1U)
#define IPC_FRAME_PAYLOAD_BYTES     (64U)   /**< Max payload per frame in bytes          */
#define IPC_MAGIC_HEADER            (0xA5C3B7E1UL) /**< Frame integrity magic word       */
#define IPC_CHANNEL_A53_TO_M7       (0U)    /**< A53→M7 command channel index            */
#define IPC_CHANNEL_M7_TO_A53       (1U)    /**< M7→A53 status/telemetry channel index   */
#define IPC_NUM_CHANNELS            (2U)

/*=====================================================================================
 * IPC Frame Structure (placed in Shared SRAM)
 * Alignment to 32-byte boundary for cache line efficiency
 *====================================================================================*/
typedef struct
{
    volatile uint32 Magic;                          /**< 0xA5C3B7E1 validity stamp       */
    volatile uint32 SequenceNumber;                 /**< Monotonic frame counter          */
    volatile uint16 PayloadLength;                  /**< Actual used bytes in Payload     */
    volatile uint16 FrameType;                      /**< Command type identifier          */
    volatile uint32 Timestamp_us;                   /**< Microsecond timestamp (GPT)      */
    volatile uint8  Payload[IPC_FRAME_PAYLOAD_BYTES]; /**< E2E-protected data payload    */
    volatile uint16 E2E_CRC;                        /**< E2E CRC-16 checksum             */
    volatile uint8  E2E_Counter;                    /**< E2E sliding counter             */
    volatile uint8  Reserved[1U];                   /**< Pad to 4-byte alignment          */
} __attribute__((aligned(32))) IPC_FrameType;

/*=====================================================================================
 * IPC Ring Buffer Control Block (Shared SRAM — cache-line isolated)
 *====================================================================================*/
typedef struct
{
    volatile uint32  WriteIdx;                      /**< Producer write index (atomic)   */
    volatile uint8   _pad_write[60U];               /**< Pad to separate cache lines     */
    volatile uint32  ReadIdx;                       /**< Consumer read index (atomic)    */
    volatile uint8   _pad_read[60U];                /**< Pad to separate cache lines     */
    IPC_FrameType    Slots[IPC_RING_BUFFER_SIZE];   /**< Ring buffer slot array          */
} __attribute__((aligned(64))) IPC_RingBufferType;

/*=====================================================================================
 * IPC Return Codes
 *====================================================================================*/
typedef uint8 IPC_ReturnType;
#define IPC_OK              ((IPC_ReturnType)0x00U)
#define IPC_E_FULL          ((IPC_ReturnType)0x01U)  /**< Ring buffer is full            */
#define IPC_E_EMPTY         ((IPC_ReturnType)0x02U)  /**< Ring buffer is empty           */
#define IPC_E_PARAM         ((IPC_ReturnType)0x03U)  /**< Invalid parameter              */
#define IPC_E_CRC           ((IPC_ReturnType)0x04U)  /**< E2E CRC check failed           */

/*=====================================================================================
 * Frame Type Definitions
 *====================================================================================*/
typedef uint16 IPC_FrameTypeId;
#define IPC_FRAME_AI_STEER_CMD      ((IPC_FrameTypeId)0x0001U) /**< AI steer command    */
#define IPC_FRAME_AI_ACCEL_CMD      ((IPC_FrameTypeId)0x0002U) /**< AI accel/brake cmd  */
#define IPC_FRAME_SAFETY_STATUS     ((IPC_FrameTypeId)0x0101U) /**< M7 safety status    */
#define IPC_FRAME_DIAG_EVENT        ((IPC_FrameTypeId)0x0201U) /**< DEM event report    */

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Initialize IPC ring buffer channel. Resets read/write indices and clears slots.
 *         Must be called from the producer core before any writes.
 * @param  Channel  [in]  Channel index (IPC_CHANNEL_A53_TO_M7 or IPC_CHANNEL_M7_TO_A53).
 * @return IPC_OK on success, IPC_E_PARAM if channel index invalid.
 */
extern IPC_ReturnType IPC_RingBuffer_Init(uint8 Channel);

/**
 * @brief  Atomically write one frame into the ring buffer (lock-free, single producer).
 *         Performs cache clean after write to ensure A53/M7 coherency.
 * @param  Channel  [in]  Channel index.
 * @param  Frame    [in]  Pointer to frame to be written.
 * @return IPC_OK on success, IPC_E_FULL if no slot available, IPC_E_PARAM if invalid.
 * @note   MISRA C:2023 Rule 11.4 — Pointer cast to volatile is intentional for SRAM.
 */
extern IPC_ReturnType IPC_RingBuffer_Write(
    uint8               Channel,
    const IPC_FrameType * const Frame);

/**
 * @brief  Atomically read one frame from the ring buffer (lock-free, single consumer).
 *         Performs cache invalidate before read to ensure fresh data from A53.
 * @param  Channel  [in]  Channel index.
 * @param  Frame    [out] Destination frame buffer.
 * @return IPC_OK on success, IPC_E_EMPTY if no data, IPC_E_CRC if E2E failed.
 */
extern IPC_ReturnType IPC_RingBuffer_Read(
    uint8         Channel,
    IPC_FrameType * const Frame);

/**
 * @brief  Returns number of frames currently available for reading.
 * @param  Channel  [in]  Channel index.
 * @return Frame count (0 = empty, IPC_RING_BUFFER_SIZE = full).
 */
extern uint32 IPC_RingBuffer_GetCount(uint8 Channel);

/**
 * @brief  Apply E2E Profile 22 protection to a frame before writing.
 *         Computes the E2E_CRC / E2E_Counter fields using the channel's internal
 *         E2E configuration so that the consumer-side check in IPC_RingBuffer_Read
 *         passes. Must be called by the producer once per frame, before Write.
 * @param  Channel  [in]  Channel index (selects the per-channel protect state).
 * @param  Frame    [io]  Frame whose E2E fields will be populated.
 * @return IPC_OK on success, IPC_E_PARAM if invalid.
 */
extern IPC_ReturnType IPC_RingBuffer_ProtectFrame(
    uint8                 Channel,
    IPC_FrameType * const Frame);

#endif /* IPC_RINGBUFFER_H */
