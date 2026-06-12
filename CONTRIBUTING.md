# Contributing to norxs Autonomous Safety-Supervisor Gateway

Thank you for your interest in contributing to the norxs Autonomous
Safety-Supervisor Gateway SEooC reference implementation. Because this targets
ISO 26262 ASIL-D, contributions are held to a higher standard than typical
open-source projects.

---

## License Agreement

By submitting a pull request, you agree that your contribution is made under
the **norxs Reference Implementation License v1.0** (see `LICENSE`) and that
you grant norxs Technology LLC a perpetual, irrevocable, royalty-free license
to use, modify, and redistribute your contribution as part of this project.

---

## Coding Standards (Mandatory)

### MISRA C:2023 / AUTOSAR R25-11

| Rule | Requirement |
|------|-------------|
| No dynamic memory | `malloc`, `calloc`, `realloc`, VLAs are forbidden. Use static arrays. |
| No recursion | All call graphs must be statically bounded. No function may call itself directly or indirectly. |
| No `goto` | Except for forward-only error-exit jumps (MISRA C:2023 Rule 15.1 deviation with justification). |
| AUTOSAR naming | Module prefix + type suffix: `WdgM_CheckpointReached()`, `Dem_EventIdType`. |
| MemMap sections | Every new `.c` file must open/close `_START_SEC_` / `_STOP_SEC_` pragmas from `MemMap.h`. |
| ASIL-D variables | Safety-critical boolean/state variables must use `ASIL_D_REDUNDANT_VAR` / `ASIL_D_SET` / `ASIL_D_CHECK` macros. |
| No implicit conversions | Explicit casts required. Use `(uint32)` not C-style silent promotion. |
| Function prototypes | Every function must have a prototype in its corresponding `.h` file with full Doxygen. |

### Doxygen Header (Required in Every File)

```c
/**
 * =====================================================================================
 * @file        [filename]
 * @brief       [Comprehensive one-paragraph description]
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11, [applicable standards]
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 * @note        This is a reference implementation showcasing norxs's SOA architecture.
 * =====================================================================================
 */
```

### WdgM Checkpoint Instrumentation

Every new runnable or periodic task must instrument WdgM checkpoints:

```c
(void)WdgM_CheckpointReached(WDGM_SE_MY_SWC, WDGM_CP_MY_SWC_ENTRY);
/* ... task body ... */
(void)WdgM_CheckpointReached(WDGM_SE_MY_SWC, WDGM_CP_MY_SWC_EXIT);
```

Minimum: ENTRY and EXIT. Add intermediate checkpoints for any computation
exceeding 2ms to enable deadline supervision.

### DEM Events

Every new fault condition must have a corresponding DTC entry:

1. Add `DEM_EVENT_MY_FAULT ((Dem_EventIdType)0xCXYZU)` to `Dem.h`
2. Document the DTC in `CHANGELOG.md` and the HARA if a new hazard applies
3. Call `Dem_SetEventStatus()` + `Dem_SetFreezeFrameData()` at fault detection

### ASIL-D Redundancy

All safety-critical state variables must use redundant storage:

```c
/* Declaration */
ASIL_D_REDUNDANT_VAR(uint32, SA_SafeStateActive);

/* Write */
ASIL_D_SET(SA_SafeStateActive, 1UL);

/* Read with integrity check */
if (ASIL_D_CHECK(SA_SafeStateActive) == FALSE) {
    /* Single-event upset detected — immediate safe state */
    Dem_ReportErrorStatus(DEM_EVENT_SAFETY_ENVELOPE_VIOLATED,
                          DEM_EVENT_STATUS_FAILED);
}
```

---

## Testing Requirements

- Every new algorithm must have a host-native unit test in the `TEST` CMake target
- Stack usage `.su` files must be reviewed — no unbounded `?` entries allowed
- Run the full CI pipeline locally before submitting:

```bash
# Unit tests with sanitizers
cmake -B build/test -DTARGET=TEST -DCMAKE_BUILD_TYPE=Debug
cmake --build build/test --parallel
ctest --test-dir build/test -V

# M7 cross-compile check
cmake -B build/m7 -DTARGET=M7 \
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DCMAKE_OBJCOPY=arm-none-eabi-objcopy \
  -DCMAKE_SIZE=arm-none-eabi-size
cmake --build build/m7 --parallel

# Stack analysis
find build/m7 -name "*.su" | xargs grep "?" && echo "UNBOUNDED STACK — FAIL" || echo "PASS"

# Static analysis
cppcheck --enable=all --error-exitcode=1 \
  -I include/types -I src/m7_rtos/bsw/e2e \
  --std=c11 src/m7_rtos/
```

---

## Pull Request Checklist

- [ ] Compiles with `arm-none-eabi-gcc -Werror` (zero warnings)
- [ ] `.su` stack files reviewed — no unbounded `?` in safety-critical paths
- [ ] New DEM events added to `Dem.h` DTC catalogue with correct ID range
- [ ] WdgM checkpoints added for any new runnable (ENTRY + EXIT minimum)
- [ ] ASIL-D redundancy macros applied to all new safety-critical state variables
- [ ] MemMap section pragmas opened/closed in every new `.c` file
- [ ] HARA impact assessed — note in PR if any new hazard is introduced
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
- [ ] Doxygen header present in every new file
- [ ] Unit test added for every new algorithm

## What We Will Not Accept

- Dynamic memory allocation of any kind
- Changes that widen the safety envelope without a corresponding HARA update
- Register address changes without a reference to the NXP S32G Reference Manual
  section number in a comment
- Removal or weakening of ASIL-D redundancy checks

---

## Contact

For questions beyond the scope of a GitHub issue, contact norxs Technology LLC

---

*norxs Technology LLC — Safety Engineering, Built from the Ground Up.*

---

## Security Issues

**Never report security vulnerabilities through public GitHub issues or pull
requests.** Follow the coordinated disclosure process in [SECURITY.md](SECURITY.md)
(contact@norxs.com, subject prefix `[SECURITY]`).

## License Compliance for Contributions (ISO/IEC 5230)

- Contributions must be your original work; third-party code (including
  copy-pasted snippets) is not accepted into this repository — it has a
  zero-third-party-dependency policy verified by the SBOM gate in CI.
- Every new `.c`/`.h` file must carry the standard norxs Doxygen header
  (`@file`, `@brief`, `@author`, `@copyright`, `@standards`) — enforced by the
  `compliance-scan` CI job.
- After adding, removing, or modifying files, regenerate the SBOM and commit it:
  `python3 tools/generate_sbom.py 0.9.1` — the `supply-chain-compliance` CI job
  fails if any repository file is missing from the SBOM.
