/**
 * =====================================================================================
 * @file        test_main.c
 * @brief       Host-native unit-test suite for CI (compiled with ASan + UBSan).
 *
 *              Test inventory (each test is a separate ctest case):
 *                crc-kat      CRC-16-CCITT known-answer test ("123456789" → 0x29B1)
 *                e2e-p5       Profile 5 protect/check round-trip + corruption + bounds
 *                e2e-p22      Profile 22 protect/check round-trip + corruption
 *                e2e-null     Defensive null-pointer rejection (CERT EXP34-C)
 *                ipc          Ring buffer init / round-trip / empty / full boundaries
 *                asild        ASIL-D bitwise redundancy macros (ISO 26262-6 Table 9)
 *                wdgm         Alive supervision OK / starvation FAILED / deadline EXPIRED
 *                dem          Debounce confirmation, UDS status bits, invalid rejection
 *                sbst         RAM March C- self-test (healthy pass + param rejection)
 *
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 * @history
 * Version      Date        Author          Modification
 * 0.9.1        2026-06-11  norxs-lab       Expanded suite: KAT, negative, WdgM, DEM
 * =====================================================================================
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "Platform_Types.h"
#include "E2E.h"
#include "IPC_RingBuffer.h"
#include "WdgM.h"
#include "Dem.h"
#include "Sbst.h"

#define PASS 0
#define FAIL 1

#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (!(cond)) { printf("FAIL: %s\n", (msg)); return FAIL; }\
    } while (0)

/*=====================================================================================
 * crc-kat — CRC-16-CCITT known-answer test
 *  Reference vector: CRC-16/CCITT-FALSE("123456789") = 0x29B1
 *  (init 0xFFFF, poly 0x1021, no reflection, no final XOR)
 *====================================================================================*/
static int test_crc_kat(void)
{
    static const uint8 vector[9] = { '1','2','3','4','5','6','7','8','9' };

    const uint16 crc = E2E_CRC16_CCITT(vector, 9U, 0xFFFFU);
    CHECK(crc == 0x29B1U, "CRC-16-CCITT KAT mismatch (expected 0x29B1)");

    /* Null input must return safe value, not crash */
    CHECK(E2E_CRC16_CCITT(NULL_PTR, 9U, 0xFFFFU) == 0x0000U,
          "CRC null-input handling");

    /* Zero-length input returns the start value unchanged */
    CHECK(E2E_CRC16_CCITT(vector, 0U, 0xFFFFU) == 0xFFFFU,
          "CRC zero-length handling");

    printf("PASS: CRC-16-CCITT known-answer test (0x29B1)\n");
    return PASS;
}

/*=====================================================================================
 * e2e-p5 — Profile 5 round-trip, corruption detection, bounds rejection
 *====================================================================================*/
static int test_e2e_p5(void)
{
    uint8 buf[16];
    E2E_P5ConfigType cfg;
    E2E_P5ProtectStateType tx;
    E2E_P5CheckStateType rx;
    E2E_PCheckStatusType status;

    (void)memset(buf, 0xA5, sizeof(buf));
    (void)memset(&cfg, 0, sizeof(cfg));
    (void)memset(&tx, 0, sizeof(tx));
    (void)memset(&rx, 0, sizeof(rx));

    cfg.DataLength        = 16U;
    cfg.MaxDeltaCounter   = E2E_P5_MAX_DELTA_COUNTER;
    cfg.WindowSize        = 2U;
    cfg.MinOkStateInit    = 1U;
    cfg.MaxErrorStateInit = 1U;

    /* 1. Round-trip: protected frame must verify OK */
    CHECK(E2E_P5Protect(&cfg, &tx, buf, 16U) == E_OK, "P5Protect");
    status = E2E_P_STATUS_ERROR;
    CHECK(E2E_P5Check(&cfg, &rx, buf, 16U, &status) == E_OK, "P5Check returns E_OK");
    CHECK((status == E2E_P_STATUS_OK) || (status == E2E_P_STATUS_REPEATED),
          "P5 round-trip status not OK");

    /* 2. Corruption: single-bit flip in payload must be detected */
    buf[8] ^= 0x01U;
    status = E2E_P_STATUS_OK;
    (void)E2E_P5Check(&cfg, &rx, buf, 16U, &status);
    CHECK(status != E2E_P_STATUS_OK, "P5 single-bit corruption NOT detected");
    buf[8] ^= 0x01U;  /* restore */

    /* 3. Corruption: CRC field flip must be detected */
    buf[0] ^= 0x80U;
    status = E2E_P_STATUS_OK;
    (void)E2E_P5Check(&cfg, &rx, buf, 16U, &status);
    CHECK(status != E2E_P_STATUS_OK, "P5 CRC-field corruption NOT detected");
    buf[0] ^= 0x80U;

    /* 4. Bounds: wrong length must be rejected */
    CHECK(E2E_P5Protect(&cfg, &tx, buf, 8U) == E_NOT_OK,
          "P5Protect accepted length != DataLength");
    CHECK(E2E_P5Check(&cfg, &rx, buf, 3U, &status) == E_NOT_OK,
          "P5Check accepted length < 4");

    printf("PASS: E2E Profile 5 (round-trip, corruption, bounds)\n");
    return PASS;
}

