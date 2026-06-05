# Changelog

All notable changes to the **norxs Autonomous Safety-Supervisor Gateway (SEooC)**
are documented here. This project follows [Semantic Versioning](https://semver.org/).

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
