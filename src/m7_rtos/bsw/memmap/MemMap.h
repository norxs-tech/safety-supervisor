/**
 * @file        MemMap.h
 * @brief       AUTOSAR R25-11 Memory Mapping Pragmas — Spatial Isolation for ASIL-D
 * @project     Autonomous Safety-Supervisor Gateway (SEooC)
 * @standards   ISO 26262-6 ASIL-D, AUTOSAR R25-11 SWS_MemMap
 * @author      norxs-lab
 * @copyright   (c) 2026 norxs Technology LLC. All rights reserved.
 *
 * AUTOSAR MemMap pattern: this header has NO include guard and is designed for
 * multiple inclusion. Each inclusion processes exactly the section macro that
 * the caller defined, applies the corresponding linker-section pragma, then
 * #undefs the macro so the next inclusion starts clean.
 *
 * Usage in source files:
 *   #define E2E_START_SEC_CODE
 *   #include "MemMap.h"
 *   // ... code in .e2e_code section ...
 *   #define E2E_STOP_SEC_CODE
 *   #include "MemMap.h"
 */

/* ── Code sections ───────────────────────────────────────────────────────── */

#ifdef SAFETY_ARBITRATOR_START_SEC_CODE
  #undef SAFETY_ARBITRATOR_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef SAFETY_ARBITRATOR_STOP_SEC_CODE
  #undef SAFETY_ARBITRATOR_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef VEHICLE_DYNAMICS_START_SEC_CODE
  #undef VEHICLE_DYNAMICS_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef VEHICLE_DYNAMICS_STOP_SEC_CODE
  #undef VEHICLE_DYNAMICS_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef SAFE_STATE_MGR_START_SEC_CODE
  #undef SAFE_STATE_MGR_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef SAFE_STATE_MGR_STOP_SEC_CODE
  #undef SAFE_STATE_MGR_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef E2E_START_SEC_CODE
  #undef E2E_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef E2E_STOP_SEC_CODE
  #undef E2E_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef IPC_START_SEC_CODE
  #undef IPC_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef IPC_STOP_SEC_CODE
  #undef IPC_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef CSM_START_SEC_CODE
  #undef CSM_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef CSM_STOP_SEC_CODE
  #undef CSM_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef DEM_START_SEC_CODE
  #undef DEM_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef DEM_STOP_SEC_CODE
  #undef DEM_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef WDGM_START_SEC_CODE
  #undef WDGM_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef WDGM_STOP_SEC_CODE
  #undef WDGM_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef RTE_START_SEC_CODE
  #undef RTE_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef RTE_STOP_SEC_CODE
  #undef RTE_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

#ifdef OTA_START_SEC_CODE
  #undef OTA_START_SEC_CODE
  /* GCC: section via linker script */
#endif
#ifdef OTA_STOP_SEC_CODE
  #undef OTA_STOP_SEC_CODE
  /* GCC: section via linker script */
#endif

/* ── VAR sections ────────────────────────────────────────────────────────── */

#ifdef SAFETY_ARBITRATOR_START_SEC_VAR_INIT_UNSPECIFIED
  #undef SAFETY_ARBITRATOR_START_SEC_VAR_INIT_UNSPECIFIED
  /* GCC: section via linker script */
#endif
#ifdef SAFETY_ARBITRATOR_STOP_SEC_VAR_INIT_UNSPECIFIED
  #undef SAFETY_ARBITRATOR_STOP_SEC_VAR_INIT_UNSPECIFIED
  /* GCC: section via linker script */
#endif

#ifdef VEHICLE_DYNAMICS_START_SEC_VAR_INIT_UNSPECIFIED
  #undef VEHICLE_DYNAMICS_START_SEC_VAR_INIT_UNSPECIFIED
  /* GCC: section via linker script */
#endif
#ifdef VEHICLE_DYNAMICS_STOP_SEC_VAR_INIT_UNSPECIFIED
  #undef VEHICLE_DYNAMICS_STOP_SEC_VAR_INIT_UNSPECIFIED
  /* GCC: section via linker script */
#endif

#ifdef SAFE_STATE_MGR_START_SEC_VAR_INIT_UNSPECIFIED
  #undef SAFE_STATE_MGR_START_SEC_VAR_INIT_UNSPECIFIED
  /* GCC: section via linker script */
#endif
#ifdef SAFE_STATE_MGR_STOP_SEC_VAR_INIT_UNSPECIFIED
  #undef SAFE_STATE_MGR_STOP_SEC_VAR_INIT_UNSPECIFIED
  /* GCC: section via linker script */
#endif

#ifdef IPC_START_SEC_VAR_SHARED
  #undef IPC_START_SEC_VAR_SHARED
  /* GCC: section via linker script */
#endif
#ifdef IPC_STOP_SEC_VAR_SHARED
  #undef IPC_STOP_SEC_VAR_SHARED
  /* GCC: section via linker script */
#endif

/* ── Calibration sections ────────────────────────────────────────────────── */

#ifdef SAFETY_ARBITRATOR_START_SEC_CALIB_32
  #undef SAFETY_ARBITRATOR_START_SEC_CALIB_32
  /* GCC: section via linker script */
#endif
#ifdef SAFETY_ARBITRATOR_STOP_SEC_CALIB_32
  #undef SAFETY_ARBITRATOR_STOP_SEC_CALIB_32
  /* GCC: section via linker script */
#endif

#ifdef VEHICLE_DYNAMICS_START_SEC_CALIB_32
  #undef VEHICLE_DYNAMICS_START_SEC_CALIB_32
  /* GCC: section via linker script */
#endif
#ifdef VEHICLE_DYNAMICS_STOP_SEC_CALIB_32
  #undef VEHICLE_DYNAMICS_STOP_SEC_CALIB_32
  /* GCC: section via linker script */
#endif

#ifdef SAFE_STATE_MGR_START_SEC_CALIB_32
  #undef SAFE_STATE_MGR_START_SEC_CALIB_32
  /* GCC: section via linker script */
#endif
#ifdef SAFE_STATE_MGR_STOP_SEC_CALIB_32
  #undef SAFE_STATE_MGR_STOP_SEC_CALIB_32
  /* GCC: section via linker script */
#endif