/*=====================================================================================
 * e2e-p22 — Profile 22 round-trip and corruption detection
 *====================================================================================*/
static int test_e2e_p22(void)
{
    uint8 buf[4];
    E2E_P22ConfigType cfg;
    E2E_P22ProtectStateType tx;
    E2E_P22CheckStateType rx;
    E2E_PCheckStatusType status;
    uint8 i;

    (void)memset(buf, 0, sizeof(buf));
    (void)memset(&cfg, 0, sizeof(cfg));
    (void)memset(&tx, 0, sizeof(tx));
    (void)memset(&rx, 0, sizeof(rx));

    for (i = 0U; i < 16U; i++)
    {
        cfg.DataIDList[i] = (uint8)(0x10U + i);
    }
    cfg.DataLength      = 16U;
    cfg.MaxDeltaCounter = E2E_P22_MAX_DELTA_COUNTER;

    /* 1. Round-trip */
    CHECK(E2E_P22Protect(&cfg, &tx, buf) == E_OK, "P22Protect");
    rx.Counter = (uint8)(buf[1] >> 4U);  /* align expected counter with frame */
    status = E2E_P_STATUS_ERROR;
    CHECK(E2E_P22Check(&cfg, &rx, buf, &status) == E_OK, "P22Check returns E_OK");
    CHECK((status == E2E_P_STATUS_OK) || (status == E2E_P_STATUS_REPEATED),
          "P22 round-trip status not OK");

    /* 2. Corruption of CRC byte must be detected */
    buf[0] ^= 0xFFU;
    status = E2E_P_STATUS_OK;
    (void)E2E_P22Check(&cfg, &rx, buf, &status);
    CHECK(status != E2E_P_STATUS_OK, "P22 CRC corruption NOT detected");

    printf("PASS: E2E Profile 22 (round-trip + corruption)\n");
    return PASS;
}

/*=====================================================================================
 * e2e-null — defensive null-pointer rejection (regression test for EXP34-C fix)
 *====================================================================================*/
static int test_e2e_null(void)
{
    uint8 buf[8] = { 0 };
    E2E_P5ConfigType  cfg5;
    E2E_P22ConfigType cfg22;
    E2E_P5CheckStateType  rx5;
    E2E_P22CheckStateType rx22;
    E2E_PCheckStatusType  status = E2E_P_STATUS_ERROR;

    (void)memset(&cfg5,  0, sizeof(cfg5));
    (void)memset(&cfg22, 0, sizeof(cfg22));
    (void)memset(&rx5,   0, sizeof(rx5));
    (void)memset(&rx22,  0, sizeof(rx22));
    cfg5.DataLength = 8U;

    /* Status == NULL must be rejected WITHOUT dereference (would abort under ASan) */
    CHECK(E2E_P5Check(&cfg5, &rx5, buf, 8U, NULL_PTR) == E_NOT_OK,
          "P5Check accepted NULL Status");
    CHECK(E2E_P22Check(&cfg22, &rx22, buf, NULL_PTR) == E_NOT_OK,
          "P22Check accepted NULL Status");

    /* Other null params: rejected with Status = ERROR */
    CHECK(E2E_P5Check(NULL_PTR, &rx5, buf, 8U, &status) == E_NOT_OK,
          "P5Check accepted NULL Config");
    CHECK(status == E2E_P_STATUS_ERROR, "P5Check Status not ERROR on NULL Config");

    printf("PASS: E2E defensive null-pointer rejection\n");
    return PASS;
}

/*=====================================================================================
 * ipc — full round-trip via ProtectFrame, empty / full boundary behaviour
 *====================================================================================*/
