# Changelog

All notable changes to the **norxs Autonomous Safety-Supervisor Gateway (SEooC)**
are documented here. This project follows [Semantic Versioning](https://semver.org/).

---

## [0.9.2] — 2026-06-12

### Fixed — Defects That Would Fail on Real Silicon

- **FPU was never enabled.** The image is compiled `-mfloat-abi=hard` and the
  SWCs use `sqrtf`/`atanf`, but `Reset_Handler` never granted CP10/CP11 access in
  CPACR — on hardware, the first FP instruction raises UsageFault, whose handler
  stops servicing the watchdog → permanent reset loop. CPACR is now enabled as
  the very first action in `Reset_Handler` and re-verified by SBST (`SBST_E_FPU`).
- **The MPU was claimed but never programmed.** Linker script, README, and docs
  referenced "MPU-enforced regions" with zero MPU configuration code. New
  `os/Mpu_S32G_M7.c` programs a 6-region PMSAv7 map mirroring the linker memory
  map (ITCM execute/no-write, DTCM/SRAM RW+XN, Shared SRAM non-cacheable
  shareable, calibration flash RO, peripherals device) with **PRIVDEFENA = 0** —
  any access outside the map raises MemManage, routed to safe state. MemManage
  is enabled as a dedicated fault (no HardFault escalation).
- **Scheduler could silently skip activations.** The `tick % period` dispatch
  lost task releases whenever the loop was delayed past a tick boundary. Replaced
  with per-task release counters (wrap-safe signed comparison); missed releases
  are now detected, reported as the new `DEM_EVENT_OS_TASK_OVERRUN`, and the
  schedule re-synchronizes by whole periods without activation bursts.
- **Stack-canary failure path led to newlib `_exit`** (an untimed spin). A
  project-owned `__stack_chk_fail` now forces the hardware safe state via
  watchdog expiry, and `__stack_chk_guard` is provided with a provisioning AoU
  (HSE TRNG re-seed). Side effect: the newlib reent/stack-protector dependency
  chain is no longer linked (−2.7KB text).

### Added — Boot Diagnostics

- **`os/Sbst.c`**: startup built-in self-test executed before any SWC init —
  RAM March C- subset on a dedicated DTCM region (ISO 26262-5 Table D.1, ≈ 90%
  stuck-at DC), vector table integrity (initial SP + thumb-bit reset vector via
  the new `__isr_vector_start` linker symbol), and FPU-enabled check. A failed
  self-test reports `DEM_EVENT_SBST_FAILURE` and never starts the scheduler.
- DEM event catalogue grown 16 → 18 (`OS_TASK_OVERRUN`, `SBST_FAILURE`).

### Added — Verification & Evidence

- 9th unit test `sbst_ram_march_test` (March C- healthy pass + parameter
  rejection + host-mode `Sbst_Run`).
- **Coverage gate in CI**: gcov-instrumented build + gcovr, line coverage gated
  at ≥ 80% over the unit-tested BSW/CDD/OS modules (measured 81.8% line, 89.7%
  function); Cobertura XML uploaded as a CI artifact.
- **`docs/SRS_TRACEABILITY.md`**: software safety requirements matrix —
  24 SSR items traced requirement → ASIL → implementation → verification method
  → CI job, with coverage summary and integrator AoU register.

### Changed

- CMake project version 0.9.2; `NORXS_COVERAGE` option for instrumented test
  builds; cppcheck CI gate extended over `src/m7_rtos/os/`.

---

## [0.9.1] — 2026-06-12

### Fixed — Safety-Relevant Defects

- **`bsw/e2e`: corrected the CRC-16-CCITT lookup table.** The previous table was
  internally consistent (protect/check round-trips passed) but did **not**
  implement standard CRC-16/CCITT-FALSE behaviour — the known-answer vector
  `"123456789"` produced `0x69F1` instead of `0x29B1`. Interoperability with any
  standards-compliant peer (including the A53 `autosar-soa-gateway`) would have
  failed on every frame. The table is regenerated from the reference bitwise
  algorithm and locked in CI by `crc16_ccitt_kat_test`.
- **`bsw/e2e`: fixed NULL-pointer dereference in `E2E_P5Check` / `E2E_P22Check`**
  (CERT EXP34-C): when `Status == NULL_PTR` the error path still wrote
  `*Status`. Regression-tested by `e2e_null_rejection_test` under ASan.
- **`bsw/dem`: `Dem_SetEventStatus` returned `E_OK` for invalid status values**
  (the `default` rejection branch was overwritten by a trailing assignment).
  Status values are now validated before mutating the event entry.
- **`rte`: missing prototypes for `Rte_Write_SafetyArbitrator_VehicleDynamics_Data`
  and `Rte_UpdateAiCommandFromIpc`** caused implicit function declarations in
  `SwcVehicleDynamics.c`. Prototypes added to `Rte_SafetyArbitrator.h`.
- **`rte`: volatile qualifiers no longer cast away** when reading inter-runnable
  variables (3 sites).
- **`tools/s32g_m7_safety.ld`: added `__ssm_bss_start/end` symbols** — the Safe
  State Manager NOLOAD BSS section had no boundary symbols, so startup could not
  zero it (uninitialized FSM state on cold boot).

### Added — OS Layer (new)

- **`src/m7_rtos/os/startup_s32g_m7.c`**: ARMv7-M vector table, `Reset_Handler`
  (.data copy + .bss and MPU-isolated SWC BSS zeroing), fault handlers that
  force safe state via watchdog expiry, SysTick 1ms OS tick, and a static cyclic
  scheduler (5ms WdgM / 10ms safety chain / 100ms diagnostics + OTA) with
  run-to-completion semantics meeting the 20ms FTTI argument. The M7 ELF
  previously linked with **zero text bytes** (no entry symbol; all code
  garbage-collected) — it now produces a real 10.7KB ITCM image.
- CMSIS-equivalent `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`
  implementations (previously declared `extern` but provided nowhere).
- IPC buffers are now actually placed in the A53-visible **Shared SRAM** section
  via `SHARED_SRAM_ATTR` on target builds (previously landed in DTCM `.bss`).

### Added — API

- `IPC_RingBuffer_ProtectFrame()`: producer-side E2E Profile 22 protection.
  Previously no public API could construct a frame the consumer-side E2E check
  would accept (the DataID list is channel-private).
- `Dem_GetEventUdsStatus()`: ISO 14229-1 §11.2.1 composite status readout (UDS
  service $19 support) with AUTOSAR SWS traceability tag.
- `OtaRollback.h` and `SwcSafeStateMgr.h`: public headers for previously
  prototype-less modules.

### Added — Verification

- Unit-test suite expanded from 3 to **8 ctest cases** (CRC known-answer,
  corruption-detection negatives, full IPC round-trip with boundary conditions,
  ASIL-D redundancy macro upset detection, WdgM alive/deadline supervision, DEM
  debounce + UDS bits). All run under ASan + UBSan; JUnit XML report uploaded as
  a CI artifact.
- `bsw/wdgm` is now host-testable through a `UNIT_TEST_BUILD` SWT register shadow.
- Fixed `ASIL_D_SET` macro: the inverse copy is derived from the stored variable
  (type-correct on any host word size).

### Added — Supply-Chain & Security Compliance

- **`SECURITY.md`** — coordinated vulnerability disclosure policy with triage
  SLAs (OpenChain ISO/IEC 18974:2023, NIST SP 800-216 aligned).
- **`sbom/safety-supervisor-0.9.1.spdx.json`** — SPDX 2.3 SBOM cataloguing every
  repository file with SHA-1/SHA-256 checksums; generator at
  `tools/generate_sbom.py`.
- **`NOTICE`** — third-party and trademark notices (zero bundled third-party code).
- **`docs/COMPLIANCE.md`** — OpenChain ISO/IEC 5230 & 18974 requirement mapping
  and NIST CSF 2.0 function-by-function implementation table.
- **CI Job 6 `supply-chain-compliance`** — fails the build on missing compliance
  artifacts, incomplete SBOM coverage, or missing copyright headers.

### Changed — Build & CI

- Removed all five `-Wno-error=` downgrades: the M7 target now genuinely builds
  with the full `-Werror` wall (`-Wmissing-prototypes -Wcast-qual -Wconversion
  -Wshadow -Wredundant-decls …`) with **zero warnings**.
- `tests/test_main.c` (hosted stdio code) removed from the freestanding M7
  production image source list.
- M7 executable now carries the `.elf` suffix; `.hex`/`.bin` added to CI
  artifacts; `arm-none-eabi-size` step path corrected.
- cppcheck gate now covers `src/qnx_a53/` and uses a documented deviation-record
  suppression list (`tools/cppcheck_suppressions.txt`) — error/warning level
  findings are never suppressed.
- Build documentation corrected: configuration flag is `-DNORXS_TARGET=…`.

---

## [0.9.0-RC1] — 2026-06-01

### Initial public release — norxs Technology LLC

First public release of the Autonomous Safety-Supervisor Gateway SEooC — a
production-grade AUTOSAR R25-11 Classic Platform reference implementation for
the NXP S32G SoC Cortex-M7 safety cluster.

### Added — BSW (Basic Software)

#### `E2E` — End-to-End Protection Library
- **Profile 5** (CRC-16-CCITT): variable-length IPC AI command frames (up to 512B)
- **Profile 22** (CRC-16-CCITT, 4-bit counter): fixed chassis safety command frames
- CRC-16-CCITT lookup table (256 entries) in `.rodata` — zero runtime init cost
- `E2E_CRC16_CCITT()` exposed for host-native unit testing

#### `WdgM` — Watchdog Manager
- Alive supervision: expected indication count ± tolerance window per entity
- Deadline supervision: minimum/maximum elapsed time between checkpoints
- Logical program flow (graph) supervision via checkpoint sequence validation
- Hardware SWT (Software Watchdog Timer) service via `WdgM_MainFunction()` (5ms period)
- Four Supervised Entities: `SA`, `VD`, `SSM`, `IPC_HANDLER`
- DEM integration: `DEM_EVENT_WDGM_ALIVE_SUPERVISION_FAIL` / `DEADLINE_FAIL`

#### `DEM` — Diagnostic Event Manager
- 16-event DTC catalogue (0xC101–0xC502) covering Safety, Dynamics, IPC, OTA, CSM
- Freeze frame capture: vehicle speed, yaw rate, steer angle, lateral accel, AI override count
- UDS-compatible status byte (ISO 14229-1 §11.2.1)
- NvM write hook on confirmed failure (`SWS_Dem_00234`)

#### `CSM` — Crypto Service Manager (NXP HSE interface)
- `Csm_MacGenerate()` / `Csm_MacVerify()`: CMAC-AES-128 via HSE MU
- `Csm_AES256_Encrypt()`: AES-256-CBC for NvM data protection
- `Csm_GenerateRandom()`: TRNG-based secure random byte generation
- `Csm_GetHseStatus()`: HSE firmware version + lifecycle state query
- 5 pre-provisioned key slots: IPC MAC, OTA verify, Secure Boot, TLS, NvM encrypt
- `CSM_HSE_TIMEOUT_CYCLES = 10000` bounded polling (no infinite wait)

#### `OtaRollback` — Dual-bank OTA Rollback State Machine
- Bank validity check via HSE RSA-4096 signature verification
- Automatic rollback on: WdgM deadline failure, E2E error accumulation, boot failure
- `DEM_EVENT_OTA_ROLLBACK_TRIGGERED` on activation

#### `MemMap` — AUTOSAR Memory Section Pragmas
- Section pragmas for: `SAFETY_RAM`, `SHARED_SRAM`, `CALIB_DATA`, `CODE`, `CONST`
- Compliant with AUTOSAR R25-11 MemMap specification

### Added — CDD (Complex Device Driver)

#### `IPC_RingBuffer` — Lock-Free Cross-Core SRAM Communication
- 2-channel SPSC ring buffer (A53→M7 command, M7→A53 status)
- 16 slots × 80-byte `IPC_FrameType` (32-byte aligned, cache-line isolated)
- `WriteIdx` and `ReadIdx` padded to separate 64-byte cache lines (false-sharing prevention)
- LDREX/STREX atomic index update (ARMv7-M exclusive load/store)
- `__DSB()` / `__DMB()` CMSIS barriers before/after cross-core writes
- E2E CRC-16 + counter embedded in every frame header
- Frame types: AI steer cmd, AI accel cmd, safety status, DEM event

### Added — RTE (Runtime Environment)

#### `Rte_SafetyArbitrator` — Auto-Generated RTE Interface
- Inter-Runnable Variables (IRV): `SA_SafeStateActive`, `SA_MaxSteerAngle_deg`,
  `SA_MaxYawRate_radps`, `SA_MuRoad`, `SA_VehicleSpeed_mps`
- `Rte_Read_*` / `Rte_Write_*` macros for VFB-compliant data access
- `ASIL_D_REDUNDANT_VAR` storage for all safety-critical IRVs

### Added — SWC (Software Components)

#### `SwcSafetyArbitrator` — AI Command Plausibility & μ-Adaptive Safety Envelope
- Static bounds check: steer lock-to-lock, peak acceleration, rollover threshold
- Dynamic envelope: `steer_max = arctan(μ·g·0.7 · L / v²)` (Ackermann geometry)
- Yaw rate limit: `ψ̇_max = μ·g·0.7 / v`
- ASIL-D redundancy check (`ASIL_D_CHECK`) on all safety-critical variables before arbitration
- WdgM checkpoint instrumentation: ENTRY, ENVELOPE_CHECK, EXIT
- Override action: DEM event + freeze frame + NvM persist + Safe State command

#### `SwcVehicleDynamics` — Sensor Fusion & Road Friction Estimation
- Wheel speed L/R delta check (threshold: 2.0 rad/s)
- Kinematic yaw rate from wheel speeds vs IMU gyro plausibility (threshold: 0.3 rad/s)
- Burckhardt friction model: `μ_raw = √(Ay² + Ax²) / g`
- 1st-order LP filter: `μ_filt[n] = 0.05·μ_raw + 0.95·μ_filt[n-1]`

#### `SwcSafeStateMgr` — Smooth-Step Steer Return FSM
- Cubic Hermite smooth-step interpolation: `s(t) = 3t² - 2t³`
- Jerk-bounded deceleration ramp: `a(t) = a_start·(1 - s(t))`
- FSM: IDLE → ACTIVE → RETURN → COMPLETE with DEM event at each transition

### Added — QNX A53

#### `app_idps` — Intrusion Detection & Prevention System Daemon
- Token Bucket rate limiter per SOME/IP client ID (burst 20, refill 5/ms)
- SOME/IP client ID whitelist firewall (UN R155 §7.3.3)
- MAC verification via IPC call to M7 CSM
- Audit log ring buffer (128 entries) with POSIX `clock_gettime(CLOCK_MONOTONIC)`

### Added — Build & Quality

#### `CMakeLists.txt`
- Three targets: `M7` (arm-none-eabi), `A53` (QNX aarch64), `TEST` (host-native)
- `-Werror` on all targets — zero warnings policy
- `-fstack-usage` → `.su` files for stack analysis (ISO 26262 §7.4.10)
- ASan + UBSan on TEST target for host-native CI
- `POST_BUILD`: auto-generates `.hex` and `.bin` for flash programming
- `misra_check` target placeholder for PC-lint Plus / Polyspace integration

#### `.github/workflows/ci.yml`
- Job 1: Host-native unit tests (ASan + UBSan, Ubuntu 24.04)
- Job 2: M7 cross-compile (arm-none-eabi-gcc, -Werror)
- Job 3: Stack usage analysis (unbounded stack `?` detection = CI failure)
- Job 4: MISRA C static analysis (cppcheck proxy, zero-error gate)

### Compliance Summary (v0.9.0-RC1)

| Standard | Coverage |
|----------|----------|
| ISO 26262 Part 6 ASIL-D | `ASIL_D_REDUNDANT_VAR`, WdgM LET, E2E Profile 5/22, zero heap, stack bounds |
| AUTOSAR R25-11 Classic | BSW (E2E, WdgM, DEM, CSM), CDD, RTE, SWC architecture |
| MISRA C:2023 | `-Werror`, `-Wpedantic`, `-Wmissing-prototypes`, `-Wstrict-prototypes` |
| UN R155 §7.3 | IDPS Token Bucket, CMAC-AES-128 IPC auth, DEM audit trail |
| ISO 14229-1 (UDS) | DEM event status byte, DTC format `0xCXYZ` |

---

*For commercial licensing, ASIL-D safety evidence packages, and ASPICE process
documentation, contact norxs Technology LLC at https://norxs.com*
