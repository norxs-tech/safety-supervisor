# =====================================================================================
# Hazard Analysis and Risk Assessment (HARA) — Autonomous Safety-Supervisor Gateway
# Document: ASS-SEooC-HARA-001
# Version:  0.9.0-RC1
# Date:     2026-06-01
# Author:   norxs-lab
# Standard: ISO 26262-3 (Item Definition & HARA)
# Project:  Autonomous Safety-Supervisor Gateway (SEooC)
# =====================================================================================

## 1. Item Definition

The Autonomous Safety-Supervisor Gateway is a Safety Element out of Context (SEooC)
designed for integration into any Software-Defined Vehicle zonal architecture.
It acts as the ultimate arbitration layer between AI-computed driving commands and
physical vehicle chassis actuators.

**Item Boundary:**
- IN:  AI driving commands (steer, acceleration, yaw) via Ethernet SOME/IP / DDS
- IN:  Chassis sensor data (wheel speed, yaw rate, lateral acceleration)
- OUT: Arbitrated chassis commands (to EPS, eBrake, Powertrain)
- OUT: Safe state commands (to all chassis ECUs)
- OUT: Diagnostic events (to vehicle OBD system)

---

## 2. Hazard Analysis

| ID   | Hazard Description                          | Operational Situation        |
|------|---------------------------------------------|------------------------------|
| H-01 | Unintended lateral deviation (steer fault)  | Highway at v > 100 km/h     |
| H-02 | Unintended acceleration / failure to brake  | Urban intersection           |
| H-03 | Loss of control (yaw instability)           | High-speed cornering on wet  |
| H-04 | Delayed safe state entry (latency fault)    | Emergency brake scenario     |
| H-05 | Corrupted AI command injection (cyber)      | Any operational situation    |
| H-06 | Firmware replacement with malicious image   | OTA update window            |

---

## 3. Risk Assessment (ISO 26262-3 §6.4)

| Hazard | Severity (S) | Exposure (E) | Controllability (C) | ASIL   | Safety Goal               |
|--------|-------------|--------------|---------------------|--------|---------------------------|
| H-01   | S3          | E4           | C3                  | ASIL-D | SG-01: Limit steer rate   |
| H-02   | S3          | E4           | C2                  | ASIL-D | SG-02: Limit accel decel  |
| H-03   | S3          | E3           | C3                  | ASIL-D | SG-03: Limit yaw rate     |
| H-04   | S3          | E4           | C3                  | ASIL-D | SG-04: <20ms safe state   |
| H-05   | S3          | E4           | C3                  | ASIL-D | SG-05: Reject unauth cmds |
| H-06   | S3          | E1           | C3                  | ASIL-B | SG-06: Secure OTA verify  |

---

## 4. Safety Goals

| ID    | Safety Goal                                                            | ASIL   | FTTI   |
|-------|------------------------------------------------------------------------|--------|--------|
| SG-01 | The system shall limit steering angle to the μ-adaptive envelope        | ASIL-D | 20ms   |
| SG-02 | The system shall reject acceleration commands violating physics bounds  | ASIL-D | 20ms   |
| SG-03 | The system shall limit yaw rate to prevent rollover                    | ASIL-D | 20ms   |
| SG-04 | The system shall enter safe state within 20ms of envelope violation    | ASIL-D | 20ms   |
| SG-05 | The system shall reject AI commands with failed MAC verification       | ASIL-D | 1ms    |
| SG-06 | The system shall verify firmware integrity before OTA activation       | ASIL-B | N/A    |

Fault Tolerant Time Interval (FTTI) = 20ms (consistent with 10ms task cycle × 2)

---

## 5. Functional Safety Requirements (FSR) → Software Safety Requirements (SSR)

### SG-01 → FSR-01 → SSR-01
**SSR-01:** SwcSafetyArbitrator shall compute μ-adaptive steer envelope every 10ms using
Ackermann geometry model. Override AI steer command if |steer_req| > steer_max(μ, v).

**Mechanism:** SA_ComputeMaxSteerAngle_deg() in SwcSafetyArbitrator.c
**Verification:** Unit test SA_UT_001 through SA_UT_015

### SG-04 → FSR-04 → SSR-04
**SSR-04:** Safe state entry (zero steer command + decel ramp) shall be initiated within
two 10ms task cycles of envelope violation detection (debounce = 2 cycles = 20ms maximum).
This satisfies FTTI = 20ms for SG-04 per ISO 26262-3 §6.4.6.

**Mechanism:** SA_EnterSafeState() + SwcSafeStateMgr STATE_ACTIVE_RETURN
**Verification:** Integration test INT_007 (hardware-in-loop)

### SG-05 → FSR-05 → SSR-05
**SSR-05:** All AI Command Service (0x1235) SOME/IP packets shall be verified via
CMAC-AES-128 using HSE before forwarding to M7 safety core via IPC.

**Mechanism:** IDPS_VerifyMac() → Csm_MacVerify() → HSE MU0
**Verification:** Penetration test PT_003 (replay attack, forged command injection)

---

## 6. ASIL-D Implementation Measures

| Measure                          | Location                    | ISO 26262-6 Ref     |
|----------------------------------|-----------------------------|---------------------|
| Bitwise redundancy (x, ~x)       | SwcSafetyArbitrator.c       | Table 9, Row 1d     |
| Memory spatial isolation (MPU)   | s32g_m7_safety.ld + MemMap  | §7.4.3              |
| Logical Execution Time (WdgM)    | WdgM.c Checkpoints          | §7.4.7              |
| E2E Protection Profile 5/22      | E2E.c / IPC_RingBuffer.c    | §7.5.4              |
| MISRA C:2023 compliance          | All M7 sources              | §7.4.11             |
| No dynamic memory allocation     | CMakeLists (no heap)        | §7.4.11             |
| Hardware watchdog service        | WdgM_MainFunction()         | §7.4.7              |
| Secure boot (HSE)                | Csm_Init() / OtaRollback.c  | §7.4.14             |
| Freeze frame / DEM logging       | Dem_SetFreezeFrameData()    | §7.4.8              |

---

## 7. Assumptions of Use (AoU) — SEooC Integration Requirements

Integrators of this SEooC must guarantee:

AoU-01: The M7 10ms safety task shall be the highest-priority OS task (no preemption).
AoU-02: Shared SRAM IPC region shall be mapped as non-cached in both A53 MMU and M7 MPU.
AoU-03: HSE firmware >= v3.0.0 with CMAC-AES-128 key provisioned in slot 0x00000001.
AoU-04: Hardware SWT timeout shall be set to ≥ 50ms (WdgM services every 5ms).
AoU-05: The vehicle dynamics sensors shall provide data within ±2% accuracy per SAE J3016.
AoU-06: The OTA service shall sign firmware images with the key in HSE slot 0x00000002.

---

*This document is a living artifact. Updates are tracked in the project HARA tool
(IBM DOORS / PTC Integrity) and supersede this Markdown representation for certification.*
