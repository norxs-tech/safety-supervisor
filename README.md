# norxs Autonomous Safety-Supervisor Gateway
### AUTOSAR R25-11 ASIL-D SEooC — NXP S32G Cortex-M7 Reference Implementation

**norxs Technology LLC** | Safety Engineering, Built from the Ground Up.

[![CI](https://github.com/norxs-tech/autonomous-safety-supervisor/actions/workflows/ci.yml/badge.svg)](https://github.com/norxs-tech/autonomous-safety-supervisor/actions)
[![License](https://img.shields.io/badge/license-norxs%20RI%20v1.0-blue)](LICENSE)
[![Standard](https://img.shields.io/badge/standard-AUTOSAR%20R25--11-green)]()
[![Safety](https://img.shields.io/badge/safety-ISO%2026262%20ASIL--D-red)]()
[![MISRA](https://img.shields.io/badge/MISRA-C%3A2023-orange)]()

---

## What This Is

A production-grade **AUTOSAR R25-11 Classic Platform** reference implementation of
the Autonomous Safety-Supervisor Gateway, operating as a Safety Element out of Context
(SEooC) on the NXP S32G SoC Cortex-M7 cluster. It is the ultimate arbitration layer
between AI-computed driving commands and physical vehicle chassis actuators — ensuring
that no command from an AI domain can exceed the physical safety envelope derived from
real-time road conditions.

**This is the software we build for our clients — shown here as a reference.**

> **Companion repository:** [norxs SOA Gateway](https://github.com/norxs-tech/autosar-soa-gateway)
> — the Cortex-A53 / QNX SOME/IP & DDS middleware that feeds this supervisor.

---

## System Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│  NXP S32G SoC                                                    │
│                                                                  │
│  ┌───────────────────────────┐   ┌──────────────────────────┐   │
│  │  Cortex-A53  (QNX 8.0)   │   │  Cortex-M7 (AUTOSAR R25) │   │
│  │                           │   │                          │   │
│  │  ┌─────────────────────┐  │   │  ┌────────────────────┐  │   │
│  │  │  SOA Gateway        │  │   │  │SwcSafetyArbitrator │  │   │
│  │  │  (SOME/IP · DDS)    │  │   │  │ μ-Adaptive Envelope│  │   │
│  │  │  [separate repo]    │  │   │  │ ASIL-D Redundancy  │  │   │
│  │  └──────────┬──────────┘  │   │  └────────┬───────────┘  │   │
│  │             │             │   │           │               │   │
│  │  ┌──────────▼──────────┐  │   │  ┌────────▼───────────┐  │   │
│  │  │  app_idps           │  │   │  │SwcVehicleDynamics  │  │   │
│  │  │  (Token Bucket IDPS │  │   │  │ Burckhardt μ model │  │   │
│  │  │   UN R155 Firewall) │  │   │  │ Ackermann geometry │  │   │
│  │  └──────────┬──────────┘  │   │  └────────┬───────────┘  │   │
│  │             │             │   │           │               │   │
│  │  ┌──────────▼──────────┐  │   │  ┌────────▼───────────┐  │   │
│  │  │  HSE Crypto Engine  │  │   │  │  SwcSafeStateMgr   │  │   │
│  │  │  CMAC-AES-128       │◄─┼───┼─►│  Smooth-Step FSM   │  │   │
│  │  │  RSA-4096 OTA       │  │   │  │  Decel Ramp        │  │   │
│  │  └──────────┬──────────┘  │   │  └────────────────────┘  │   │
│  │             │             │   │                          │   │
│  │  ┌──────────▼──────────┐  │   │  BSW Layer:              │   │
│  │  │  IPC Ring Buffer    │◄─┼───┼─►E2E · WdgM · DEM · CSM │   │
│  │  │  Lock-Free SRAM CDD │  │   │  OtaRollback · MemMap   │   │
│  │  └─────────────────────┘  │   │                          │   │
│  └───────────────────────────┘   └──────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

---

## Module Inventory

### BSW — Basic Software (AUTOSAR R25-11)

| Module | Files | Purpose | Standard |
|--------|-------|---------|----------|
| **E2E** | `E2E.h/c` | Profile 5 (CRC-16-CCITT, variable-len) + Profile 22 (4-bit counter, fixed-len) | AUTOSAR SWS_E2ELibrary |
| **WdgM** | `WdgM.h/c` | Alive + deadline + logical supervision, HW SWT service (5ms) | AUTOSAR SWS_WatchdogManager |
| **DEM** | `Dem.h/c` | 16-event DTC catalogue (0xC101–0xC502), freeze frame, UDS status | ISO 14229-1, AUTOSAR SWS_DEM |
| **CSM** | `Csm.h/c` | NXP HSE MU interface: CMAC-AES-128, AES-256-CBC, RSA-4096, TRNG | AUTOSAR SWS_CSM, FIPS 197 |
| **OtaRollback** | `OtaRollback.c` | Dual-bank OTA rollback via HSE signature + WdgM health gate | UN R156 |
| **MemMap** | `MemMap.h` | AUTOSAR section pragmas: SAFETY_RAM, SHARED_SRAM, CALIB_DATA | AUTOSAR MemMap |

### CDD — Complex Device Driver

| Module | Files | Purpose | Standard |
|--------|-------|---------|----------|
| **IPC_RingBuffer** | `IPC_RingBuffer.h/c` | 2-channel SPSC lock-free ring (16 slots, 64B cache-line isolated), LDREX/STREX atomic | AUTOSAR SWS_CDD |

### RTE — Runtime Environment

| Module | Files | Purpose |
|--------|-------|---------|
| **Rte_SafetyArbitrator** | `Rte_SafetyArbitrator.h` | IRV types: SafeStateActive, MaxSteerAngle, MaxYawRate, MuRoad, VehicleSpeed |
| **Rte Stubs** | `Rte_SafetyArbitrator_Stubs.c` | VFB `Rte_Read_*` / `Rte_Write_*` implementations |

### SWC — Software Components (ASIL-D)

| Module | Files | Purpose | Standard |
|--------|-------|---------|----------|
| **SwcSafetyArbitrator** | `SwcSafetyArbitrator.c` | μ-adaptive steer/yaw envelope, ASIL-D redundancy check, AI override | ISO 26262 Part 6 |
| **SwcVehicleDynamics** | `SwcVehicleDynamics.h/c` | Wheel speed δ check, Ackermann yaw plausibility, Burckhardt μ estimation | ISO 26262 Part 6 |
| **SwcSafeStateMgr** | `SwcSafeStateMgr.c` | Cubic Hermite smooth-step steer return + deceleration ramp FSM | ISO 26262 Part 6 |

### QNX A53

| Module | Files | Purpose | Standard |
|--------|-------|---------|----------|
| **app_idps** | `app_idps.c` | Token Bucket rate limiter, SOME/IP whitelist firewall, MAC auth | UN R155 §7.3.3 |

---

## Key Safety Algorithms

### 1. μ-Adaptive Safety Envelope (`SwcSafetyArbitrator.c`)

Physics-based steer and yaw rate limiting derived from real-time road friction:

```
Ay_max    = μ_road × g × 0.70           (30% safety margin on Kamm circle)
steer_max = arctan(Ay_max × L / v²)     (Ackermann geometry, deg)
ψ̇_max    = Ay_max / v                   (yaw rate limit, rad/s)
```

Any AI command exceeding these bounds is **rejected within one 10ms task cycle**.

### 2. Road Friction Estimation — Burckhardt Model (`SwcVehicleDynamics.c`)

```
μ_raw   = √(Ay² + Ax²) / g              (combined slip vector magnitude)
μ_filt  = 0.05 × μ_raw + 0.95 × μ_filt  (1st-order LP filter, α = 0.05)
```

### 3. Smooth-Step Steer Return (`SwcSafeStateMgr.c`)

Jerk-free steering return on safe state entry using Cubic Hermite interpolation:

```
t_norm     = elapsed_cycles / total_cycles
smoothstep = 3t² - 2t³
steer(t)   = steer_start × (1 − smoothstep)   → 0° at t = 1
```

### 4. ASIL-D Bitwise Redundancy (`Platform_Types.h` — ISO 26262-6 Table 9)

All safety-critical boolean state variables stored as (x, ~x) pairs:

```c
ASIL_D_SET(SA_SafeStateActive, 1UL);    /* writes value AND bitwise inverse */
ASIL_D_CHECK(SA_SafeStateActive);       /* TRUE if (x ^ ~x) == 0xFFFFFFFF  */
```

Single-bit upset detected within one task cycle → immediate safe state entry.

---

## ISO 26262 Compliance

| Measure | Status | Reference |
|---------|--------|-----------|
| Zero heap allocation (linker `HEAP_SIZE = 0`) | ✅ | §7.4.11 |
| MISRA C:2023 compliance (`-Werror`) | ✅ | §7.4.11 |
| Stack usage reports (`.su` files, CI gate) | ✅ | §7.4.10 |
| Watchdog alive + deadline supervision | ✅ `WdgM.c` | §7.4.7 |
| E2E Profile 5 + 22 on IPC channel | ✅ `E2E.c` | §7.5.4 |
| DEM event logging + NvM persistence | ✅ `Dem.c` | §7.4.8 |
| Spatial isolation (MPU via linker script) | ✅ `s32g_m7_safety.ld` | §7.4.3 |
| Hardware watchdog (SWT) integration | ✅ `WdgM_MainFunction` | §7.4.7 |
| ASIL-D bitwise redundancy | ✅ `ASIL_D_REDUNDANT_VAR` macro | Table 9 |
| Freeze frame capture on fault | ✅ `Dem_SetFreezeFrameData` | §7.4.8 |
| Fault Tolerant Time Interval ≤ 20ms | ✅ 10ms task × 2 debounce | §7.4.6 |

---

## Hazard Analysis (HARA — ISO 26262-3)

See [`docs/HARA_ASS_SEooC_001.md`](docs/HARA_ASS_SEooC_001.md) for the complete
Hazard Analysis and Risk Assessment.

| Hazard | ASIL | Safety Goal | FTTI |
|--------|------|-------------|------|
| H-01: Unintended lateral deviation | ASIL-D | Limit steer to μ-envelope | 20ms |
| H-02: Unintended acceleration / brake failure | ASIL-D | Limit accel/decel | 20ms |
| H-03: Yaw instability (rollover) | ASIL-D | Limit yaw rate | 20ms |
| H-04: Delayed safe state entry | ASIL-D | Safe state < 20ms | 20ms |
| H-05: Corrupted AI command injection | ASIL-D | Reject unauth cmds | 1ms |
| H-06: Malicious OTA firmware | ASIL-B | Secure OTA verify | N/A |

---

## Assumptions of Use (AoU)

Key integration responsibilities (full list in `docs/HARA_ASS_SEooC_001.md` §7):

1. M7 10ms safety task must be the highest OS priority (no preemption)
2. Shared SRAM region must be non-cached in both A53 MMU and M7 MPU tables
3. HSE firmware ≥ v3.0.0 must be provisioned with CMAC key in slot `0x00000001`
4. Hardware SWT timeout configured ≥ 50ms
5. OTA firmware images must be signed with key in HSE slot `0x00000002`

---

## Build Instructions

### Prerequisites
- `arm-none-eabi-gcc` ≥ 13.0 (ARM Embedded Toolchain)
- `cmake` ≥ 3.26
- `libm` (statically linked for `sqrtf`, `atanf`)
- QNX SDP 8.0 (for A53 IDPS target only)
- `cppcheck` (for static analysis)

### M7 Safety Core (production)
```bash
cmake -B build/m7 -DTARGET=M7 \
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DCMAKE_OBJCOPY=arm-none-eabi-objcopy \
  -DCMAKE_SIZE=arm-none-eabi-size \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/m7 -j$(nproc)
# Outputs: build/m7/m7_safety_supervisor.elf / .hex / .map
```

### A53 IDPS Daemon (QNX)
```bash
source /path/to/qnx800/qnxsdp-env.sh
cmake -B build/a53 -DTARGET=A53 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/qnx_a53_toolchain.cmake
cmake --build build/a53 -j$(nproc)
```

### Host-Native Unit Tests (CI / development)
```bash
cmake -B build/test -DTARGET=TEST -DCMAKE_BUILD_TYPE=Debug
cmake --build build/test -j$(nproc)
ctest --test-dir build/test -V
```

### Stack Usage Analysis
```bash
find build/m7 -name "*.su" | xargs cat | sort -t$'\t' -k2 -rn | head -20
# CI will FAIL if any "?" (unbounded) entry is present
```

### Static Analysis (MISRA C proxy)
```bash
cppcheck --enable=all --error-exitcode=1 \
  -I include/types -I src/m7_rtos/bsw/e2e \
  --std=c11 src/m7_rtos/
```

---

## Repository Structure

```
autonomous-safety-supervisor/
├── include/types/
│   └── Platform_Types.h          AUTOSAR R25-11 types + ASIL-D macros
├── src/
│   ├── m7_rtos/
│   │   ├── bsw/                  Basic Software (AUTOSAR R25-11)
│   │   │   ├── e2e/              E2E Profile 5 & 22
│   │   │   ├── wdgm/             Watchdog Manager (LET monitoring)
│   │   │   ├── dem/              Diagnostic Event Manager + DTC catalogue
│   │   │   ├── csm/              Crypto Service Manager (NXP HSE MU)
│   │   │   ├── memmap/           AUTOSAR MemMap section pragmas
│   │   │   └── OtaRollback.c     Dual-bank OTA rollback FSM
│   │   ├── cdd/                  Complex Device Driver
│   │   │   └── ipc_ringbuffer/   Lock-free cross-core SRAM ring buffer
│   │   ├── rte/                  Runtime Environment (auto-generated)
│   │   │   ├── Rte_SafetyArbitrator.h
│   │   │   └── Rte_SafetyArbitrator_Stubs.c
│   │   └── swc/                  Software Components (ASIL-D)
│   │       ├── swc_safety_arbitrator/   μ-adaptive envelope arbitration
│   │       ├── swc_vehicle_dynamics/    Sensor fusion + Burckhardt μ
│   │       └── swc_safe_state_mgr/      Smooth-step FSM
│   └── qnx_a53/
│       └── app_idps/             Token Bucket IDPS daemon (UN R155)
├── tools/
│   └── s32g_m7_safety.ld         GNU LD linker script (MPU region layout)
├── docs/
│   └── HARA_ASS_SEooC_001.md     Hazard Analysis & Risk Assessment
├── .github/
│   ├── workflows/ci.yml          5-job CI: tests · M7 build · stack · lint · compliance
│   ├── ISSUE_TEMPLATE/           Bug report template
│   └── PULL_REQUEST_TEMPLATE.md
├── CMakeLists.txt                Multi-target build (M7 / A53 / TEST)
├── LICENSE                       norxs Reference Implementation License v1.0
├── CHANGELOG.md                  Full version history
├── CONTRIBUTING.md               Coding standards & contribution guide
└── README.md                     This file
```

---

## Related Repository

This project is the **Cortex-M7 safety supervisor** half of the norxs NXP S32G
dual-core safety architecture. The companion repository implements the
**Cortex-A53 SOA Gateway** (SOME/IP & DDS middleware):

👉 **[norxs/autosar-soa-gateway](https://github.com/norxs-tech/autosar-soa-gateway)**
— SOME/IP & DDS integration, RBAC firewall, E2E Profile 5, HSE adapter, rate limiter,
safety arbitrator (A53 side), QNX/POSIX, AUTOSAR C++14

Together these two repositories form a complete, production-grade reference
implementation of a mixed-criticality autonomous vehicle safety architecture
on the NXP S32G SoC.

---

## Commercial Licensing & Services

This reference implementation is published under the
**norxs Reference Implementation License v1.0**.
Commercial use requires a separate license agreement.

**norxs Technology LLC** offers:
- Full production source rights for ASIL-D deployment
- ISO 26262 safety evidence package (FMEA, FTA, DFA, HARA)
- UN R155 / ISO 21434 cybersecurity artifact package
- ASPICE process documentation
- Long-term engineering support and maintenance

**Contact:** https://norxs.com
---

## Standards

ISO 26262 · AUTOSAR R25-11 · MISRA C:2023 · UN R155 · UN R156 · ISO 14229-1 · ASPICE

---

*(c) 2026 norxs Technology LLC. All rights reserved.*