static int test_ipc(void)
{
    IPC_FrameType frame;
    IPC_FrameType out;
    uint32 n;

    CHECK(IPC_RingBuffer_Init(IPC_CHANNEL_M7_TO_A53) == IPC_OK, "IPC Init");

    /* 1. Empty read must return IPC_E_EMPTY */
    (void)memset(&out, 0, sizeof(out));
    CHECK(IPC_RingBuffer_Read(IPC_CHANNEL_M7_TO_A53, &out) == IPC_E_EMPTY,
          "Read on empty ring != IPC_E_EMPTY");

    /* 2. Full E2E-protected round-trip must return IPC_OK and intact payload */
    (void)memset(&frame, 0, sizeof(frame));
    frame.Magic          = IPC_MAGIC_HEADER;
    frame.SequenceNumber = 1U;
    frame.PayloadLength  = 4U;
    frame.FrameType      = IPC_FRAME_SAFETY_STATUS;
    frame.Payload[0]     = 0xAAU;
    frame.Payload[3]     = 0x55U;

    CHECK(IPC_RingBuffer_ProtectFrame(IPC_CHANNEL_M7_TO_A53, &frame) == IPC_OK,
          "ProtectFrame");
    CHECK(IPC_RingBuffer_Write(IPC_CHANNEL_M7_TO_A53, &frame) == IPC_OK, "Write");

    (void)memset(&out, 0, sizeof(out));
    CHECK(IPC_RingBuffer_Read(IPC_CHANNEL_M7_TO_A53, &out) == IPC_OK,
          "Round-trip Read != IPC_OK (E2E rejected valid frame)");
    CHECK(out.Payload[0] == 0xAAU, "Payload[0] mismatch after round-trip");
    CHECK(out.Payload[3] == 0x55U, "Payload[3] mismatch after round-trip");
    CHECK(out.SequenceNumber == 1U, "SequenceNumber mismatch after round-trip");

    /* 3. Fill to capacity (free-running indices → SIZE slots usable);
          next Write must return IPC_E_FULL */
    CHECK(IPC_RingBuffer_Init(IPC_CHANNEL_M7_TO_A53) == IPC_OK, "Re-Init");
    for (n = 0U; n < IPC_RING_BUFFER_SIZE; n++)
    {
        frame.SequenceNumber = n;
        CHECK(IPC_RingBuffer_ProtectFrame(IPC_CHANNEL_M7_TO_A53, &frame) == IPC_OK,
              "ProtectFrame (fill)");
        CHECK(IPC_RingBuffer_Write(IPC_CHANNEL_M7_TO_A53, &frame) == IPC_OK,
              "Write (fill)");
    }
    CHECK(IPC_RingBuffer_Write(IPC_CHANNEL_M7_TO_A53, &frame) == IPC_E_FULL,
          "Write on full ring != IPC_E_FULL");
    CHECK(IPC_RingBuffer_GetCount(IPC_CHANNEL_M7_TO_A53) ==
          IPC_RING_BUFFER_SIZE, "GetCount after fill");

    /* 4. Invalid parameters */
    CHECK(IPC_RingBuffer_Init(99U) == IPC_E_PARAM, "Init accepted bad channel");
    CHECK(IPC_RingBuffer_Write(0U, NULL_PTR) == IPC_E_PARAM,
          "Write accepted NULL frame");

    printf("PASS: IPC RingBuffer (round-trip, empty, full, param checks)\n");
    return PASS;
}

/*=====================================================================================
 * asild — ASIL-D bitwise redundancy macros (ISO 26262-6 Table 9)
 *====================================================================================*/
static ASIL_D_REDUNDANT_VAR(uint32, AsilTestVar);

static int test_asild(void)
{
    /* 1. Consistent pair must pass the check */
    ASIL_D_SET(AsilTestVar, 0xDEADBEEFUL);
    CHECK(ASIL_D_CHECK(AsilTestVar), "ASIL_D_CHECK failed on consistent pair");
    CHECK(AsilTestVar == 0xDEADBEEFUL, "ASIL_D_SET did not store value");

    /* 2. Simulated single-bit upset in primary must be detected */
    AsilTestVar ^= 0x00000400UL;
    CHECK(!ASIL_D_CHECK(AsilTestVar), "Single-bit upset (primary) NOT detected");

    /* 3. Simulated single-bit upset in inverse copy must be detected */
    ASIL_D_SET(AsilTestVar, 0x12345678UL);
    AsilTestVar_inv ^= 0x00010000UL;
    CHECK(!ASIL_D_CHECK(AsilTestVar), "Single-bit upset (inverse) NOT detected");

    printf("PASS: ASIL-D bitwise redundancy macros\n");
    return PASS;
}

/*=====================================================================================
 * wdgm — alive supervision OK, starvation FAILED, deadline EXPIRED
 *  Entity config (WdgM.c): SA/VD/SSM expect 3 indications per 10ms window,
 *  IPC expects 2 (+1 margin). WdgM_MainFunction advances the tick by 5ms per call.
 *====================================================================================*/
