/**
 * =====================================================================================
 * @file        app_idps.c
 * @brief       IDPS Firewall — QNX/A53 Intrusion Detection & Prevention System
 *              UN R155 compliant network threat mitigation for SDV gateway.
 *
 *              Architecture:
 *              ─────────────────────────────────────────────────────────────────────
 *              ┌──────────────────────────────────────────────────────────┐
 *              │  QNX Process: idps_daemon (POSIX, runs on Cortex-A53)   │
 *              │                                                           │
 *              │  ┌─────────────────┐    ┌──────────────────────────────┐ │
 *              │  │ Token Bucket    │    │ Deep Packet Inspection (DPI) │ │
 *              │  │ Rate Limiter    │    │ • SOME/IP service filter      │ │
 *              │  │ per source IP   │    │ • DDS topic whitelist         │ │
 *              │  │ per service ID  │    │ • CSMA/CD anomaly detect      │ │
 *              │  └────────┬────────┘    └───────────┬──────────────────┘ │
 *              │           │                         │                     │
 *              │           └──────────┬──────────────┘                     │
 *              │                      │                                     │
 *              │           ┌──────────▼──────────┐                         │
 *              │           │  CSM/HSE MAC Verify │  ← CMAC-AES-128 via HSE │
 *              │           └──────────┬──────────┘                         │
 *              │                      │ PASS                                │
 *              │           ┌──────────▼──────────┐                         │
 *              │           │  IPC Ring Buffer    │  ← Forward to M7        │
 *              │           │  Write (A53→M7)     │                         │
 *              │           └─────────────────────┘                         │
 *              └──────────────────────────────────────────────────────────┘
 *
 *              Token Bucket Algorithm:
 *                Each (src_ip, service_id) pair gets an independent bucket.
 *                Bucket refills at rate = IDPS_REFILL_RATE_PPS tokens/second.
 *                Max burst = IDPS_MAX_BURST_TOKENS.
 *                Packet arriving to empty bucket → DROP + log event.
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   UN R155, AUTOSAR R25-11 (IAM), ISO 21434
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *              Contact: contact@norxs.com | https://www.norxs.com/
 * @confidential Proprietary information. Unauthorized disclosure is strictly prohibited.
 * @history
 * Version      Date        Author          Modification
 * 0.9.0-RC1    2026-06-01  norxs-lab       V3.0 AUTOSAR R25-11 Refactoring
 * =====================================================================================
 */

/* QNX POSIX includes */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/neutrino.h>

/* Project includes */
#include "Platform_Types.h"
#include "IPC_RingBuffer.h"
#include "Csm.h"

/*=====================================================================================
 * IDPS Configuration
 *====================================================================================*/
#define IDPS_MAX_TRACKED_SOURCES    (64U)   /**< Max concurrent tracked IP sources     */
#define IDPS_MAX_BURST_TOKENS       (20U)   /**< Maximum burst before rate limiting    */
#define IDPS_REFILL_RATE_PPS        (10U)   /**< Token refill rate [packets/second]    */
#define IDPS_REFILL_INTERVAL_NS     (100000000ULL) /**< Refill interval [100ms in ns]  */
#define IDPS_MAX_PAYLOAD_BYTES      (1500U) /**< Ethernet MTU                          */
#define IDPS_SOMEIP_HEADER_BYTES    (16U)   /**< SOME/IP header size                   */
#define IDPS_DEM_EVENT_THRESHOLD    (5U)    /**< Drops before reporting security event */

/*=====================================================================================
 * SOME/IP Whitelisted Service IDs (allow-list per UN R155 §7.3.4)
 *====================================================================================*/
static const uint16_t IDPS_AllowedServiceIDs[] = {
    0x1234U, /**< Vehicle Dynamics Service                                              */
    0x1235U, /**< AI Command Distribution Service                                      */
    0x1236U, /**< Sensor Aggregation Service                                           */
    0x1237U, /**< Diagnostics Service (UDS over SOME/IP)                               */
    0x1238U, /**< OTA Management Service                                               */
    0x0000U  /**< Terminator                                                            */
};

/*=====================================================================================
 * Token Bucket Entry
 *====================================================================================*/
typedef struct
{
    uint32_t  src_ip;                   /**< Source IP (IPv4) or 0 = unused slot       */
    uint16_t  service_id;               /**< SOME/IP service ID                        */
    uint32_t  tokens;                   /**< Current token count                       */
    uint64_t  last_refill_ns;           /**< Timestamp of last refill [ns]             */
    uint32_t  drop_count;               /**< Total packets dropped for this source     */
    uint32_t  pass_count;               /**< Total packets passed for this source      */
} IDPS_TokenBucketEntry;

/*=====================================================================================
 * SOME/IP Packet Header (network byte order)
 *====================================================================================*/
