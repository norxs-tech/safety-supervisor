# Software Safety Requirements & Traceability Matrix

Document: ASS-SEooC-SRS-001 · Version 0.9.2 · 2026-06-12
Project: norxs Autonomous Safety-Supervisor Gateway (SEooC)
Scope: ISO 26262-6 §6 (software safety requirements) and §9/§11 (verification)
traceability for the Cortex-M7 safety core. Hazard-level derivation: see
`docs/HARA_ASS_SEooC_001.md`. Compliance program: see `docs/COMPLIANCE.md`.

Legend — Verification: **UT** unit test (ctest case, ASan+UBSan), **SA** static
analysis (cppcheck CI gate), **BLD** build-time gate (`-Werror` / linker /
stack-usage CI), **REV** design review artifact. CI job names refer to
`.github/workflows/ci.yml`.

---

## 1. End-to-End Protection (BSW/E2E)

| Req ID | Requirement (shall…) | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-E2E-001 | Compute CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) bit-exact to the public reference vector `"123456789" → 0x29B1` | D | `E2E.c::E2E_CRC16_CCITT` (ROM table) | UT `crc16_ccitt_kat_test` | unit-tests |
| SSR-E2E-002 | Detect any single-bit corruption in a Profile 5 protected frame (payload or CRC field) | D | `E2E_P5Protect/Check` | UT `e2e_profile5_test` (bit-flip negatives) | unit-tests |
| SSR-E2E-003 | Reject frames whose length differs from the configured DataLength | D | `E2E_P5Protect/Check` guards | UT `e2e_profile5_test` | unit-tests |
| SSR-E2E-004 | Detect corruption and counter anomalies on Profile 22 frames (4-bit counter, DataID list) | D | `E2E_P22Protect/Check` | UT `e2e_profile22_test` | unit-tests |
| SSR-E2E-005 | Reject NULL Config/State/Data/Status without dereferencing any output pointer (CERT EXP34-C) | D | guard ordering in `E2E_P5Check`/`E2E_P22Check` | UT `e2e_null_rejection_test` under ASan | unit-tests |

## 2. Inter-Core IPC (CDD/IPC_RingBuffer)

| Req ID | Requirement | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-IPC-001 | Deliver frames producer→consumer unmodified, accepted only after E2E Profile 22 validation | D | `IPC_RingBuffer_Write/Read` + `ProtectFrame` | UT `ipc_ringbuffer_test` (round-trip, payload compare) | unit-tests |
| SSR-IPC-002 | Return `IPC_E_EMPTY` on empty ring and `IPC_E_FULL` at capacity; never overwrite unread frames | D | free-running index full/empty checks | UT `ipc_ringbuffer_test` (boundary cases) | unit-tests |
| SSR-IPC-003 | Reject invalid channel IDs and NULL frame pointers | D | parameter guards | UT `ipc_ringbuffer_test` | unit-tests |
| SSR-IPC-004 | Place IPC buffers in non-cacheable Shared SRAM visible to both cores; maintain cache coherency by explicit clean/invalidate | D | `SHARED_SRAM_ATTR` placement; `SCB_*DCache_by_Addr` (startup); MPU region 3 non-cacheable | BLD (map file: 3328B in SHARED_SRAM) + REV | build-m7 |
| SSR-IPC-005 | Quarantine E2E-failed frames and report `DEM_EVENT_IPC_E2E_ERROR` | D | `Os_IpcHandler10ms` | REV (code) + UT of DEM path `dem_diagnostics_test` | unit-tests |

## 3. Watchdog Supervision (BSW/WdgM)

| Req ID | Requirement | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-WDG-001 | Detect missing alive indications of any supervised entity within one 10ms window and set local + global status FAILED | D | `WdgM_MainFunction` alive evaluation | UT `wdgm_supervision_test` (starvation case) | unit-tests |
| SSR-WDG-002 | Detect entry→exit deadline overrun (> 8ms) and set local status EXPIRED | D | `WdgM_CheckpointReached` deadline check | UT `wdgm_supervision_test` (deadline case) | unit-tests |
| SSR-WDG-003 | Service the hardware SWT only while global status is OK, so a FAILED supervision state leads to hardware reset into safe state | D | SWT service gating in `WdgM_MainFunction` | UT (shadow registers) + REV | unit-tests |
| SSR-WDG-004 | Reject checkpoint reports from unknown supervised entities | D | SEID lookup guard | UT `wdgm_supervision_test` | unit-tests |

## 4. Diagnostics (BSW/DEM)

