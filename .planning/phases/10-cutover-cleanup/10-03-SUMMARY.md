---
phase: 10-cutover-cleanup
plan: 03
subsystem: fail-pills-and-driver-version
tags: [phase-10, cutover-cleanup, wave-3, fail-pills, named-mutex, driver-version, parallel-with-02-04]
requires:
  - apps/micmap/src/tray_glyph.hpp (10-02 — canonical HealthSnapshot/StateSnapshot declaration site; this plan extends HealthSnapshot with bool econnrefused)
  - apps/micmap/main.cpp:1614 CreateMutexW (FAIL-04 hardening site — was L"MicMapSingleInstance")
  - apps/micmap/main.cpp:548 MicMapApp::pollDriverHealth (P8 1Hz/2Hz poll loop — pill picker hook site)
  - apps/micmap/main.cpp:840 MicMapApp::renderUI (P8 D-11 driver-health pane — pill render site)
  - apps/micmap/main.cpp:79 driverClient member (existing IDriverApi; clearError() from P8 D-16 used by Dismiss button)
  - driver/src/http_server.hpp HttpServer ctor (extended with driverVersionGetter as last positional param)
  - driver/src/device_provider.cpp DeviceProvider::Init (constructs HttpServer; passes the new lambda)
  - driver/CMakeLists.txt MICMAP_VERSION_STRING define (10-01 SSoT)
  - tests/test_fail_pill_priority.cpp (Wave 0 RED scaffold — flips to GREEN after this plan)
provides:
  - apps/micmap/src/process_check.hpp (isProcessRunning(const wchar_t*) declaration)
  - apps/micmap/src/process_check.cpp (CreateToolhelp32Snapshot + 2s mutex-guarded cache; Pitfall 6 mitigation)
  - apps/micmap/src/fail_pill.hpp (FailKind enum + FailPill struct + pickActivePill pure function)
  - apps/micmap/src/fail_pill.cpp (D-08 priority impl: FAIL-02 > FAIL-03 > FAIL-01 > FAIL-05; deep-link URIs; D-10 dismissable bits; D-20 blocking=false)
  - HealthSnapshot.econnrefused field (added to apps/micmap/src/tray_glyph.hpp for test scaffold positional brace-init compatibility)
  - GET /health.driver_version field (driver/src/http_server.cpp; D-19; mirror of P7 D-09 / P9 D-07 getter-callback pattern)
  - driverVersionGetter callback param + member on HttpServer (driver/src/http_server.hpp; appended last positional)
  - apps/micmap/main.cpp FAIL pill render in P8 D-11 driver-health pane (headline + ShellExecuteW deep-link button + Dismiss button)
  - apps/micmap/main.cpp FAIL-04 mutex hardening (Local\MicMap_Client_SingleInstance_v1 + ShowWindow(SW_RESTORE) + minimized-skip carve-out preserved)
  - g_failUx static FailUxState global next to g_app + g_tray
  - Phase 10 Wave 0 test_fail_pill_priority ctest GREEN (was build-RED)
affects:
  - apps/micmap/src/tray_glyph.hpp (1 field added to HealthSnapshot — does not break deriveTrayGlyph)
  - apps/micmap/main.cpp (5 insertion sites: include + g_failUx global, FAIL-04 mutex line + restore-window block, pollDriverHealth pill picker hook, renderUI pill render block)
  - apps/micmap/CMakeLists.txt (2 new source files appended to MICMAP_SOURCES)
  - driver/src/http_server.hpp (ctor signature + private member appended)
  - driver/src/http_server.cpp (ctor init list + /health JSON build block extended by 1 field)
  - driver/src/device_provider.cpp (Init: 1 new lambda + 1 new ctor positional arg)
