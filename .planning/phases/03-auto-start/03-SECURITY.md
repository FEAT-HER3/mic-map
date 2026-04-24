---
phase: 03
slug: auto-start
status: verified
threats_open: 0
asvs_level: 1
created: 2026-04-23
---

# Phase 03 — Security

> Per-phase security contract: threat register, accepted risks, and audit trail.
> Phase 3 (Auto-Start) covers SteamVR-native auto-launch via `app.vrmanifest`, silent-launch CLI mode, retry-on-VR-not-ready, first-launch tray balloon, and config-flag read/write for balloon dedup.

---

## Trust Boundaries

| Boundary | Description | Data Crossing |
|----------|-------------|---------------|
| SteamVR vrserver ↔ MicMap (OpenVR IPC) | vrserver is trust root; MicMap is consumer of `IVRApplications` / `IVRSystem` events | VREvent enums (uint32_t), VRInitError codes, app-install state booleans |
| Filesystem: `%APPDATA%\MicMap\config.json` | Per-user config store, read/written by MicMap | `shownTrayNotification` bool (UX dedup flag) |
| Filesystem: `<install-dir>\app.vrmanifest` | Admin-ACL'd in Program Files (Phase 4 installer context); read-only at runtime | Static manifest JSON (app_key, binary_path, strings, `arguments: "--steamvr"`) |
| CLI arguments (`LPWSTR lpCmdLine`) | Win32 process entry; parsed via `CommandLineToArgvW` | 3 presence-only flags: `--steamvr`, `--register-manifest`, `--unregister-manifest` |
| Admin-elevated registrar context | Phase 4 installer invokes `micmap.exe --register-manifest` with admin rights | OpenVR API calls only — no FS writes, no shell-out, no registry writes |
| Windows shell tray (NOTIFYICONDATA) | Tray icon + balloon surface | Compile-time wide-literal strings; no user input interpolated |

---

## Threat Register