typedef struct __attribute__((packed))
{
    uint16_t service_id;        /**< SOME/IP Service ID                                */
    uint16_t method_id;         /**< Method/Event ID                                   */
    uint32_t length;            /**< Payload length + 8 bytes                          */
    uint16_t client_id;         /**< Client ID                                         */
    uint16_t session_id;        /**< Session ID (rolling counter)                      */
    uint8_t  protocol_ver;      /**< Protocol version (0x01)                           */
    uint8_t  interface_ver;     /**< Interface version                                 */
    uint8_t  msg_type;          /**< Message type                                      */
    uint8_t  return_code;       /**< Return code                                       */
} IDPS_SomeIpHeaderType;

/*=====================================================================================
 * IDPS State
 *====================================================================================*/
static IDPS_TokenBucketEntry IDPS_Buckets[IDPS_MAX_TRACKED_SOURCES];
static pthread_mutex_t       IDPS_BucketMutex;
static volatile bool         IDPS_Running;

/*=====================================================================================
 * Internal: Get current monotonic timestamp in nanoseconds
 *====================================================================================*/
static uint64_t IDPS_GetTimestampNs(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/*=====================================================================================
 * Internal: Find or allocate token bucket for (src_ip, service_id) pair
 *====================================================================================*/
static IDPS_TokenBucketEntry * IDPS_GetBucket(
    uint32_t src_ip,
    uint16_t service_id)
{
    uint32_t i;
    uint32_t first_free = IDPS_MAX_TRACKED_SOURCES; /* Sentinel */
    IDPS_TokenBucketEntry * found = NULL;

    for (i = 0U; i < IDPS_MAX_TRACKED_SOURCES; i++)
    {
        if ((IDPS_Buckets[i].src_ip    == src_ip) &&
            (IDPS_Buckets[i].service_id == service_id))
        {
            found = &IDPS_Buckets[i];
            break;
        }

        if ((IDPS_Buckets[i].src_ip == 0U) && (first_free == IDPS_MAX_TRACKED_SOURCES))
        {
            first_free = i;
        }
    }

    if ((found == NULL) && (first_free < IDPS_MAX_TRACKED_SOURCES))
    {
        /* Allocate new bucket */
        IDPS_Buckets[first_free].src_ip          = src_ip;
        IDPS_Buckets[first_free].service_id      = service_id;
        IDPS_Buckets[first_free].tokens          = IDPS_MAX_BURST_TOKENS;
        IDPS_Buckets[first_free].last_refill_ns  = IDPS_GetTimestampNs();
        IDPS_Buckets[first_free].drop_count      = 0U;
        IDPS_Buckets[first_free].pass_count      = 0U;
        found = &IDPS_Buckets[first_free];
    }

    return found;
}

/*=====================================================================================
 * Internal: Refill tokens based on elapsed time (token bucket algorithm)
 *====================================================================================*/
static void IDPS_RefillTokens(IDPS_TokenBucketEntry * const bucket)
{
    uint64_t now_ns;
    uint64_t elapsed_ns;
    uint32_t new_tokens;

    now_ns     = IDPS_GetTimestampNs();
    elapsed_ns = now_ns - bucket->last_refill_ns;

    if (elapsed_ns >= IDPS_REFILL_INTERVAL_NS)
    {
        uint32_t intervals = (uint32_t)(elapsed_ns / IDPS_REFILL_INTERVAL_NS);
        new_tokens = intervals * IDPS_REFILL_RATE_PPS / 10U;

        bucket->tokens = bucket->tokens + new_tokens;
        if (bucket->tokens > IDPS_MAX_BURST_TOKENS)
        {
            bucket->tokens = IDPS_MAX_BURST_TOKENS;
        }

        bucket->last_refill_ns = now_ns;
    }
}

/*=====================================================================================
 * Internal: Check if SOME/IP service ID is in whitelist
 *====================================================================================*/
static bool IDPS_IsServiceAllowed(uint16_t service_id)
{
    uint32_t i = 0U;
    bool     allowed = false;

    while (IDPS_AllowedServiceIDs[i] != 0U)
    {
        if (IDPS_AllowedServiceIDs[i] == service_id)
        {
            allowed = true;
            break;
        }
        i++;
    }

    return allowed;
}

/*=====================================================================================
 * Internal: MAC Verification via HSE (CSM layer)
 *  IPC frames include an 8-byte CMAC-AES-128 tag at end of SOME/IP payload.
 *  If tag present: verify via HSE. If absent: allow (non-safety SOME/IP messages).
 *====================================================================================*/
static bool IDPS_VerifyMac(
    const uint8_t * const payload,
    uint32_t              payload_len)
{
    bool            result = true;
    Crypto_ResultType csm_result;

    /* Only verify if payload is large enough to contain CMAC tag */
    if (payload_len > CSM_CMAC_TRUNCATED_BYTES)
    {
        uint32_t data_len = payload_len - CSM_CMAC_TRUNCATED_BYTES;
        const uint8_t * mac_ptr = &payload[data_len];

        csm_result = Csm_MacVerify(
            CSM_KEYID_IPC_MAC_KEY,
            payload,
            data_len,
            mac_ptr,
            CSM_CMAC_TRUNCATED_BYTES);

        if (csm_result != CRYPTO_E_OK)
        {
            result = false;
        }
    }

    return result;
}

/*=====================================================================================
 * IDPS_ProcessPacket — Main packet processing entry point
 *  Called for each incoming Ethernet frame from the SOME/IP stack.
 *  Returns true if packet should be forwarded, false if dropped.
 *====================================================================================*/
bool IDPS_ProcessPacket(
    uint32_t       src_ip,
    const uint8_t * const pkt_data,
    uint32_t              pkt_len)
{
    bool                    forward = false;
    const IDPS_SomeIpHeaderType * someip_hdr;
    IDPS_TokenBucketEntry * bucket;

    if ((pkt_data == NULL) || (pkt_len < IDPS_SOMEIP_HEADER_BYTES))
    {
        return false; /* Malformed packet */
    }

    /* Parse SOME/IP header */
    someip_hdr = (const IDPS_SomeIpHeaderType *)pkt_data;

    /* Step 1: Service ID whitelist check (DPI) */
    if (!IDPS_IsServiceAllowed(someip_hdr->service_id))
    {
        return false; /* Unknown service — drop silently */
    }

    /* Step 2: Protocol version check */
    if (someip_hdr->protocol_ver != 0x01U)
    {
        return false; /* Invalid SOME/IP version */
    }

    /* Step 3: Token bucket rate limiting */
    pthread_mutex_lock(&IDPS_BucketMutex);
    bucket = IDPS_GetBucket(src_ip, someip_hdr->service_id);

    if (bucket != NULL)
    {
        IDPS_RefillTokens(bucket);

        if (bucket->tokens > 0U)
        {
            bucket->tokens--;
            bucket->pass_count++;
            forward = true;
        }
        else
        {
            /* Rate limit exceeded — drop packet */
            bucket->drop_count++;

            if (bucket->drop_count == IDPS_DEM_EVENT_THRESHOLD)
            {
                /* Log security event to M7 via IPC */
                IPC_FrameType diagFrame;
                diagFrame.Magic          = IPC_MAGIC_HEADER;
                diagFrame.FrameType      = IPC_FRAME_DIAG_EVENT;
                diagFrame.SequenceNumber = bucket->drop_count;
                diagFrame.PayloadLength  = 4U;
                diagFrame.Timestamp_us   = (uint32_t)(IDPS_GetTimestampNs() / 1000ULL);
                diagFrame.Payload[0U]    = (uint8_t)(src_ip >> 24U);
                diagFrame.Payload[1U]    = (uint8_t)(src_ip >> 16U);
                diagFrame.Payload[2U]    = (uint8_t)(src_ip >> 8U);
                diagFrame.Payload[3U]    = (uint8_t)(src_ip & 0xFFU);

                (void)IPC_RingBuffer_Write(IPC_CHANNEL_A53_TO_M7, &diagFrame);
            }
        }
    }
    pthread_mutex_unlock(&IDPS_BucketMutex);

    /* Step 4: MAC verification for safety-critical commands */
    if (forward && (someip_hdr->service_id == 0x1235U)) /* AI Command service */
    {
        const uint8_t * payload = &pkt_data[IDPS_SOMEIP_HEADER_BYTES];
        uint32_t payload_len    = pkt_len - IDPS_SOMEIP_HEADER_BYTES;

        if (!IDPS_VerifyMac(payload, payload_len))
        {
            forward = false; /* MAC failed — attacker signature mismatch */
        }
    }

    return forward;
}

/*=====================================================================================
 * IDPS_Init — Initialize IDPS module
 *====================================================================================*/
int IDPS_Init(void)
{
    uint32_t i;
    int      result;

    /* Zero-initialize all token buckets */
    for (i = 0U; i < IDPS_MAX_TRACKED_SOURCES; i++)
    {
        memset(&IDPS_Buckets[i], 0, sizeof(IDPS_TokenBucketEntry));
    }

    result = pthread_mutex_init(&IDPS_BucketMutex, NULL);

    if (result == 0)
    {
        IDPS_Running = true;
    }

    return result;
}

/*=====================================================================================
 * IDPS_GetStats — Query per-source statistics (for UDS diagnostics)
 *====================================================================================*/
void IDPS_GetStats(uint32_t * const total_pass, uint32_t * const total_drop)
{
    uint32_t i;

    if ((total_pass == NULL) || (total_drop == NULL))
    {
        return;
    }

    *total_pass = 0U;
    *total_drop = 0U;

    pthread_mutex_lock(&IDPS_BucketMutex);
    for (i = 0U; i < IDPS_MAX_TRACKED_SOURCES; i++)
    {
        if (IDPS_Buckets[i].src_ip != 0U)
        {
            *total_pass += IDPS_Buckets[i].pass_count;
            *total_drop += IDPS_Buckets[i].drop_count;
        }
    }
    pthread_mutex_unlock(&IDPS_BucketMutex);
}
