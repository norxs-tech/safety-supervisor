# Security Policy — norxs Technology LLC

norxs Technology LLC operates an Open Source Security Assurance program conformant
with **OpenChain ISO/IEC 18974:2023** and aligned with the **NIST Cybersecurity
Framework (CSF) 2.0** and NIST SP 800-216 (Federal Vulnerability Disclosure
Guidelines). This policy applies to all norxs public repositories, including this
Autonomous Safety-Supervisor Gateway (SEooC) reference implementation.

---

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.9.x (current RC line) | ✅ Security fixes provided |
| < 0.9 | ❌ Not supported |

---

## Reporting a Vulnerability

**Please do NOT open a public GitHub issue for security vulnerabilities.**

| Channel | Address |
|---------|---------|
| Email (preferred) | **contact@norxs.com** — subject line prefix `[SECURITY]` |
| Web form | https://www.norxs.com/ → Contact → topic "Security Vulnerability Reporting" |
| GitHub | Private vulnerability reporting (Security tab → "Report a vulnerability"), if enabled on this repository |

Please include, where possible:

1. Affected file(s), function(s), and version/commit hash
2. Vulnerability class (e.g. CWE identifier) and an impact assessment
3. Reproduction steps, proof-of-concept, or failing test case
4. Suggested remediation, if known
5. Whether you wish to be credited in the advisory

## Coordinated Disclosure Process & Service Levels

| Phase | Target |
|-------|--------|
| Acknowledgement of report | within **3 business days** |
| Triage & severity assessment (CVSS v3.1) | within **10 business days** |
| Status updates to reporter | at least every **14 days** |
| Fix or documented mitigation for Critical/High | within **90 days** of triage |
| Public advisory | coordinated with reporter; default 90-day disclosure window |

norxs will not pursue legal action against researchers who act in good faith,
avoid privacy violations and service disruption, and follow this coordinated
disclosure process.

---

## Scope Notes for This Repository

This repository is a **Safety Element out of Context (SEooC) reference
implementation**. It is published for evaluation and demonstration; it is **not
a production firmware release**. Security findings are nonetheless triaged with
the same process, because the code demonstrates patterns intended for
safety-critical deployment. The following are of particular interest:

- E2E protection bypass (CRC/counter weaknesses) — `src/m7_rtos/bsw/e2e/`
- IPC ring buffer memory-safety or TOCTOU issues — `src/m7_rtos/cdd/ipc_ringbuffer/`
- Crypto service misuse (HSE interface) — `src/m7_rtos/bsw/csm/`
- OTA rollback logic flaws (UN R156) — `src/m7_rtos/bsw/OtaRollback.c`
- IDPS filter evasion — `src/qnx_a53/app_idps/`

Out of scope: vulnerabilities requiring physical access to a debug port on
hardware the attacker already controls; findings in third-party toolchains.

---

## Security Assurance Measures in This Repository (ISO/IEC 18974 §3.3)

- **Known-vulnerability awareness:** the repository has **zero third-party
  runtime dependencies** (see `sbom/`); toolchain components used in CI are
  pinned to Ubuntu LTS package streams and rebuilt on every run.
- **Static analysis:** cppcheck gate in CI (`misra-lint` job) with documented
  deviation records (`tools/cppcheck_suppressions.txt`).
- **Dynamic analysis:** all unit tests execute under AddressSanitizer and
  UndefinedBehaviorSanitizer (`unit-tests` job).
- **Architectural mitigations:** zero heap allocation, bounded stacks (CI-gated
  `.su` analysis), E2E-protected IPC, MPU-isolated SWC memory regions,
  hardware-watchdog-enforced safe state.
- **SBOM:** SPDX 2.3 document maintained at `sbom/` and validated in CI
  (`supply-chain-compliance` job).

## Security Contact

norxs Technology LLC · 30 N Gould St Ste N, Sheridan, WY 82801 US
contact@norxs.com · https://www.norxs.com/