static void wdgm_feed_all_entities(void)
{
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_ENTRY);
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_ENVELOPE_CHECK);
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_EXIT);
    (void)WdgM_CheckpointReached(WDGM_SE_VEHICLE_DYNAMICS,  WDGM_CP_VD_ENTRY);
    (void)WdgM_CheckpointReached(WDGM_SE_VEHICLE_DYNAMICS,  WDGM_CP_VD_SENSOR_FUSION);
    (void)WdgM_CheckpointReached(WDGM_SE_VEHICLE_DYNAMICS,  WDGM_CP_VD_EXIT);
    (void)WdgM_CheckpointReached(WDGM_SE_SAFE_STATE_MGR,    WDGM_CP_SSM_ENTRY);
    (void)WdgM_CheckpointReached(WDGM_SE_SAFE_STATE_MGR,    WDGM_CP_SSM_INTERPOLATION);
    (void)WdgM_CheckpointReached(WDGM_SE_SAFE_STATE_MGR,    WDGM_CP_SSM_EXIT);
    (void)WdgM_CheckpointReached(WDGM_SE_IPC_HANDLER,       WDGM_CP_IPC_RX);
    (void)WdgM_CheckpointReached(WDGM_SE_IPC_HANDLER,       WDGM_CP_IPC_TX);
}

static int test_wdgm(void)
{
    WdgM_GlobalStatusType g = WDGM_GLOBAL_STATUS_FAILED;
    WdgM_LocalStatusType  l = WDGM_LOCAL_STATUS_FAILED;
    int window;

    /* 1. Healthy operation: feed expected indications → OK after 2 windows */
    CHECK(WdgM_Init() == E_OK, "WdgM_Init");
    for (window = 0; window < 2; window++)
    {
        wdgm_feed_all_entities();
        WdgM_MainFunction();           /* +5ms  */
        WdgM_MainFunction();           /* +10ms → window evaluation */
    }
    CHECK(WdgM_GetGlobalStatus(&g) == E_OK, "GetGlobalStatus");
    CHECK(g == WDGM_GLOBAL_STATUS_OK, "Global status != OK under healthy feeding");

    /* 2. Starvation: no checkpoints for one window → entity + global FAILED */
    WdgM_MainFunction();
    WdgM_MainFunction();
    CHECK(WdgM_GetLocalStatus(WDGM_SE_SAFETY_ARBITRATOR, &l) == E_OK,
          "GetLocalStatus");
    CHECK(l == WDGM_LOCAL_STATUS_FAILED, "Starved entity not FAILED");
    (void)WdgM_GetGlobalStatus(&g);
    CHECK(g == WDGM_GLOBAL_STATUS_FAILED, "Global not FAILED on starvation");

    /* 3. Deadline supervision: ENTRY → 10ms elapses (> 8ms limit) → EXIT = EXPIRED */
    CHECK(WdgM_Init() == E_OK, "WdgM re-Init");
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_ENTRY);
    WdgM_MainFunction();
    WdgM_MainFunction();               /* +10ms elapsed > DeadlineLimit 8ms */
    (void)WdgM_CheckpointReached(WDGM_SE_SAFETY_ARBITRATOR, WDGM_CP_SA_EXIT);
    CHECK(WdgM_GetLocalStatus(WDGM_SE_SAFETY_ARBITRATOR, &l) == E_OK,
          "GetLocalStatus (deadline)");
    CHECK(l == WDGM_LOCAL_STATUS_EXPIRED, "Deadline overrun not EXPIRED");

    /* 4. Unknown entity rejected */
    CHECK(WdgM_CheckpointReached(0x7777U, WDGM_CP_SA_ENTRY) == E_NOT_OK,
          "Unknown SEID accepted");

    printf("PASS: WdgM (alive OK, starvation FAILED, deadline EXPIRED)\n");
    return PASS;
}

/*=====================================================================================
 * dem — debounce confirmation, UDS status bits (ISO 14229-1), invalid rejection
 *====================================================================================*/