| Req ID | Requirement | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-DEM-001 | Confirm a DTC only after the configured debounce threshold of PREFAILED reports | B | counter-based debouncing in `Dem_SetEventStatus` | UT `dem_diagnostics_test` | unit-tests |
| SSR-DEM-002 | Maintain ISO 14229-1 §11.2.1 UDS status bits (testFailed, pendingDTC, confirmedDTC) and expose them for service $19 | B | `Dem_GetEventUdsStatus` | UT `dem_diagnostics_test` | unit-tests |
| SSR-DEM-003 | Reject unknown event IDs and undefined status values without mutating event state | B | validation-before-mutation in `Dem_SetEventStatus` | UT `dem_diagnostics_test` (regression for 0.9.1 fix) | unit-tests |

## 5. OS Layer: Startup, Scheduling, FFI (os/)

| Req ID | Requirement | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-OS-001 | Establish a valid C runtime before main(): enable FPU (hard-float ABI prerequisite), copy .data, zero .bss including all MPU-isolated NOLOAD SWC sections | D | `Reset_Handler` | BLD (link, entry symbol) + SBST FPU/vector checks | build-m7 |
| SSR-OS-002 | Execute the 10ms safety chain (IPC→VD→SA→SSM) run-to-completion in fixed order; worst-case fault reaction ≤ FTTI 20ms | D | `Os_Task10ms`, non-preemptive cyclic scheduler | REV (FTTI argument in file header) + SSR-WDG-002 runtime monitor | build-m7 |
| SSR-OS-003 | Enforce spatial FFI: no write to code memory, no execution from RAM, faults on access outside the defined map | D | `Mpu_Init` (PRIVDEFENA=0, 6 PMSAv7 regions); MemManage→safe state | REV + BLD (region map mirrors .ld) | build-m7, misra-lint |
| SSR-OS-004 | Verify RAM integrity at startup with a March test (DC ≈ 90% stuck-at per ISO 26262-5 Table D.1); a failed test shall prevent scheduler start | D | `Sbst_RamMarchC`, `Sbst_Run` gate in `Os_InitRuntime` | UT `sbst_ram_march_test` | unit-tests |
| SSR-OS-005 | Verify vector table integrity and FPU enablement at startup | D | `Sbst_Run` (target checks) | REV + BLD | build-m7 |
| SSR-OS-006 | Detect missed task activations (overrun) and report `DEM_EVENT_OS_TASK_OVERRUN`; re-synchronize the schedule without activation bursts | D | `Os_AdvanceRelease` release-counter scheduler | REV (wrap-safe signed compare) | build-m7 |
| SSR-OS-007 | Route all core faults (HardFault/MemManage/BusFault/UsageFault/stack-canary) to the hardware safe state via watchdog expiry | D | fault handlers + `__stack_chk_fail` override | REV | build-m7 |
| SSR-OS-008 | Prohibit dynamic memory allocation; all stack frames statically bounded | D | zero-heap linker config; `-fstack-usage` | BLD (compliance-scan malloc gate; stack-analysis unbounded gate) | compliance-scan, stack-analysis |

## 6. Data Integrity Mechanisms (Platform)

| Req ID | Requirement | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-PLT-001 | Detect single-bit upsets in safety-critical state variables via bitwise-inverse redundant storage | D | `ASIL_D_REDUNDANT_VAR/SET/CHECK` macros | UT `asil_d_redundancy_test` (upsets in either copy) | unit-tests |

## 7. Security & OTA (BSW/Csm, OtaRollback) — verification by review/integration

| Req ID | Requirement | ASIL | Implementation | Verification | CI Job |
|---|---|---|---|---|---|
| SSR-SEC-001 | Authenticate chassis-bound frames with CMAC-AES-128 via the HSE; report MAC failures to DEM | D | `Csm.c` HSE MU descriptors | SA + REV (HSE HIL required — AoU-5) | misra-lint |
| SSR-SEC-002 | Accept OTA firmware only with valid RSA-4096 signature; roll back on health-gate failure (UN R156) | B | `OtaRollback.c` dual-bank FSM | SA + REV (HIL required — AoU-5) | misra-lint |

---

## Coverage Summary (CI-measured, unit-tested modules)

| Metric | Value | Gate |
|---|---|---|
| Line coverage (e2e, wdgm, dem, ipc_ringbuffer, Sbst) | 81.8% | ≥ 80% (`unit-tests` job, gcovr) |
| Function coverage | 89.7% | report-only |
| Branch coverage | 64.3% | report-only (roadmap: MC/DC via commercial tooling) |

## Open Items / Assumptions of Use for the Integrator

- AoU-5: SSR-SEC-001/002 require hardware-in-the-loop verification against real
  HSE firmware; this repository verifies them by static analysis and review only.
- AoU-6: `OS_CORE_CLOCK_HZ` (default 400MHz) must match the integrator's clock
  tree configuration for correct SysTick timing.
- AoU-7: `__stack_chk_guard` shall be re-seeded from the HSE TRNG during
  production provisioning.
- Roadmap: WdgM logical (program-flow) supervision; MC/DC coverage evidence;
  fault-injection campaign on the March test and E2E checkers.

*(c) 2026 norxs Technology LLC. All rights reserved.*