tech-stack:
  added: []
  patterns:
    - "Pure-derivation seam pattern (pickActivePill): inputs are POD snapshot structs + a bool; output is std::optional<FailPill>; no globals, no Win32, fully testable in headless ctest. The Win32 wrapper (renderUI ShellExecuteW + Dismiss) consumes the optional and the existing g_app.driverClient; the seam keeps the test fixture flat (9 cases including 4 dismissable-flag sub-cases) per the 10-02 deriveTrayGlyph + P9 IDriverApi pattern."
    - "FAIL-02 vs FAIL-03 disambiguation via isProcessRunning(L\"vrserver.exe\") + 2-second cache (Pitfall 6): CreateToolhelp32Snapshot enumerates running processes in microseconds (vs 50-200ms tasklist.exe spawn cost that would cause UI freeze at 1Hz polling). The 2s cache amortizes the cost — when the driver is unreachable for an extended period, only one snapshot is taken every 2 seconds. The cache is a small fixed-size mutex-guarded array (4 slots, expected size 1-2 entries — just \"vrserver.exe\")."
    - "Local\\<vendor>_<scope>_<purpose>_v<N> mutex naming convention (FAIL-04 D-09): Local\\ session-scoping prefix is explicit (matches default behavior but documented), version-suffixed _v1 lets future versions shed a stuck handle from a crashed v0 process by bumping to _v2. Combined with the existing P3 D-08 minimized-skip carve-out, SteamVR --minimized auto-relaunch never steals focus mid-VR-session, while a user-clicked second launch correctly surfaces the tray-minimized first instance via ShowWindow(SW_RESTORE) + SetForegroundWindow."
    - "Append-only HttpServer ctor evolution (D-19): mirrors P7 D-09 driverDetectionActiveGetter + P9 D-07 driverTrainingActiveGetter + 09-03 driverAudioEnabledGetter. The new driverVersionGetter is appended as the LAST positional param so existing test scaffolds + legacy callers compile unchanged; defensive null-check inside the /health handler returns std::string(\"\") when no getter is wired (test code), matching the existing pattern for the other three boolean getters."
    - "ASCII-safe UTF-8 -> UTF-16 conversion at the ShellExecuteW boundary (deep-link URIs): the URI string literals in fail_pill.cpp (\"ms-settings:privacy-microphone\" + \"steam://rungameid/250820\") are pure ASCII, so std::wstring(s.begin(), s.end()) widens losslessly. Avoids dragging WideCharToMultiByte into the render path; if a future deep-link URI introduced non-ASCII characters the conversion would need to switch to MultiByteToWideChar(CP_UTF8, ...). Documented inline at the call site."
key-files:
  created:
    - apps/micmap/src/process_check.hpp
    - apps/micmap/src/process_check.cpp
    - apps/micmap/src/fail_pill.hpp
    - apps/micmap/src/fail_pill.cpp
    - .planning/phases/10-cutover-cleanup/10-03-SUMMARY.md
  modified:
    - apps/micmap/src/tray_glyph.hpp
    - apps/micmap/main.cpp
    - apps/micmap/CMakeLists.txt
    - driver/src/http_server.hpp
    - driver/src/http_server.cpp
    - driver/src/device_provider.cpp
