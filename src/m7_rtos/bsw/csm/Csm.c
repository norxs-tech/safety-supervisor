/**
 * =====================================================================================
 * @file        Csm.c
 * @brief       AUTOSAR R25-11 Crypto Service Manager — HSE MU Implementation
 *              Interfaces with NXP S32G HSE via Message Unit (MU0) registers.
 *              All crypto operations use a synchronous polling model on M7 (ASIL-D
 *              context does not permit blocking OS calls). Timeout guard prevents
 *              infinite polling in case of HSE hang.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_CryptoServiceManager
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#include "Csm.h"
#include <stdint.h>    /* uintptr_t for pointer-to-integer casts */
#include "Dem.h"

/*=====================================================================================
 * NXP S32G HSE Message Unit (MU0) Register Map
 * Reference: NXP S32G Reference Manual Rev.3 §59 (HSE Subsystem)
 *====================================================================================*/
#define HSE_MU0_BASE              (0x40210000UL)
#define HSE_MU_SR_OFFSET          (0x00UL)   /**< Status Register                    */
#define HSE_MU_CR_OFFSET          (0x08UL)   /**< Control Register                   */
#define HSE_MU_TR_OFFSET(n)       (0x20UL + ((n) * 4UL)) /**< Transmit Reg n (0-3)  */
#define HSE_MU_RR_OFFSET(n)       (0x40UL + ((n) * 4UL)) /**< Receive Reg n (0-3)   */

/* MU Status Register bit fields */
#define HSE_MU_SR_TEn_MASK        (0x00F00000UL) /**< TX Empty flags (bits 23:20)   */
#define HSE_MU_SR_RFn_MASK        (0x000F0000UL) /**< RX Full flags (bits 19:16)    */

/* HSE General Purpose Register (HSE_GPR) for boot status */
#define HSE_GPR_BASE              (0x40079000UL)
#define HSE_GPR_HSEBOOT_STATUS    (0x00UL)
#define HSE_BOOT_STATUS_OK        (0xA5A5A5A5UL)

/* HSE Service Descriptor (SRV_DESC) layout — abbreviated */
#define HSE_SRV_ID_MAC_GENERATE   (0x00000201UL)
#define HSE_SRV_ID_MAC_VERIFY     (0x00000202UL)
#define HSE_SRV_ID_SYM_ENCRYPT    (0x00000301UL)
#define HSE_SRV_ID_GET_RANDOM     (0x00000501UL)

#define MU_REG32(offset)          (*((volatile uint32 *)(HSE_MU0_BASE + (offset))))
#define GPR_REG32(offset)         (*((volatile uint32 *)(HSE_GPR_BASE + (offset))))

/*=====================================================================================
 * HSE Service Descriptor (simplified — maps to HSE firmware API structs)
 * In production: use NXP HSE Firmware API header (hse_srv_mac.h, etc.)
 *====================================================================================*/
typedef struct
{
    uint32 ServiceId;         /**< HSE service identifier                              */
    uint32 KeyHandle;         /**< Key slot handle                                     */
    uint32 InputAddr;         /**< Physical address of input data                      */
    uint32 InputLength;       /**< Input data length                                   */
    uint32 OutputAddr;        /**< Physical address of output buffer                   */
    uint32 OutputLength;      /**< Output buffer length                                */
    uint32 IVAddr;            /**< IV physical address (for AES-CBC)                   */
    uint32 Flags;             /**< Service-specific flags                              */
    uint32 Reserved[4U];      /**< Reserved for HSE internal use                       */
} Csm_HseServiceDescType;

/*=====================================================================================
 * Static service descriptor (M7 DTCM — not DMA buffer, kept aligned)
 *====================================================================================*/
#define CSM_START_SEC_VAR_SHARED
#include "MemMap.h"

static volatile Csm_HseServiceDescType Csm_ServiceDesc
    __attribute__((aligned(32)));

static volatile uint8 Csm_OutputBuffer[256U]
    __attribute__((aligned(32)));

#define CSM_STOP_SEC_VAR_SHARED
#include "MemMap.h"

/*=====================================================================================
 * Internal: Send descriptor to HSE and poll for completion
 *====================================================================================*/
#define CSM_START_SEC_CODE
#include "MemMap.h"