| Threat ID | Category | Component | Disposition | Mitigation | Status |
|-----------|----------|-----------|-------------|------------|--------|
| T-03-01-01 | Tampering | test_vrmanifest_schema loads `app.vrmanifest` from build dir | accept | Build-dir path injected via CMake `CMAKE_RUNTIME_OUTPUT_DIRECTORY`; not user-supplied; no writes in test | closed |
| T-03-01-02 | N/A | Input validation (V5) in schema test | N/A | Tests do not parse untrusted input; `nlohmann::json::parse(..., allow_exceptions=false)` defensive | closed |
| T-03-01-03 | N/A | Auth (V2) / session (V3) / access (V4) / crypto (V6) in schema test | N/A | Test-only code; no network, no auth surface | closed |
| T-03-02-01 | Tampering | `app.vrmanifest` writable in Program Files | transfer (Phase 4) | Phase 4 installer elevates; file inherits admin-only ACLs. Phase 3 scope is build-dir; ACLs inherit from build dir | closed |
| T-03-02-02 | Tampering | `@MICMAP_VERSION@` substitution from PROJECT_VERSION | accept | Build-time only; no user input. Explicit `set(MICMAP_VERSION ${PROJECT_VERSION})` before `configure_file` | closed |
| T-03-02-03 | N/A | Input validation (V5) — `arguments` field form in manifest | mitigate | A2 empirical test locked the form; `test_vrmanifest_schema` asserts it; no untrusted input | closed |
| T-03-02-04 | N/A | Auth / session / access / crypto / SSRF / injection in manifest emission | N/A | Static JSON emission; no network, no code-execution path from manifest | closed |
| T-03-02-05 | DoS | `/SUBSYSTEM:WINDOWS` bypass → console flash on auto-launch | mitigate | D-07: `add_executable(micmap WIN32 ...)` locked; phase-exit grep gate prevents `SUBSYSTEM:CONSOLE` regression | closed |
| T-03-03-01 | Tampering | User hand-edits `config.json` with garbage `shownTrayNotification` | mitigate | `Config::readBool` returns default on missing-key or wrong-type (test case 4 covers); Phase 2 corruption-backup protects full-file parse failures | closed |
| T-03-03-02 | Tampering | User sets `shownTrayNotification: true` to suppress balloon | accept | Legitimate user preference (no balloon spam) — by design, not a threat | closed |
| T-03-03-03 | N/A | Auth (V2) / session (V3) / access (V4) / crypto (V6) | N/A | Field is a single bool; no privilege implications | closed |
| T-03-03-04 | DoS | Malformed JSON in `config.json` | transfer (Phase 2) | Phase 2 already handles corruption → backup + defaults; no new exposure from this field | closed |
| T-03-04-01 | Tampering | Buffer overrun on `GetModuleFileNameW` | mitigate | 32768-WCHAR buffer per Pitfall 9; explicit `n == _countof(buf)` truncation check; `PathCchRemoveFileSpec` preferred over shlwapi | closed |
| T-03-04-02 | Tampering | Side-loaded `openvr_api.dll` | accept / transfer | Phase 4 installer owns install-dir ACLs; `openvr_api.dll` ships alongside `micmap.exe` (DLL search order favors sibling). Out of Phase 3 scope | closed |
| T-03-04-03 | Spoofing | Malicious app registers with `app_key="bigscreen.micmap"` before us | accept | `IsApplicationInstalled` returns true → `ensureRegistered` no-ops. User can remove in SteamVR UI. Installer is the trust anchor | closed |
| T-03-04-04 | EoP | Registrar called with admin context from installer | mitigate | Registrar makes only OpenVR API calls; zero FS writes, zero registry writes, zero shell-outs. Admin context does not widen surface | closed |
| T-03-04-05 | Information Disclosure | `manifestAbsPath` in logs | accept | Path logged is the binary's own install dir (no secret); `%APPDATA%\MicMap\micmap.log` is per-user | closed |
| T-03-04-06 | N/A | Auth / session / access / crypto / SSRF / SQLi / XSS | N/A | No user-controlled input; no network; no web surface; no DB | closed |
| T-03-05-01 | DoS | SteamVR force-kill if Quit ack delayed (Pitfall 2) | mitigate | D-11: ack BEFORE app callback in `processVREventImpl`; `test_vr_input_quit_ordering` locks the ordering as call-log assertion — any regression reversing the two lines fails the test | closed |
| T-03-05-02 | Tampering | Malicious VREvent injection into vrserver queue | accept | vrserver is trusted producer; MicMap is consumer. Out of scope (vrserver = trust root) | closed |
| T-03-05-03 | N/A | Auth / session / access / crypto / input-validation in VR event loop | N/A | `eventType` is a `uint32_t` enum check; default branch no-op; no untrusted strings | closed |
| T-03-06-01 | Tampering | argv use-after-free (Pitfall 8) | mitigate | `CliFlags` populated into plain-struct copy BEFORE `LocalFree(argvW)`; argv pointer nulled after free; no argv pointers stored | closed |
| T-03-06-02 | EoP | Admin-elevated CLI mode executes arbitrary code via CLI injection | mitigate | Only 3 exact-match `wcscmp` flags recognized; no argument values consumed; no shell-out; no FS writes outside OpenVR API | closed |
| T-03-06-03 | Spoofing | Named-mutex squatting by third party | accept | Mutex is UX helper, not trust boundary. Worst case: MicMap sees another instance and exits; user relaunches | closed |
| T-03-06-04 | Information Disclosure | Log leaks argv content | accept | Logger writes only hard-coded strings + VRInitError enum names; flag values never logged (flags are presence-only) | closed |
| T-03-06-05 | Spoofing | Balloon text spoofs | mitigate | Title + body are compile-time wide-literal strings (`L"MicMap"`, `L"Running in the system tray. Click the icon to open."`); no interpolation, no user input | closed |
| T-03-06-06 | DoS | Focus Assist suppression leaves user unaware of silent launch | accept | Tray icon is primary discoverability surface (NIF_ICON always on); balloon is a nicety per D-09. `NIIF_RESPECT_QUIET_TIME` graceful degrade | closed |
| T-03-06-07 | N/A | Auth / session / access / crypto / SSRF / SQLi / XSS / ReDoS | N/A | No network, no DB, no web, no regex on untrusted input | closed |
| T-03-07-01 | DoS | Retry thread never exits → handle leak | mitigate | `manifestRetryCancel` atomic set in shutdown; `join()` blocks until thread exits; 1s cancel-ticks ensure responsiveness | closed |
| T-03-07-02 | DoS | Retry thread races `vrInput->shutdown()` → crash in VR_Shutdown | mitigate | `shutdown()` joins retry thread BEFORE `vrInput->shutdown`; ordering locked by code comment + acceptance_criteria grep | closed |
| T-03-07-03 | DoS | SteamVR force-kill if ack happens but teardown exceeds 2s | mitigate | Plan 05 ack-first stops watchdog clock at ack time; teardown runs without time pressure | closed |
| T-03-07-04 | Tampering | User hand-edits `config.json` between balloon-fire and save | accept | Save uses Phase 2 atomic `ReplaceFile` semantics; last-write-wins acceptable for UX flag | closed |
| T-03-07-05 | Information Disclosure | Log leaks VRInitError name during retry | accept | VRInitError names are Valve-public; no user data | closed |
| T-03-07-06 | EoP | Retry thread runs with admin rights when installer invoked with admin | N/A | Retry thread only exists in GUI mode (`MicMapApp::initialize`) under user rights. Admin-elevated CLI mode exits after single registrar call — no thread spun up | closed |
| T-03-07-07 | N/A | Auth / session / access / crypto / SSRF / SQLi / XSS | N/A | No network, no DB, no web | closed |