decisions:
  - "FailPill struct names the deep-link field `deepLink` (matches the Wave 0 RED scaffold tests/test_fail_pill_priority.cpp:58 `pill->deepLink.find(...)`), not `actionUri` as drafted in the plan body. The plan's <action> block named the field `actionUri`, but the test scaffold contract is load-bearing and was written first. Following the plan's name would have caused build-RED in the test executable. The semantic is identical (URI string passed to ShellExecuteW); only the field name differs. The plan's `<read_first>` block correctly cites the test scaffold as a contract; this is treating it as such."
  - "HealthSnapshot extended with `bool econnrefused{false}` as field 2 (between driverLoaded and driverVersion). The Wave 0 test scaffold brace-inits HealthSnapshot{ /*ok=*/false, /*econnrefused=*/true } — passing a bool as the 2nd positional arg. The pre-existing tray_glyph.hpp HealthSnapshot had std::string driverVersion at position 2, which would refuse the bool->string conversion (narrowing). Added econnrefused at position 2 so the brace-init compiles. deriveTrayGlyph (10-02) does not consume the new field — adding it does not change its behavior. This is the cleanest landing because tray_glyph.hpp is the canonical HealthSnapshot site per 10-02 SUMMARY's contract that 10-03 fail_pill + 10-06 version_mismatch reuse via #include rather than duplicate."
  - "FAIL pill rendered inside MicMapApp::renderUI (apps/micmap/main.cpp:840+) at the TOP of the existing P8 D-11 driver-health pane, just after the `Driver Health` ImGui::Separator. Render reads g_failUx.activePill (populated by pollDriverHealth one tick prior — the 1Hz cadence is fine for FAIL pill latency since detect-to-render is <1s). The pill block ends with ImGui::Separator() before the existing HEALTH-01 driver-loaded indicator, so the pre-existing pane layout is preserved with the FAIL pill prepended. driverClient member access works because renderUI is a MicMapApp member function — no new accessor function needed."
  - "vrserverRunning bool computed conditionally: only when health.driverLoaded == false (the case where the FAIL-02 vs FAIL-03 disambiguation matters). When driverLoaded is true, we pass `true` for clarity (the endpoint reached us so vrserver.exe must be running, by definition), but pickActivePill does not consume the bool in that branch. This avoids paying the CreateToolhelp32Snapshot cost on every tick when the driver IS loaded (the 2-second cache would amortize anyway, but skipping it entirely is cheaper)."
  - "FAIL-04 hardening uses the smallest-diff form: kept the existing minimized-skip carve-out + the existing PostMessageW(WM_COMMAND, IDM_SHOW) hand-off + WR-02 CloseHandle on ERROR_ALREADY_EXISTS path. The only changes are (a) the mutex name string literal, (b) inserting ShowWindow(w, SW_RESTORE) between PostMessageW and SetForegroundWindow inside the existing `if (w)` block, and (c) adding inline documentation explaining the Local\\ prefix + _v1 suffix rationale. RESEARCH §Pattern 2's optional AttachThreadInput dance is NOT added per Pitfall A5 — basic ShowWindow + SetForegroundWindow is sufficient for D-25(6) UAT; if UAT shows the simple form misses cases the dance can be added in a follow-up plan."
  - "/health.driver_version sourced from a captureless lambda (`[](){ return std::string(MICMAP_VERSION_STRING); }`) rather than capturing `this` and reading a member. MICMAP_VERSION_STRING is a compile-time string literal (define from cmake/version.cmake via target_compile_definitions); there is no DeviceProvider state to read. Captureless lambda is the simplest possible getter; matches the std::string-return shape that http_server.hpp added as a new ctor param."
  - "Existing P3 D-08 minimized-skip carve-out (apps/micmap/main.cpp:1626 `if (!flags.minimized)`) preserved verbatim. The plan body discussed this preservation; the actual hardening only added ShowWindow(SW_RESTORE) inside the existing block, no behavior change to the SteamVR --minimized auto-relaunch path. Verified by reading the existing block before edit and confirming the carve-out is still in place after edit."
metrics:
  duration: ~25 minutes
  completed_date: 2026-05-10
  task_count: 4
  file_count: 9
---

# Phase 10 Plan 03: Wave 3 FAIL UX + FAIL-04 Mutex Hardening + driver_version Summary

Land FAIL UX cluster + FAIL-04 named-mutex hardening + GET /health.driver_version field. Five concerns:

1. `apps/micmap/src/process_check.{hpp,cpp}` — `isProcessRunning(L"vrserver.exe")` via CreateToolhelp32Snapshot with a 2-second mutex-guarded cache (Pitfall 6: NEVER tasklist).
2. `apps/micmap/src/fail_pill.{hpp,cpp}` — `pickActivePill()` pure function implementing D-08 priority stacking (FAIL-02 > FAIL-03 > FAIL-01 > FAIL-05) with deep-link URIs + D-10 dismissable flags + D-20 `blocking=false`.
3. FAIL-04 mutex hardening at `apps/micmap/main.cpp:1614`: `L"MicMapSingleInstance"` → `L"Local\\MicMap_Client_SingleInstance_v1"` + `ShowWindow(SW_RESTORE)` before `SetForegroundWindow` (preserves the existing P3 D-08 minimized-skip carve-out).
4. `GET /health.driver_version` field via getter-callback pattern (mirror of P7 D-09 / P9 D-07): `HttpServer` ctor extended with `std::function<std::string()> driverVersionGetter`; `DeviceProvider::Init` passes `[](){ return std::string(MICMAP_VERSION_STRING); }`.
5. FAIL pill render at the TOP of the existing P8 D-11 driver-health pane in `MicMapApp::renderUI` — pill-red headline, ShellExecuteW deep-link button, Dismiss button (FAIL-01 + FAIL-05 only per D-10).

