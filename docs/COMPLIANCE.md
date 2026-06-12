# Compliance Statement — norxs Autonomous Safety-Supervisor Gateway (SEooC)

Document: ASS-SEooC-COMP-001 · Version 0.9.1 · 2026-06-12
Owner: norxs Technology LLC — Open Source Program Office (contact@norxs.com)

This document states how this repository implements the norxs Technology LLC
compliance program for **OpenChain ISO/IEC 5230:2020** (open source license
compliance), **OpenChain ISO/IEC 18974:2023** (open source security assurance),
and the **NIST Cybersecurity Framework (CSF) 2.0**. norxs's company-level
conformance announcements are published in the norxs newsroom at
https://www.norxs.com/.

---

## 1. OpenChain ISO/IEC 5230:2020 — License Compliance

| §5230 Requirement | Implementation in this repository |
|---|---|
| 3.1 Program foundation — documented policy | This document + `LICENSE` (norxs RI License v1.0) + `NOTICE` |
| 3.1.2 Competence | Repository maintained by TÜV-certified engineers; license review is part of the PR checklist (`.github/PULL_REQUEST_TEMPLATE.md`) |
| 3.1.4 Program scope | All repositories under the `norxs-tech` GitHub organization |
| 3.1.5 License obligations | Inbound: **no third-party source is incorporated** (verified by SBOM + CI header scan). Outbound: norxs RI License v1.0 with attribution requirement (§4 of LICENSE) |
| 3.2 Relevant tasks — external inquiries | Open Source License Inquiries: contact@norxs.com or https://www.norxs.com/ contact form (topic: ISO 5230) |
| 3.3 Open source content review | CI `supply-chain-compliance` job: SPDX SBOM validation + per-file copyright/license header verification on every push and PR |
| 3.4 Compliance artifacts | Machine-readable **SPDX 2.3 SBOM** at `sbom/safety-supervisor-0.9.1.spdx.json` (per-file SHA-1/SHA-256 checksums); `NOTICE`; `LICENSE` |
| 3.5 Conformance | Program self-certified per OpenChain self-certification process; company announcement in norxs newsroom |

**Bill of Materials summary:** this codebase consists exclusively of original
norxs source files. There are **zero third-party runtime dependencies**. The only
external code reaching the delivered binary is compiler runtime support
(libgcc / newlib libm and syscall stubs) linked under their respective GCC/newlib
linking exceptions, documented in `NOTICE`.

---

## 2. OpenChain ISO/IEC 18974:2023 — Security Assurance

| §18974 Requirement | Implementation in this repository |
|---|---|
| 3.1.1 Documented policy | `SECURITY.md` — coordinated vulnerability disclosure policy with SLAs |
| 3.1.2 Competence & 3.1.3 Awareness | Maintainers hold TÜV Automotive Cybersecurity Professional certification (ISO/SAE 21434 / UN R155) |
| 3.1.4 Scope | All `norxs-tech` public repositories |
| 3.1.5 Standard practice methods | Static analysis (cppcheck, CI-gated), dynamic analysis (ASan/UBSan on all unit tests), stack bound verification, compiler hardening (`-Werror`, `-fstack-protector-strong`) |
| 3.2.1 Security assurance contact | contact@norxs.com, subject `[SECURITY]` — acknowledged within 3 business days (see `SECURITY.md`) |
| 3.3.1 Known-vulnerability detection | Zero third-party runtime components (SBOM-verified); CI toolchain tracks Ubuntu LTS security stream; SBOM enables downstream consumers to monitor |
| 3.3.2 Vulnerability handling | Triage, CVSS scoring, 90-day coordinated disclosure per `SECURITY.md` |
| 3.4 Conformance artifacts | `SECURITY.md`, SBOM, CI security gate logs (retained as workflow artifacts) |

---

## 3. NIST Cybersecurity Framework 2.0 Mapping

| CSF 2.0 Function | Category (examples) | Repository implementation |
|---|---|---|
| **GOVERN (GV)** | GV.PO policy, GV.RR roles | This compliance statement; `SECURITY.md` ownership; PR template review gates; CODEOWNERS-equivalent maintainer review on `main` |
| **IDENTIFY (ID)** | ID.AM asset management | SPDX SBOM enumerates every file with checksums; `docs/HARA_ASS_SEooC_001.md` identifies the item, boundary, and assets; TARA-ready interface inventory (IPC, OTA, diagnostic) |
| **PROTECT (PR)** | PR.DS data security, PR.PS platform security | E2E Profile 5/22 integrity on all IPC frames; CMAC-AES-128 frame authentication via HSE; RSA-4096 signed OTA with dual-bank rollback (UN R156); MPU-isolated memory regions; zero dynamic allocation; stack canaries |
| **DETECT (DE)** | DE.CM continuous monitoring | A53 IDPS daemon (Token Bucket rate limiting, SOME/IP whitelist, MAC verification — UN R155 §7.3.3); WdgM alive/deadline/logical supervision; DEM 16-event DTC catalogue with freeze frames |
| **RESPOND (RS)** | RS.MA incident management | Safe state entry within FTTI 20ms on detected violation; `SECURITY.md` coordinated disclosure workflow for externally reported issues |
| **RECOVER (RC)** | RC.RP recovery planning | Dual-bank OTA rollback to last-known-good firmware on health-gate failure; hardware-watchdog-driven reset to defined safe state |

---

## 4. Verification

The CI pipeline (`.github/workflows/ci.yml`) enforces this statement on every
push and pull request:

1. `unit-tests` — 8 host-native test cases under ASan/UBSan, JUnit report artifact
2. `build-m7` — strict `-Werror` ASIL-D cross-compile, ELF/HEX/MAP artifacts
3. `stack-analysis` — fails on any unbounded stack frame (ISO 26262 §7.4.10)
4. `misra-lint` — cppcheck with documented deviation records only
5. `compliance-scan` — forbidden-pattern scan (heap, recursion) + Doxygen headers
6. `supply-chain-compliance` — SPDX SBOM schema/coverage validation, per-file
   copyright header verification, `SECURITY.md` / `NOTICE` / `LICENSE` presence
   (ISO/IEC 5230 §3.3–3.4, ISO/IEC 18974 §3.4)

---

*(c) 2026 norxs Technology LLC. All rights reserved.*