static Crypto_ResultType Csm_SendAndWait(void)
{
    Crypto_ResultType retVal       = CRYPTO_E_TIMEOUT;
    uint32            timeout      = CSM_HSE_TIMEOUT_CYCLES;
    uint32            muStatus;
    uint32            hseResponse;

    /* Wait for MU TX0 to be empty (ready to accept) */
    do
    {
        muStatus = MU_REG32(HSE_MU_SR_OFFSET);
        timeout--;
    } while (((muStatus & 0x00100000UL) == 0U) && (timeout > 0U));

    if (timeout == 0U)
    {
        retVal = CRYPTO_E_TIMEOUT;
    }
    else
    {
        /* Write descriptor physical address to MU TR0 */
        MU_REG32(HSE_MU_TR_OFFSET(0U)) =
            (uint32)(uintptr_t)&Csm_ServiceDesc;

        /* Poll for MU RX0 full (HSE response ready) */
        timeout = CSM_HSE_TIMEOUT_CYCLES;
        do
        {
            muStatus = MU_REG32(HSE_MU_SR_OFFSET);
            timeout--;
        } while (((muStatus & 0x00010000UL) == 0U) && (timeout > 0U));

        if (timeout == 0U)
        {
            retVal = CRYPTO_E_TIMEOUT;
        }
        else
        {
            /* Read HSE response code from RR0 */
            hseResponse = MU_REG32(HSE_MU_RR_OFFSET(0U));

            if (hseResponse == 0x00000000UL) /* HSE_SRV_RSP_OK */
            {
                retVal = CRYPTO_E_OK;
            }
            else if (hseResponse == 0x55A5AA33UL) /* HSE_SRV_RSP_VERIFY_FAILED */
            {
                retVal = CRYPTO_E_AUTH_FAILED;
            }
            else if (hseResponse == 0xA5A5A501UL) /* HSE_SRV_RSP_BUSY */
            {
                retVal = CRYPTO_E_BUSY;
            }
            else
            {
                retVal = CRYPTO_E_TIMEOUT;
            }
        }
    }

    return retVal;
}

/*=====================================================================================
 * Csm_Init
 *====================================================================================*/
Std_ReturnType Csm_Init(void)
{
    Std_ReturnType retVal       = E_NOT_OK;
    uint32         bootStatus;

    bootStatus = GPR_REG32(HSE_GPR_HSEBOOT_STATUS);

    if (bootStatus == HSE_BOOT_STATUS_OK)
    {
        retVal = E_OK;
    }
    else
    {
        /* HSE boot failure — report DEM event */
        (void)Dem_SetEventStatus(DEM_EVENT_CSM_MAC_VERIFICATION_FAIL,
                                  DEM_EVENT_STATUS_FAILED);
        retVal = E_NOT_OK;
    }

    return retVal;
}

/*=====================================================================================
 * Csm_MacGenerate — CMAC-AES-128 via HSE
 *====================================================================================*/
Crypto_ResultType Csm_MacGenerate(
    Csm_KeyIdType   KeyId,
    const uint8   * const DataPtr,
    uint32          DataLength,
    uint8         * const MacPtr,
    uint32        * const MacLength)
{
    Crypto_ResultType retVal = CRYPTO_E_KEY_NOT_VALID;
    uint32            i;

    if ((DataPtr == NULL_PTR) || (MacPtr == NULL_PTR) || (MacLength == NULL_PTR))
    {
        retVal = CRYPTO_E_KEY_NOT_VALID;
    }
    else if (*MacLength < CSM_CMAC_TRUNCATED_BYTES)
    {
        retVal = CRYPTO_E_SMALL_BUFFER;
    }
    else
    {
        /* Build HSE service descriptor */
        Csm_ServiceDesc.ServiceId    = HSE_SRV_ID_MAC_GENERATE;
        Csm_ServiceDesc.KeyHandle    = KeyId;
        Csm_ServiceDesc.InputAddr    = (uint32)(uintptr_t)DataPtr;
        Csm_ServiceDesc.InputLength  = DataLength;
        Csm_ServiceDesc.OutputAddr   = (uint32)(uintptr_t)Csm_OutputBuffer;
        Csm_ServiceDesc.OutputLength = CSM_CMAC_AES128_TAG_BYTES;
        Csm_ServiceDesc.IVAddr       = 0U;
        Csm_ServiceDesc.Flags        = 0U;

        retVal = Csm_SendAndWait();

        if (retVal == CRYPTO_E_OK)
        {
            /* Copy truncated 8-byte tag to output */
            for (i = 0U; i < CSM_CMAC_TRUNCATED_BYTES; i++)
            {
                MacPtr[i] = (uint8)Csm_OutputBuffer[i];
            }
            *MacLength = CSM_CMAC_TRUNCATED_BYTES;
        }
        else
        {
            (void)Dem_SetEventStatus(DEM_EVENT_CSM_MAC_VERIFICATION_FAIL,
                                      DEM_EVENT_STATUS_FAILED);
        }
    }

    return retVal;
}

/*=====================================================================================
 * Csm_MacVerify — CMAC-AES-128 verification via HSE
 *====================================================================================*/