The Wave 0 `test_fail_pill_priority` ctest flips from build-RED to GREEN.

## What Shipped

**`apps/micmap/src/process_check.{hpp,cpp}`:**

- `isProcessRunning(const wchar_t* exeName)` — `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)` + `Process32FirstW` + `Process32NextW` + `_wcsicmp` for case-insensitive wide-string compare.
- 2-second result cache: `std::chrono::seconds(2)` TTL, fixed-size 4-slot array, `std::mutex`-guarded read + write. Cache hits skip the snapshot entirely; expected steady-state size is 1-2 entries (just `"vrserver.exe"`).
- Pitfall 6 mitigation: NO `tasklist.exe`, NO `system()` call, NO new thread. The snapshot is sub-millisecond on Windows; the 2s cache amortizes to one snapshot every 2s while the driver is unreachable, even at 1Hz polling.
- `_WIN32`-gated; non-Windows builds compile to an empty TU (matches the existing tray_glyph.cpp pattern).

**`apps/micmap/src/fail_pill.{hpp,cpp}`:**

- `enum class FailKind { None, MicPermission, DriverNotLoaded, SteamVRNotRunning, DoubleInstance, DeviceRemoved, VersionMismatch }` — all 5 v1.6 fail kinds plus `None` (sentinel for "no pill" / "generic last_error fallthrough") plus `VersionMismatch` (10-06 will reuse this enum).
- `struct FailPill { FailKind kind; std::string headlineText; std::string actionLabel; std::string deepLink; bool dismissable; bool blocking; }` — `blocking` is forever `false` in v1.6 per D-20.
- `pickActivePill(health, state, vrserverRunning)` — pure D-08 priority order:
  - Priority 1 — `!health.driverLoaded`: disambiguate via `vrserverRunning`. False → `SteamVRNotRunning` ("SteamVR not running", no action button, not dismissable). True → `DriverNotLoaded` ("Driver not installed — run installer or enable in SteamVR", "Open SteamVR" → `steam://rungameid/250820`, not dismissable).
  - Priority 2 — `state.audioDeviceState == "permission_denied"`: `MicPermission` ("Mic access blocked", "Open Windows mic settings" → `ms-settings:privacy-microphone`, dismissable per D-10).
  - Priority 3 — `state.audioDeviceState == "missing"`: `DeviceRemoved` ("Microphone disconnected — reconnect to resume detection", no action button (driver auto-recovers via IMMNotificationClient rebind from P6/P7), dismissable per D-10).
  - Priority 4 — `state.lastError.has_value() && !state.lastError->empty()`: generic dismissable pill (`kind=None` signals "render text but no canonical action").
- Pure POD-in / optional-out — no globals, no Win32, testable on any host.

**HealthSnapshot extension (`apps/micmap/src/tray_glyph.hpp`):**

- Added `bool econnrefused{false}` as field 2 (between `driverLoaded` and `driverVersion`) so the Wave 0 test scaffold's brace-init `HealthSnapshot{ /*ok=*/false, /*econnrefused=*/true }` compiles. `deriveTrayGlyph` does not consume the new field; behavior unchanged.

**Driver-side `/health.driver_version` (D-19):**

- `driver/src/http_server.hpp`: `HttpServer` ctor extended with `std::function<std::string()> driverVersionGetter = nullptr` as the LAST positional param (append-only — does not reorder existing params; mirror of P7 D-09 / P9 D-07 / 09-03 evolution discipline). New private member `driverVersionGetter_`.
- `driver/src/http_server.cpp`: ctor member-init list extended with `, driverVersionGetter_(std::move(driverVersionGetter))`. `/health` JSON build block extended with `body["driver_version"] = driverVersionGetter_ ? driverVersionGetter_() : std::string("")` AFTER `body["driver_audio_enabled"]` (stable field order: `driver_loaded` → `driver_detection_active` → `driver_training_active` → `driver_audio_enabled` → `driver_version`).
- `driver/src/device_provider.cpp`: `DeviceProvider::Init` constructs a captureless lambda `[](){ return std::string(MICMAP_VERSION_STRING); }` and passes it as the new last positional arg to `std::make_unique<HttpServer>(...)`. `MICMAP_VERSION_STRING` flows from `cmake/version.cmake` via `target_compile_definitions(driver_micmap PRIVATE MICMAP_VERSION_STRING="${MICMAP_VERSION}")` (10-01 SSoT).