*Status: open · closed*
*Disposition: mitigate (implementation required) · accept (documented risk) · transfer (third-party / other phase) · N/A (category does not apply)*

---

## Accepted Risks Log

| Risk ID | Threat Ref | Rationale | Accepted By | Date |
|---------|------------|-----------|-------------|------|
| AR-03-01 | T-03-01-01 | Build-dir path is CMake-controlled, not user-supplied; test reads only, no writes. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-02 | T-03-02-02 | `@MICMAP_VERSION@` substituted at build time from PROJECT_VERSION; no runtime input path. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-03 | T-03-03-02 | User self-suppression of balloon via config edit is the intended UX contract, not an attack. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-04 | T-03-04-02 | DLL side-loading mitigated by Phase 4 installer ACLs on install dir; out of Phase 3 scope. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-05 | T-03-04-03 | App-key squatting recoverable via SteamVR UI; installer is trust anchor. Low-risk deferral. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-06 | T-03-04-05 | Logged path is the binary's own install dir; log file is per-user under `%APPDATA%`. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-07 | T-03-05-02 | vrserver is the trusted IPC producer; MicMap is a consumer. Threat belongs to vrserver's trust model. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-08 | T-03-06-03 | Named-mutex is a UX singleton hint, not a security boundary; user relaunches on collision. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-09 | T-03-06-04 | Flags are presence-only booleans; logger writes only hard-coded strings + VRInitError names. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-10 | T-03-06-06 | Tray icon is the primary discoverability surface; balloon is a nicety. Focus Assist respected by design. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-11 | T-03-07-04 | Atomic `ReplaceFile` last-write-wins is acceptable semantics for a UX dedup flag. | Phase 3 planner + executor | 2026-04-23 |
| AR-03-12 | T-03-07-05 | VRInitError enum names are Valve-public documentation; no user data leaked. | Phase 3 planner + executor | 2026-04-23 |

*Accepted risks do not resurface in future audit runs.*
*Transfer dispositions (T-03-02-01 → Phase 4, T-03-03-04 → Phase 2) are tracked by the receiving phase.*

---

## Security Audit Trail

| Audit Date | Threats Total | Closed | Open | Run By |
|------------|---------------|--------|------|--------|
| 2026-04-23 | 35 | 35 | 0 | /gsd-secure-phase 3 — user option 2 (accept all; mitigations verified via phase summaries) |

---

## Sign-Off

- [x] All threats have a disposition (mitigate / accept / transfer / N/A)
- [x] Accepted risks documented in Accepted Risks Log
- [x] `threats_open: 0` confirmed
- [x] `status: verified` set in frontmatter

**Approval:** verified 2026-04-23
