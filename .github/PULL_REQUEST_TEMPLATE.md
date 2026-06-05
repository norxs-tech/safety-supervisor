## Summary

## Motivation
Closes #

## Module(s) Changed
- [ ] SwcSafetyArbitrator
- [ ] SwcVehicleDynamics
- [ ] SwcSafeStateMgr
- [ ] IPC_RingBuffer / E2E / WdgM / DEM / CSM
- [ ] app_idps (QNX A53)
- [ ] Build / CMake / Docs

## MISRA C:2023 / AUTOSAR Compliance Checklist

- [ ] Zero dynamic memory (`malloc`/`calloc`/VLA)
- [ ] Zero recursion — call graph is statically bounded
- [ ] All new functions have prototypes in `.h` with Doxygen `@brief` / `@param` / `@return`
- [ ] Doxygen corporate header in every new file
- [ ] MemMap `_START_SEC_` / `_STOP_SEC_` pragmas in every new `.c` file
- [ ] `ASIL_D_REDUNDANT_VAR` / `ASIL_D_CHECK` applied to all new safety-critical variables

## Safety Checklist

- [ ] WdgM checkpoints added for any new runnable (ENTRY + EXIT minimum)
- [ ] New DEM events added to `Dem.h` catalogue with correct DTC ID range
- [ ] HARA impact assessed — no new unmitigated hazard introduced
- [ ] `CHANGELOG.md` updated under `[Unreleased]`

## Testing

- [ ] Host-native unit test added for every new algorithm (TEST target)
- [ ] Stack `.su` files reviewed — no unbounded `?` in safety-critical paths
- [ ] `cmake --build build/m7` passes with zero warnings (`-Werror`)
- [ ] `ctest --test-dir build/test` passes with ASan + UBSan

## Stack Usage Summary
```
# Paste top 10 from: find build/m7 -name "*.su" | xargs cat | sort -t$'\t' -k2 -rn | head -10
```

## Test Results
```
# Paste ctest output
```