static int test_dem(void)
{
    uint8 uds = 0U;

    /* 1. PREFAILED below threshold: pendingDTC set, confirmedDTC clear */
    CHECK(Dem_SetEventStatus(DEM_EVENT_IPC_E2E_ERROR,
                             DEM_EVENT_STATUS_PREFAILED) == E_OK, "PREFAILED #1");
    CHECK(Dem_GetEventUdsStatus(DEM_EVENT_IPC_E2E_ERROR, &uds) == E_OK,
          "GetEventUdsStatus");
    CHECK((uds & 0x04U) != 0U, "pendingDTC not set after first PREFAILED");
    CHECK((uds & 0x08U) == 0U, "confirmedDTC set too early");

    /* 2. Second PREFAILED reaches DEM_FAULT_CONFIRM_THRESHOLD → confirmed */
    CHECK(Dem_SetEventStatus(DEM_EVENT_IPC_E2E_ERROR,
                             DEM_EVENT_STATUS_PREFAILED) == E_OK, "PREFAILED #2");
    (void)Dem_GetEventUdsStatus(DEM_EVENT_IPC_E2E_ERROR, &uds);
    CHECK((uds & 0x08U) != 0U, "confirmedDTC not set at debounce threshold");
    CHECK((uds & 0x01U) != 0U, "testFailed not set at debounce threshold");

    /* 3. Direct FAILED on another event → immediately confirmed */
    CHECK(Dem_ReportErrorStatus(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED,
                                DEM_EVENT_STATUS_FAILED) == E_OK, "FAILED report");
    (void)Dem_GetEventUdsStatus(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED, &uds);
    CHECK((uds & 0x09U) == 0x09U, "TF+CDTC not set on direct FAILED");

    /* 4. Invalid event status must be rejected (regression for retVal bug) */
    CHECK(Dem_SetEventStatus(DEM_EVENT_IPC_E2E_ERROR, 0xEEU) == E_NOT_OK,
          "Invalid EventStatus accepted");

    /* 5. Unknown EventId must be rejected */
    CHECK(Dem_SetEventStatus(0x9999U, DEM_EVENT_STATUS_FAILED) == E_NOT_OK,
          "Unknown EventId accepted");
    CHECK(Dem_GetEventUdsStatus(0x9999U, &uds) == E_NOT_OK,
          "Unknown EventId accepted by getter");
    CHECK(Dem_GetEventUdsStatus(DEM_EVENT_IPC_E2E_ERROR, NULL_PTR) == E_NOT_OK,
          "NULL UdsStatus accepted");

    printf("PASS: DEM (debounce, UDS bits, parameter rejection)\n");
    return PASS;
}

/*=====================================================================================
 * sbst — RAM March C- self-test logic (parameter checks + clean-RAM pass)
 *====================================================================================*/
static int test_sbst(void)
{
    static volatile uint32 region[32];
    uint32 i;

    /* 1. March C- over healthy RAM must pass and leave the region zeroed */
    for (i = 0U; i < 32U; i++) { region[i] = 0xA5A5A5A5UL; }
    CHECK(Sbst_RamMarchC(region, 32U) == SBST_OK, "March C- failed on healthy RAM");
    for (i = 0U; i < 32U; i++)
    {
        CHECK(region[i] == 0U, "March C- did not leave region zeroed");
    }

    /* 2. Parameter validation */
    CHECK(Sbst_RamMarchC(NULL_PTR, 32U) == SBST_E_PARAM, "NULL buffer accepted");
    CHECK(Sbst_RamMarchC(region, 0U) == SBST_E_PARAM, "Zero length accepted");

    /* 3. Full host-mode Sbst_Run (target-only checks compiled out) */
    CHECK(Sbst_Run() == SBST_OK, "Sbst_Run failed on host");

    printf("PASS: SBST RAM March C- (healthy pass, param rejection)\n");
    return PASS;
}

/*=====================================================================================
 * Test dispatcher
 *====================================================================================*/
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: unit_tests --crc-kat|--e2e-p5|--e2e-p22|--e2e-null|"
               "--ipc|--asild|--wdgm|--dem|--sbst\n");
        return FAIL;
    }
    if (strcmp(argv[1], "--crc-kat")  == 0) { return test_crc_kat();  }
    if (strcmp(argv[1], "--e2e-p5")   == 0) { return test_e2e_p5();   }
    if (strcmp(argv[1], "--e2e-p22")  == 0) { return test_e2e_p22();  }
    if (strcmp(argv[1], "--e2e-null") == 0) { return test_e2e_null(); }
    if (strcmp(argv[1], "--ipc")      == 0) { return test_ipc();      }
    if (strcmp(argv[1], "--asild")    == 0) { return test_asild();    }
    if (strcmp(argv[1], "--wdgm")     == 0) { return test_wdgm();     }
    if (strcmp(argv[1], "--dem")      == 0) { return test_dem();      }
    if (strcmp(argv[1], "--sbst")     == 0) { return test_sbst();     }

    printf("Unknown test: %s\n", argv[1]);
    return FAIL;
}
