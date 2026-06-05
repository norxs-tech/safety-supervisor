/**
 * =====================================================================================
 * @file        Csm.h
 * @brief       AUTOSAR R25-11 Crypto Service Manager (CSM) — HSE Interface Header
 *              Provides MAC generation/verification, symmetric encryption, and
 *              key management primitives via NXP S32G Hardware Security Engine (HSE).
 *              All cryptographic operations are offloaded to HSE to avoid M7 CPU load.
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_CryptoServiceManager,
 *              UN R155 Cybersecurity, FIPS 197, NIST SP 800-38B (CMAC)
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

#ifndef CSM_H
#define CSM_H

#include "Platform_Types.h"

/*=====================================================================================
 * CSM Job Result Codes (AUTOSAR R25-11 SWS_Csm_00004)
 *====================================================================================*/
typedef uint8 Crypto_ResultType;
#define CRYPTO_E_OK              ((Crypto_ResultType)0x00U) /**< Job successful         */
#define CRYPTO_E_BUSY            ((Crypto_ResultType)0x01U) /**< HSE currently busy     */
#define CRYPTO_E_SMALL_BUFFER    ((Crypto_ResultType)0x02U) /**< Output buffer too small*/
#define CRYPTO_E_ENTROPY_EXHAUSTED ((Crypto_ResultType)0x03U) /**< RNG entropy depleted */
#define CRYPTO_E_AUTH_FAILED     ((Crypto_ResultType)0x04U) /**< MAC verification fail  */
#define CRYPTO_E_KEY_NOT_VALID   ((Crypto_ResultType)0x05U) /**< Key slot empty/invalid */
#define CRYPTO_E_TIMEOUT         ((Crypto_ResultType)0x06U) /**< HSE response timeout   */

/*=====================================================================================
 * Key Slot IDs (mapped to NXP HSE Key Catalog)
 *====================================================================================*/
typedef uint32 Csm_KeyIdType;
#define CSM_KEYID_IPC_MAC_KEY        (0x00000001UL) /**< CMAC key for IPC frame auth   */
#define CSM_KEYID_OTA_VERIFY_KEY     (0x00000002UL) /**< RSA-4096 OTA image verify key  */
#define CSM_KEYID_SECUREBOOT_KEY     (0x00000003UL) /**< Secure Boot root of trust key  */
#define CSM_KEYID_TLS_SESSION_KEY    (0x00000004UL) /**< TLS session key (cloud comms)  */
#define CSM_KEYID_NVM_ENCRYPT_KEY    (0x00000005UL) /**< AES-256 NvM encryption key     */

/*=====================================================================================
 * MAC Constants
 *====================================================================================*/
#define CSM_CMAC_AES128_TAG_BYTES    (16U)  /**< Full CMAC-AES-128 tag length          */
#define CSM_CMAC_TRUNCATED_BYTES     (8U)   /**< Truncated tag (64-bit) for IPC        */
#define CSM_HSE_TIMEOUT_CYCLES       (10000UL) /**< Max spin-wait cycles for HSE resp  */

/*=====================================================================================
 * API Declarations
 *====================================================================================*/

/**
 * @brief  Initialize CSM module and verify HSE firmware integrity.
 *         Checks HSE boot status register before returning.
 * @return E_OK if HSE is operational, E_NOT_OK if HSE boot failed.
 */
extern Std_ReturnType Csm_Init(void);

/**
 * @brief  Generate a CMAC-AES-128 authentication tag over a data buffer.
 *         Offloads computation to HSE via MU (Message Unit) interface.
 * @param  KeyId       [in]  Key slot ID (CSM_KEYID_*).
 * @param  DataPtr     [in]  Pointer to input data buffer.
 * @param  DataLength  [in]  Length of input data in bytes.
 * @param  MacPtr      [out] Pointer to output MAC buffer.
 * @param  MacLength   [io]  In: buffer size. Out: actual MAC length written.
 * @return CRYPTO_E_OK, CRYPTO_E_BUSY, CRYPTO_E_KEY_NOT_VALID, or CRYPTO_E_TIMEOUT.
 * @req    SWS_Csm_00040
 */
extern Crypto_ResultType Csm_MacGenerate(
    Csm_KeyIdType   KeyId,
    const uint8   * const DataPtr,
    uint32          DataLength,
    uint8         * const MacPtr,
    uint32        * const MacLength);

/**
 * @brief  Verify a CMAC-AES-128 tag against a data buffer.
 * @param  KeyId       [in]  Key slot ID.
 * @param  DataPtr     [in]  Pointer to data buffer to verify.
 * @param  DataLength  [in]  Length of data in bytes.
 * @param  MacPtr      [in]  Pointer to expected MAC tag.
 * @param  MacLength   [in]  Length of MAC tag in bytes.
 * @return CRYPTO_E_OK if tag matches, CRYPTO_E_AUTH_FAILED if mismatch.
 * @req    SWS_Csm_00041
 */
extern Crypto_ResultType Csm_MacVerify(
    Csm_KeyIdType   KeyId,
    const uint8   * const DataPtr,
    uint32          DataLength,
    const uint8   * const MacPtr,
    uint32          MacLength);

/**
 * @brief  Encrypt data using AES-256-CBC via HSE.
 * @param  KeyId       [in]  Key slot ID.
 * @param  IV          [in]  16-byte initialization vector.
 * @param  PlainText   [in]  Input plaintext buffer (must be multiple of 16 bytes).
 * @param  PlainLen    [in]  Plaintext length in bytes.
 * @param  CipherText  [out] Output ciphertext buffer (same size as plaintext).
 * @return CRYPTO_E_OK on success.
 */
extern Crypto_ResultType Csm_AES256_Encrypt(
    Csm_KeyIdType   KeyId,
    const uint8   * const IV,
    const uint8   * const PlainText,
    uint32          PlainLen,
    uint8         * const CipherText);

/**
 * @brief  Generate cryptographically secure random bytes via HSE TRNG.
 * @param  Buffer   [out] Output buffer.
 * @param  Length   [in]  Number of random bytes to generate.
 * @return CRYPTO_E_OK on success, CRYPTO_E_ENTROPY_EXHAUSTED if TRNG not ready.
 */
extern Crypto_ResultType Csm_GenerateRandom(
    uint8  * const Buffer,
    uint32          Length);

/**
 * @brief  Query HSE firmware version and security lifecycle state.
 * @param  FwVersion  [out] HSE firmware version (4 bytes).
 * @param  LifecycleState [out] Current lifecycle (0=CUST_DEL, 1=OEM_PROD, 2=IN_FIELD).
 * @return E_OK on success.
 */
extern Std_ReturnType Csm_GetHseStatus(
    uint8  * const FwVersion,
    uint8  * const LifecycleState);

#endif /* CSM_H */