Crypto_ResultType Csm_MacVerify(
    Csm_KeyIdType   KeyId,
    const uint8   * const DataPtr,
    uint32          DataLength,
    const uint8   * const MacPtr,
    uint32          MacLength)
{
    Crypto_ResultType retVal = CRYPTO_E_KEY_NOT_VALID;

    if ((DataPtr == NULL_PTR) || (MacPtr == NULL_PTR) || (MacLength == 0U))
    {
        retVal = CRYPTO_E_KEY_NOT_VALID;
    }
    else
    {
        Csm_ServiceDesc.ServiceId    = HSE_SRV_ID_MAC_VERIFY;
        Csm_ServiceDesc.KeyHandle    = KeyId;
        Csm_ServiceDesc.InputAddr    = (uint32)(uintptr_t)DataPtr;
        Csm_ServiceDesc.InputLength  = DataLength;
        Csm_ServiceDesc.OutputAddr   = (uint32)(uintptr_t)MacPtr;
        Csm_ServiceDesc.OutputLength = MacLength;
        Csm_ServiceDesc.IVAddr       = 0U;
        Csm_ServiceDesc.Flags        = 0U;

        retVal = Csm_SendAndWait();

        if (retVal == CRYPTO_E_AUTH_FAILED)
        {
            (void)Dem_SetEventStatus(DEM_EVENT_CSM_MAC_VERIFICATION_FAIL,
                                      DEM_EVENT_STATUS_FAILED);
        }
    }

    return retVal;
}

/*=====================================================================================
 * Csm_AES256_Encrypt
 *====================================================================================*/
Crypto_ResultType Csm_AES256_Encrypt(
    Csm_KeyIdType   KeyId,
    const uint8   * const IV,
    const uint8   * const PlainText,
    uint32          PlainLen,
    uint8         * const CipherText)
{
    Crypto_ResultType retVal = CRYPTO_E_KEY_NOT_VALID;

    if ((IV == NULL_PTR) || (PlainText == NULL_PTR) || (CipherText == NULL_PTR) ||
        ((PlainLen % 16U) != 0U))
    {
        retVal = CRYPTO_E_KEY_NOT_VALID;
    }
    else
    {
        Csm_ServiceDesc.ServiceId    = HSE_SRV_ID_SYM_ENCRYPT;
        Csm_ServiceDesc.KeyHandle    = KeyId;
        Csm_ServiceDesc.InputAddr    = (uint32)(uintptr_t)PlainText;
        Csm_ServiceDesc.InputLength  = PlainLen;
        Csm_ServiceDesc.OutputAddr   = (uint32)(uintptr_t)CipherText;
        Csm_ServiceDesc.OutputLength = PlainLen;
        Csm_ServiceDesc.IVAddr       = (uint32)(uintptr_t)IV;
        Csm_ServiceDesc.Flags        = 0x00000001UL; /* AES-256-CBC mode flag */

        retVal = Csm_SendAndWait();
    }

    return retVal;
}

/*=====================================================================================
 * Csm_GenerateRandom
 *====================================================================================*/
Crypto_ResultType Csm_GenerateRandom(uint8 * const Buffer, uint32 Length)
{
    Crypto_ResultType retVal = CRYPTO_E_KEY_NOT_VALID;

    if ((Buffer == NULL_PTR) || (Length == 0U))
    {
        retVal = CRYPTO_E_KEY_NOT_VALID;
    }
    else
    {
        Csm_ServiceDesc.ServiceId    = HSE_SRV_ID_GET_RANDOM;
        Csm_ServiceDesc.KeyHandle    = 0U;
        Csm_ServiceDesc.InputAddr    = 0U;
        Csm_ServiceDesc.InputLength  = 0U;
        Csm_ServiceDesc.OutputAddr   = (uint32)(uintptr_t)Buffer;
        Csm_ServiceDesc.OutputLength = Length;
        Csm_ServiceDesc.IVAddr       = 0U;
        Csm_ServiceDesc.Flags        = 0x00000002UL; /* TRNG mode flag */

        retVal = Csm_SendAndWait();

        if (retVal != CRYPTO_E_OK)
        {
            retVal = CRYPTO_E_ENTROPY_EXHAUSTED;
        }
    }

    return retVal;
}

/*=====================================================================================
 * Csm_GetHseStatus
 *====================================================================================*/
Std_ReturnType Csm_GetHseStatus(uint8 * const FwVersion, uint8 * const LifecycleState)
{
    Std_ReturnType retVal = E_NOT_OK;
    uint32         raw;

    if ((FwVersion != NULL_PTR) && (LifecycleState != NULL_PTR))
    {
        raw               = GPR_REG32(HSE_GPR_HSEBOOT_STATUS);
        FwVersion[0U]     = (uint8)((raw >> 24U) & 0xFFU);
        FwVersion[1U]     = (uint8)((raw >> 16U) & 0xFFU);
        FwVersion[2U]     = (uint8)((raw >> 8U)  & 0xFFU);
        FwVersion[3U]     = (uint8)(raw & 0xFFU);
        *LifecycleState   = (uint8)(GPR_REG32(0x04UL) & 0x03U);
        retVal            = E_OK;
    }

    return retVal;
}

#define CSM_STOP_SEC_CODE
#include "MemMap.h"