**`apps/micmap/main.cpp` wiring (5 insertion sites):**

- Top of file (`#include` block, line ~42): `#include "src/fail_pill.hpp"` + `#include "src/process_check.hpp"` next to the existing `#include "src/tray_glyph.hpp"`.
- Globals block (line ~206 next to `g_app` + `g_tray`): `static struct FailUxState { std::optional<micmap::client::FailPill> activePill; } g_failUx;`.
- `WinMain` FAIL-04 mutex (line **1614**): `CreateMutexW(nullptr, TRUE, L"Local\\MicMap_Client_SingleInstance_v1")` (was `L"MicMapSingleInstance"`); inside the `if (!flags.minimized)` branch (line **1626**) inserted `ShowWindow(w, SW_RESTORE)` between `PostMessageW(...)` and `SetForegroundWindow(w)`. Existing `FindWindowW(L"MicMapMain", ...)` window-class match preserved (verified against line **1639**'s `RegisterClassExW(... L"MicMapMain", ...)`).
- `MicMapApp::pollDriverHealth` tail (line ~734, just after the `applyTrayGlyph` call): `const bool vrserverRunning = !healthSnap.driverLoaded ? micmap::client::isProcessRunning(L"vrserver.exe") : true;` then `g_failUx.activePill = micmap::client::pickActivePill(healthSnap, stateSnap, vrserverRunning);`. Reuses the same `healthSnap` + `stateSnap` poll-tick stack frames that 10-02's tray glyph derivation uses (D-06 — no new poll, no new thread).
- `MicMapApp::renderUI` (line **840+**, top of the P8 D-11 driver-health pane just after `ImGui::Separator()`): FAIL pill render block: pill-red headline via `ImGui::PushStyleColor + TextWrapped + PopStyleColor`, ShellExecuteW deep-link button (`std::wstring wuri(deepLink.begin(), deepLink.end()); ShellExecuteW(nullptr, L"open", wuri.c_str(), nullptr, nullptr, SW_SHOWNORMAL)` — ASCII-safe widening), Dismiss button (FAIL-01 + FAIL-05 only via `if (pill.dismissable)`) calling `driverClient->clearError()` + optimistic local clear of `lastError` + `g_failUx.activePill.reset()`. Pane block ends with `ImGui::Separator()` so the pre-existing HEALTH-01 indicator below renders unchanged.

**`apps/micmap/CMakeLists.txt`:**

- `MICMAP_SOURCES` extended with `src/fail_pill.cpp` + `src/process_check.cpp`. No new lib link needed (`<TlHelp32.h>` is part of `<Windows.h>`; kernel32 functions are linked transitively; Shlwapi was already added by 10-02; ShellExecuteW lives in shell32 which is already linked).

## Verification

**Per-task automated checks (all PASS):**

- **Task 1** (process_check): `test -f` on both files; `grep CreateToolhelp32Snapshot` + `grep kCacheTtl=2 seconds`; `! grep system(`. Pitfall 6 documented in the negative comment but no actual usage of `tasklist.exe`.
- **Task 2** (fail_pill): `test -f` on both files; FailKind enum values present (DriverNotLoaded, SteamVRNotRunning); `ms-settings:privacy-microphone` + `steam://rungameid/250820` URI literals present in fail_pill.cpp; `vrserverRunning` param present in fail_pill.hpp; no `windows.h` in the header (portable / testable headless); `deepLink` field name matches the Wave 0 RED scaffold contract.
- **Task 3** (driver_version): `MICMAP_VERSION_STRING` define present in `driver/CMakeLists.txt` (10-01 SSoT); `driverVersionGetter` present in `http_server.hpp`; `driver_version` JSON field present in `http_server.cpp`; `MICMAP_VERSION_STRING` lambda present in `device_provider.cpp`. `cmake --build build --target driver_micmap` builds clean. `cmake -DHTTP_SERVER_DIR=driver/src -P cmake/AssertHttpServerNoVrApi.cmake` → `clean (2 files scanned)`. `cmake -DHTTP_SERVER_DIR=driver/src -P cmake/AssertHttpServerLocalhostOnly.cmake` → `clean (2 files scanned)`.
- **Task 4** (main.cpp wiring + CMakeLists): all 9 grep landmarks present (`src/fail_pill.hpp` include, `src/process_check.hpp` include, `Local\\MicMap_Client_SingleInstance_v1`, `ShowWindow.*SW_RESTORE`, `pickActivePill`, `isProcessRunning`, `ShellExecuteW`, `src/fail_pill.cpp` in CMakeLists, `src/process_check.cpp` in CMakeLists). `cmake --build build --target micmap --config Debug` builds clean (only pre-existing LNK4098 LIBCMT noise from 10-01). `ctest -C Debug -R FailPillPriority --output-on-failure` → `1/1 Test #44: FailPillPriority ......... Passed 0.02 sec` (was build-RED at Wave 0).

**Regression-free check (all 6 pass):**

```
ctest --test-dir build -C Debug -R "TrayGlyphStateMachine|FailPillPriority|AssertCoVersioning|LogRotation|AssertHttpServerNoVrApi|AssertHttpServerLocalhostOnly"
1/6 Test #18: AssertHttpServerLocalhostOnly ....   Passed
2/6 Test #19: AssertHttpServerNoVrApi ..........   Passed
3/6 Test #42: AssertCoVersioning ...............   Passed
4/6 Test #43: TrayGlyphStateMachine ............   Passed (10-02 still GREEN despite HealthSnapshot.econnrefused field add)
5/6 Test #44: FailPillPriority .................   Passed (Wave 0 RED -> GREEN flip)
6/6 Test #45: LogRotation ......................   Passed
100% tests passed, 0 tests failed out of 6
```

**Build-side sanity (informational):**

- `build/bin/Debug/micmap.exe` links cleanly with the new `fail_pill.cpp` + `process_check.cpp` translation units.
- `build/driver/micmap/bin/win64/driver_micmap.dll` builds clean with the new `/health.driver_version` field. (Manual `curl http://127.0.0.1:27015/health` against a running driver would confirm the field appears; UAT-time verification, not in this autonomous executor's scope.)

**Visual / hardware verification deferred:** confirming that the FAIL pill actually renders + the deep-link button launches Settings + the Dismiss button POSTs `/state/clear-error` is a `human-verify` checkpoint best done as part of UAT for Phase 10. The plan does not request it; the test_fail_pill_priority GREEN flip + driver_micmap clean build + AssertHttpServerNoVrApi/LocalhostOnly clean are the gating signals for this plan.

## Verified Existing Artifacts (Plan-Required Documentation)

Per the plan's `<output>` section, document the actual code shape encountered:

- **FindWindowW class name**: `L"MicMapMain"` — matches the existing `RegisterClassExW(... L"MicMapMain", ...)` at `apps/micmap/main.cpp:1639`. The plan body's expected name was correct verbatim; no adjustment needed.
- **Existing IDriverApi accessor**: `driverClient` (member of `MicMapApp`, declared at `apps/micmap/main.cpp:79`). The plan's draft used `g_app.driverApi`; actual name is `driverClient`. Since the FAIL pill render is inside `MicMapApp::renderUI`, the unqualified `driverClient` resolves correctly to `this->driverClient` — no `g_app.` prefix needed.
- **Existing P8 D-11 driver-health pane**: located at `apps/micmap/main.cpp:835` (`ImGui::Text("Driver Health"); ImGui::Separator();` — line 835/836). The pre-existing `bool drvLoaded = driverLoadedIndicator.load();` at line 838 is preserved unchanged; the FAIL pill block is inserted between the `Separator()` and the `drvLoaded` declaration.
- **FAIL-04 mutex hardening line**: `apps/micmap/main.cpp:1614` (the `CreateMutexW(...)` call). The `if (GetLastError() == ERROR_ALREADY_EXISTS)` block at line 1623 with the `if (w)` body at lines 1627-1631 is where the `ShowWindow(SW_RESTORE)` insertion landed.
- **Poll-callback pill picker hook**: `apps/micmap/main.cpp:734-743` (just after the existing `applyTrayGlyph(nid, g_tray, desired)` call at line 730).
- **`MICMAP_VERSION_STRING` consumer in driver source code**: `driver/src/device_provider.cpp:69-70` (existing `#ifndef MICMAP_VERSION_STRING / #define MICMAP_VERSION_STRING "0.0.0"` defensive default block from 10-01 carryover). The new lambda `[](){ return std::string(MICMAP_VERSION_STRING); }` references the same define name, so the rename from `MICMAP_DRIVER_VERSION` → `MICMAP_VERSION_STRING` (10-01 D-18) was already complete by the time this plan landed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] FailPill struct field renamed `actionUri` → `deepLink` to match Wave 0 RED scaffold contract**
- **Found during:** Task 2 (writing fail_pill.hpp against the existing test scaffold)
- **Issue:** The plan's `<action>` block specified `std::string actionUri` as the FailPill field name. The Wave 0 RED scaffold `tests/test_fail_pill_priority.cpp:58` calls `pill->deepLink.find("ms-settings:privacy-microphone")`. Following the plan's name would have caused build-RED in `test_fail_pill_priority.exe` (no member named `actionUri`). The Wave 0 scaffold is the load-bearing contract per the plan's `<read_first>` block; the plan body's field name was a draft inconsistency.
- **Fix:** Renamed the struct field to `deepLink`. Semantic is identical (URI string passed to ShellExecuteW); only the field name differs. main.cpp's render path consumes `pill.deepLink` accordingly.
- **Files modified:** apps/micmap/src/fail_pill.hpp, apps/micmap/src/fail_pill.cpp, apps/micmap/main.cpp
- **Commit:** 7ca6971 (header/cpp), 28d4f10 (main.cpp render path)

**2. [Rule 3 — Blocking] HealthSnapshot extended with `bool econnrefused{false}` for positional brace-init compatibility**
- **Found during:** Task 2 (writing fail_pill.hpp against the existing test scaffold)
- **Issue:** The Wave 0 test scaffold `tests/test_fail_pill_priority.cpp:63` brace-inits `mc::HealthSnapshot health{ /*ok=*/false, /*econnrefused=*/true };` — passing a bool as the 2nd positional arg. The pre-existing `tray_glyph.hpp` `HealthSnapshot` had `std::string driverVersion` at position 2, which would refuse the bool→string conversion (narrowing). Following the plan's "fail_pill.hpp #includes tray_glyph.hpp to reuse HealthSnapshot/StateSnapshot" directive without addressing this would have caused build-RED in the test executable.
- **Fix:** Added `bool econnrefused{false}` as field 2 in `tray_glyph.hpp`'s `HealthSnapshot` (between `driverLoaded` and `driverVersion`). The brace-init now compiles. `deriveTrayGlyph` does not consume the new field — adding it does not change its behavior, and the existing `TrayGlyphStateMachine` ctest still passes (verified). This is the cleanest landing because tray_glyph.hpp is the canonical HealthSnapshot site per 10-02 SUMMARY's contract that 10-03 fail_pill + 10-06 version_mismatch reuse via `#include`.
- **Files modified:** apps/micmap/src/tray_glyph.hpp
- **Commit:** 7ca6971

### Auth Gates

None encountered. All work was offline/local.

## Threat Model Compliance

All 6 STRIDE threats from the plan's threat register are addressed:

- **T-10-03-01** (Tampering, foreign process named "vrserver.exe" spoofs FAIL-02/03 disambiguation): accepted. Out of scope for v1.6 to verify process publisher (would require Sysmon-equivalent integration). Worst case is the FAIL pill text differs (DriverNotLoaded vs SteamVRNotRunning) — user-visible UI nudge only, no privilege change.
- **T-10-03-02** (DoS, tasklist-cost UI freeze): mitigated. `process_check.cpp` uses `CreateToolhelp32Snapshot` (microseconds) with a 2-second mutex-guarded cache. NEVER `tasklist.exe` (50-200ms wall-clock spawn cost). `isProcessRunning` is called only from the UI thread's poll callback at the 1Hz health-poll cadence; the cache amortizes to one snapshot every 2 seconds while the driver is unreachable.
- **T-10-03-03** (Tampering, mutex name collision): mitigated. `Local\\MicMap_Client_SingleInstance_v1` is vendor-prefixed + version-suffixed; collision unlikely with any other application's mutex namespace.
- **T-10-03-04** (EoP, second-instance steals focus from VR session): mitigated. Existing P3 D-08 minimized-skip carve-out preserved verbatim — when `flags.minimized` is true (SteamVR auto-relaunch via `app.vrmanifest`), the entire `if (w) { ... }` window-surfacing block is skipped; only the user-clicked launch path (no `--minimized`) brings the existing instance forward via the new `ShowWindow(SW_RESTORE) + SetForegroundWindow` pair.
- **T-10-03-05** (Tampering, foreign process invokes ShellExecuteW with crafted ms-settings URI): accepted. The URIs are string literals in `fail_pill.cpp`; not user-controllable. The only attack surface is a tamper-with-binary which is out of scope.
- **T-10-03-06** (Information disclosure, driver_version exposes binary version on localhost): accepted. Localhost-only binding (P8 IPC-07); same machine = same trust as the binary itself. Per CONTEXT, semver is not a security secret.

## Threat Flags

None — this plan creates no new network endpoints (the existing `/health` route is extended by one field, not added; AssertHttpServerLocalhostOnly stays GREEN), no new auth paths, no new file-access patterns at trust boundaries (the `CreateToolhelp32Snapshot` enumeration is read-only and the OS already guards process visibility), and no schema changes to the persistence layer (no `config.json` or `training_data.bin` writes).

## Known Stubs

None blocking the plan's goal. The `HealthSnapshot.driverDetectionActive` and `HealthSnapshot.driverVersion` fields are still set to default values in `main.cpp`'s poll-tick materialization (`pollDriverHealth` line ~700) — this is intentional: `pickActivePill` does not consume these fields (the FAIL pill flow only depends on `driverLoaded` + `econnrefused`). They will be wired to the real `/health` envelope values when 10-06 (version mismatch pill) needs them. The unused fields are a known plan-of-work seam, not a stub blocking FAIL-01..05 / D-19 delivery for 10-03; pickActivePill operates correctly without them.

## Commits

| Task | Description                                                                              | Commit  |
| ---- | ---------------------------------------------------------------------------------------- | ------- |
| 1    | feat(10-03): add process_check helper -- isProcessRunning + 2s cache                     | b982515 |
| 2    | feat(10-03): add fail_pill -- pickActivePill D-08 priority + deep-links                  | 7ca6971 |
| 3    | feat(10-03): GET /health emits driver_version field (D-19)                               | e8db6be |
| 4    | feat(10-03): wire FAIL pills + harden FAIL-04 mutex + register sources                   | 28d4f10 |

## Self-Check

- apps/micmap/src/process_check.hpp — FOUND
- apps/micmap/src/process_check.cpp — FOUND
- apps/micmap/src/fail_pill.hpp — FOUND
- apps/micmap/src/fail_pill.cpp — FOUND
- apps/micmap/src/tray_glyph.hpp (econnrefused field added) — FOUND
- apps/micmap/main.cpp (include + g_failUx + mutex hardening + pickActivePill hook + render block) — FOUND
- apps/micmap/CMakeLists.txt (fail_pill.cpp + process_check.cpp registered) — FOUND
- driver/src/http_server.hpp (driverVersionGetter ctor param + member) — FOUND
- driver/src/http_server.cpp (ctor init + /health driver_version emit) — FOUND
- driver/src/device_provider.cpp (driverVersionGetter lambda + ctor arg) — FOUND
- ctest FailPillPriority — PASS (was build-RED at Wave 0)
- ctest TrayGlyphStateMachine — PASS (no regression from HealthSnapshot.econnrefused add)
- ctest AssertCoVersioning + LogRotation + AssertHttpServerNoVrApi + AssertHttpServerLocalhostOnly — 4/4 PASS
- micmap.exe build — clean (only pre-existing LNK4098 LIBCMT noise)
- driver_micmap.dll build — clean
- b982515 — FOUND (`git log --oneline` confirms; Task 1 commit)
- 7ca6971 — FOUND (`git log --oneline` confirms; Task 2 commit)
- e8db6be — FOUND (`git log --oneline` confirms; Task 3 commit)
- 28d4f10 — FOUND (`git log --oneline` confirms; Task 4 commit)

## Self-Check: PASSED
