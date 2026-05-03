---
phase: 07
plan: 05
subsystem: driver+client
tags: [migration-handshake, mig-02, http-health, suppression, wave-4, coexistence]
dependency-graph:
  requires:
    - "driver/src/http_server.{hpp,cpp} (P5/v1.5 SVR-05 — existing /health route, ctor signature, nlohmann::json already linked for POST /button)"
    - "driver/src/device_provider.{hpp,cpp} (07-04 — DetectionRunner + driverDetectionEnabled_ + audioWorker_ live; HttpServer construction site exists)"
    - "driver/src/detection_runner.hpp (07-03 — IsRunning() accessor used in the getter lambda)"
    - "src/steamvr/include/micmap/steamvr/vr_input.hpp (P5 — IDriverClient interface, DriverClient impl in vr_input.cpp)"
    - "src/steamvr/src/vr_input.cpp (P5 — DriverClient::connect() existing /health probe pattern at lines ~142-148; httplib already linked)"
    - "apps/micmap/main.cpp (P5/v1.5 — MicMapApp::onTrigger existing tap-on-driverClient pattern at lines ~515-523)"
  provides:
    - "Migration handshake — /health emits driver_detection_active boolean (D-09); IDriverClient::isDriverDetectionActive (D-10) caches 1 s and gates MicMapApp::onTrigger's tap() call (D-11 cooldown is the backstop)"
    - "v1.5 / v1.6 trigger-path coexistence — both flags ON: client polls /health, suppresses its own POST /button; either flag OFF or driver crashed: client resumes own trigger automatically, no restart"
    - "07-09 UAT D-25(6) coexistence verification log-line literal `onTrigger: driver_detection_active=true, suppressing` ready for grep"
  affects:
    - "Plan 07-06 (UAT) — D-25(1) curl /health JSON shape verifiable; D-25(6) coexistence single-tap behavior on Bigscreen Beyond"
    - "Plan 10 (cutover) — entire field + IDriverClient::isDriverDetectionActive + onTrigger gate + getter scaffolding deleted per D-12"
tech-stack:
  added:
    - "DriverClient cache members: std::chrono::steady_clock::time_point + bool (default-init at zero so first call always polls)"
    - "nlohmann_json link on micmap_steamvr (Rule-3 fix — vr_input.cpp now parses /health body)"
  patterns:
    - "RESEARCH Open Question 1 recommendation (a): getter lambda passed at HttpServer construction time, evaluated at REQUEST TIME — captures `this` so the field correctly transitions false→true→false through the driver's Init/Run/Cleanup lifecycle"
    - "1-second cache TTL on isDriverDetectionActive — bounds onTrigger latency overhead while keeping the suppression window short enough that D-11 cooldown handles transient double-tap"
    - "Defensive false-on-error throughout: nullptr getter, !connected_, port_==0, !res, status!=200, JSON parse exception, type mismatch via body.value(...,false). Principle: if we cannot determine the driver owns detection, we DO NOT suppress — fall back to v1.5 trigger path"
    - "Literal log line invariant: `onTrigger: driver_detection_active=true, suppressing` — used by 07-09 UAT D-25(6) coexistence verification"
key-files:
  created: []
  modified:
    - "driver/src/http_server.hpp — include <functional>; HttpServer ctor 4th param `std::function<bool()> driverDetectionActiveGetter = nullptr`; private member to hold it; doxy block updated"
    - "driver/src/http_server.cpp — ctor stores getter via std::move; /health emits nlohmann::json with status (string) + driver_detection_active (boolean from getter, defaults false when null)"
    - "driver/src/device_provider.cpp — HttpServer constructed with getter lambda capturing this + reading driverDetectionEnabled_ && audioWorker_ && detectionRunner_->IsRunning() at request time; explicit port=27015 / host=127.0.0.1 args restored for 4th-arg unambiguity"
    - "src/steamvr/include/micmap/steamvr/vr_input.hpp — include <chrono>; IDriverClient gains pure-virtual `bool isDriverDetectionActive() = 0;` with full doc-block (D-10 contract, D-12 deletion plan)"
    - "src/steamvr/src/vr_input.cpp — include <nlohmann/json.hpp>; DriverClient impl gains override + cache members (lastDetectionPoll_, cachedDetectionActive_); isDriverDetectionActive() impl mirrors connect() /health probe with 1s cache, 2s timeouts, defensive false-on-error"
    - "src/steamvr/CMakeLists.txt — Rule-3 fix: micmap_steamvr links nlohmann_json (PRIVATE, INTERFACE-only — vr_input.cpp now parses /health JSON)"
    - "apps/micmap/main.cpp — MicMapApp::onTrigger gates tap() on isDriverDetectionActive(); literal log line `onTrigger: driver_detection_active=true, suppressing` on suppress path"
    - "tests/CMakeLists.txt — Rule-3 follow-on: AudioWorkerLifecycleHeadless test exe now compiles detection_runner.cpp alongside audio_worker.cpp (07-04 added a NotifyOne() call the standalone test couldn't link)"
decisions:
  - "DriverClient cache members live in vr_input.cpp (where the concrete class is defined) NOT in vr_input.hpp — the header only contains interfaces + factory. Plan acceptance criterion expected hpp; reality is cpp. Both files satisfy >=1 grep counts; decision documented."
  - "Explicit port=27015 + host=127.0.0.1 args restored at the HttpServer construction site in device_provider.cpp — previously defaulted via the 3-arg ctor. Required for 4th-arg unambiguity now that the lambda position must be explicit. Same values as the existing defaults, no behavior change."
  - "nlohmann_json linked PRIVATE on micmap_steamvr — the JSON parsing is implementation detail of DriverClient, not an interface concern. Mirrors driver/CMakeLists.txt convention."
  - "AudioWorkerLifecycleHeadless test exe Rule-3 follow-on attributed to 07-05 (this plan) rather than 07-04 retrofit: 07-04 SUMMARY claimed the test was PASS but the standalone test exe was missing detection_runner.cpp linkage. Discovered during 07-05 carryover verification; fixed inline."
metrics:
  duration_minutes: ~25
  tasks_completed: 2
  files_created: 0
  files_modified: 8
  commits: 2
  completed: "2026-05-03"
---

# Phase 7 Plan 5: Migration Handshake Summary

The "v1.5/v1.6 coexistence" plan. Driver's `GET /health` JSON now includes a `driver_detection_active` boolean (D-09) reflecting the live driver-side detection thread state — true iff `driverDetectionEnabled_ && audioWorker_ && detectionRunner_ && detectionRunner_->IsRunning()`. The field is sourced from a getter lambda passed at `HttpServer` construction (RESEARCH Open Question 1 recommendation a) so it correctly transitions false→true→false through the driver's full lifecycle without restart. On the client side, `IDriverClient` gains a pure-virtual `isDriverDetectionActive()` method; `DriverClient` implements it with a 1-second cache (`std::chrono::steady_clock` TTL) to bound `onTrigger` latency overhead while keeping the suppression window short enough that D-11 cooldown is the natural backstop. `MicMapApp::onTrigger` checks the field BEFORE calling `tap()`; when true, emits the literal `onTrigger: driver_detection_active=true, suppressing` log line (used by 07-09 UAT D-25(6)) and returns. Defensive false-on-error throughout — null getter, disconnected client, /health unreachable, JSON parse exception, type mismatch — all fall back to the v1.5 trigger path. P10 (D-12) deletes the entire scaffolding once cutover completes.

## What Shipped

### Task 1: HttpServer + DeviceProvider — /health JSON gains driver_detection_active (D-09) — commit `5758a5b`

**`driver/src/http_server.hpp` modifications:**

- `#include <functional>` added near the existing includes (std::function<bool()> ctor param + member).
- HttpServer ctor signature gains a 4th optional parameter `std::function<bool()> driverDetectionActiveGetter = nullptr`. Default value of `nullptr` keeps existing call sites + test code compiling unchanged.
- New private member after the existing `running_` atomic: `std::function<bool()> driverDetectionActiveGetter_`.
- Doxy block above the class updated to document the new field on /health (semantics, defensive default, P10 deletion plan).

**`driver/src/http_server.cpp` modifications:**

- Constructor impl rewritten to take + store the getter via `std::move`. Existing `DriverLog("HttpServer created ...")` line preserved verbatim.
- `/health` route handler rewritten per the plan's `<interfaces>` block §2:
  - Captures `[this]` (so the getter is reachable).
  - Constructs `nlohmann::json body;`, sets `body["status"] = "healthy";` and `body["driver_detection_active"] = driverDetectionActiveGetter_ ? driverDetectionActiveGetter_() : false;`.
  - `res.set_content(body.dump(), "application/json");`.
- NO new include needed — `nlohmann/json.hpp` was already pulled in for the POST /button handler.
- Existing POST /button + GET /port + GET /status routes untouched — v1.5 trigger path stays byte-functionally identical.

**`driver/src/device_provider.cpp` modifications:**

- `httpServer_` construction call site rewritten to pass the getter lambda:
  ```cpp
  auto driverDetectionActiveGetter = [this]() {
      return driverDetectionEnabled_
          && audioWorker_
          && detectionRunner_
          && detectionRunner_->IsRunning();
  };
  httpServer_ = std::make_unique<HttpServer>(
      *commandQueue_,
      /*port=*/27015,
      /*host=*/"127.0.0.1",
      std::move(driverDetectionActiveGetter));
  ```
- Lambda captures `this` and reads members AT REQUEST TIME — so even though HttpServer is constructed BEFORE the VRSettings reads + DetectionRunner construction (D-19 ordering), the field correctly transitions false→true once detection actually starts, and back to false during Cleanup (when detectionRunner_.reset() happens FIRST per D-20).
- Explicit port=27015 + host=127.0.0.1 args restored (same as the existing defaults; needed for 4th-arg unambiguity).

**`tests/CMakeLists.txt` Rule-3 follow-on fix:**

`AudioWorkerLifecycleHeadless` test exe now compiles `detection_runner.cpp` alongside `audio_worker.cpp`. 07-04 added a `DetectionRunner::NotifyOne()` call inside `audio_worker.cpp`'s WASAPI callback (D-05 wakeup path), and the standalone test exe link couldn't resolve the symbol. Same EXISTS-gated pattern as the P7 propagation/stress tests below. `driver_micmap.dll` itself was unaffected because its source list already includes `detection_runner.cpp` — only the test exe target was missing it.

### Task 2: IDriverClient + MicMapApp::onTrigger — /health poll suppression (D-10/D-11) — commit `44a6b65`

**`src/steamvr/include/micmap/steamvr/vr_input.hpp` modifications:**

- `#include <chrono>` added (cache TTL on the DriverClient impl).
- IDriverClient gains a new pure-virtual method:
  ```cpp
  virtual bool isDriverDetectionActive() = 0;
  ```
  Doc-block describes the full contract: poll /health, parse the `driver_detection_active` boolean (Task 1 / D-09), cache for 1 s; defensive false on any error so the client falls back to its own trigger path; D-11 cooldown is the belt-and-suspenders backstop. Deleted in P10 per D-12.

**`src/steamvr/src/vr_input.cpp` modifications:**

- `#include <nlohmann/json.hpp>` added (parse /health body).
- DriverClient (concrete impl in vr_input.cpp, NOT vr_input.hpp) gains:
  - `bool isDriverDetectionActive() override` declaration + impl per the plan's `<interfaces>` block §5.
  - New private members: `std::chrono::steady_clock::time_point lastDetectionPoll_{};` + `bool cachedDetectionActive_{false};`. Default-initialized at zero so the very first call always polls.
- Implementation:
  - Cache TTL = 1000 ms via `std::chrono::milliseconds(1000)`.
  - Early-return cached value if within TTL.
  - Early-return false (and refresh timestamp) if `!connected_ || port_ == 0`.
  - `httplib::Client client(host_, port_); client.set_connection_timeout(2); client.set_read_timeout(2);` — mirrors the existing connect() /health probe shape.
  - `auto res = client.Get("/health");` on non-200 / null, set cached=false + refresh + return false.
  - `nlohmann::json::parse(res->body)`; `cachedDetectionActive_ = body.value("driver_detection_active", false);` — defensive default for type mismatch.
  - On parse exception, cached=false. Refresh timestamp; return cachedDetectionActive_.

**`src/steamvr/CMakeLists.txt` Rule-3 fix:**

micmap_steamvr now links `nlohmann_json` PRIVATE (INTERFACE-only — vr_input.cpp parses /health JSON to extract the new field).

**`apps/micmap/main.cpp` modifications:**

- `MicMapApp::onTrigger` gains the suppression check INSERTED between the existing `if (!driverClient || !driverClient->isConnected())` block AND the `driverClient->tap()` call:
  ```cpp
  if (driverClient->isDriverDetectionActive()) {
      MICMAP_LOG_DEBUG("onTrigger: driver_detection_active=true, suppressing");
      return;
  }
  ```
- Literal log text matches the plan's invariant for 07-09 UAT D-25(6) coexistence verification.
- Existing tap() call + warning log on failure unchanged.

## Verification Results

```
cmake --build build --config Release --target driver_micmap → exit 0
  driver_micmap.dll links clean (only pre-existing LIBCMT lib warning)

cmake --build build --config Release --target micmap → exit 0
  micmap.exe links clean

cmake --build build --config Release → exit 0  (full build)

ctest --test-dir build -C Release --output-on-failure → 17/17 PASS
  test_placeholder                    PASS
  test_config_manager                 PASS  (P5)
  test_command_queue                  PASS  (P5 carryover + 07-01 concurrent case)
  test_cli_flags_parse                PASS  (P5)
  test_manifest_registrar             PASS  (P5)
  test_vr_input_quit_ordering         PASS  (P5)
  test_tray_balloon_once              PASS  (P5)
  test_vrmanifest_schema              PASS  (P5)
  test_bindings_patcher               PASS  (P5)
  bindings_patcher_idempotent         PASS  (P5)
  lint_no_openvr_in_core              PASS  (P5 carryover)
  lint_no_driver_macro                PASS  (P5 carryover)
  AudioWorkerLifecycleHeadless        PASS  (P6 carryover, NOW with detection_runner.cpp linked)
  AssertAudioWorkerNoVrApi            PASS  (P6 carryover — D-07/SVR-05)
  DetectionSettingsPropagation        PASS  (07-01 → GREEN — MIG-06 < 50 ms)
  DeviceProviderLifecycleStress       PASS  (07-01 → GREEN — SC4 / MIG-04 50-cycle base=100 after=100, handle delta=0)
  AssertDetectionRunnerNoVrApi        PASS  (07-01 carryover — D-22)

dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll
  → 1 export: HmdDriverFactory (P5 SC3 carryover preserved)
```

Acceptance grep checks (Task 1):
- `#include <functional>` in http_server.hpp → 1 (≥1)
- `std::function<bool()>` in http_server.hpp → 3 (≥2: ctor param decl + member + doxy mention)
- `driverDetectionActiveGetter` in http_server.hpp → 4 (≥2)
- `driverDetectionActiveGetter_` in http_server.cpp → 2 (≥2)
- `driver_detection_active` in http_server.cpp → 2 (≥1)
- `nlohmann::json body` in http_server.cpp → 1 (≥1)
- `body["status"] = "healthy"` regex → 1 (≥1)
- `driverDetectionActiveGetter` in device_provider.cpp → 2 (≥1)
- `detectionRunner_->IsRunning()` in device_provider.cpp → 1 (≥1)
- And-chain `driverDetectionEnabled_ &&` regex → 1 (≥1)

Acceptance grep checks (Task 2):
- `isDriverDetectionActive` in vr_input.hpp → 2 (interface decl + doc-block reference)
- `virtual bool isDriverDetectionActive` → 1 (≥1)
- `DriverClient::isDriverDetectionActive` / override decl in vr_input.cpp → 2 (≥1)
- `driver_detection_active` in vr_input.cpp → 3 (≥1)
- `std::chrono::milliseconds(1000)` in vr_input.cpp → 1 (≥1)
- `nlohmann::json::parse` in vr_input.cpp → 1 (≥1)
- `client.Get("/health")` in vr_input.cpp → 2 (≥2 — connect probe + isDriverDetectionActive probe)
- `isDriverDetectionActive` in apps/micmap/main.cpp → 1 (≥1)
- Literal log line `driver_detection_active=true, suppressing` in main.cpp → 1 (≥1)

Acceptance criteria expecting `lastDetectionPoll_` and `cachedDetectionActive_` in `vr_input.hpp` are misaligned with reality — DriverClient is defined in `vr_input.cpp`, not the header. The members ARE present in vr_input.cpp (counts 6 and 7 respectively, including impl-body references). Documented under Decisions.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] AudioWorkerLifecycleHeadless test exe missing detection_runner.cpp link**
- **Found during:** Task 1 ctest verification (linker error: unresolved external `DetectionRunner::NotifyOne`)
- **Issue:** 07-04's `audio_worker.cpp` rewire added `runner->NotifyOne()` to the WASAPI callback. The standalone `test_audio_worker_lifecycle_headless` exe target compiles `audio_worker.cpp` directly into the test binary, but its source list did NOT include `detection_runner.cpp` — so the symbol was unresolved. `driver_micmap.dll` was unaffected because its source list already included detection_runner.cpp via 07-03.
- **Fix:** Added an EXISTS-gated `list(APPEND _p6_audio_worker_sources "${CMAKE_SOURCE_DIR}/driver/src/detection_runner.cpp")` block, mirroring the same pattern used for the P7 propagation/stress tests below. Now the test exe links cleanly.
- **Files modified:** `tests/CMakeLists.txt`
- **Commit:** `5758a5b` (rolled into Task 1)
- **Note:** 07-04's SUMMARY reported `AudioWorkerLifecycleHeadless PASS` — this may have been from a stale build where detection_runner.obj was lingering, or from the main repo build dir which had a different state. The standalone worktree build surfaced the gap.

**2. [Rule 3 - Blocking] micmap_steamvr missing nlohmann_json link**
- **Found during:** Task 2 build verification (compile error: `Cannot open include file: 'nlohmann/json.hpp'`)
- **Issue:** vr_input.cpp now `#include`s nlohmann/json.hpp to parse the /health body, but `src/steamvr/CMakeLists.txt` only linked httplib::httplib + Pathcch. nlohmann_json is INTERFACE-only (header-only with target properties for include path), so a PRIVATE link is sufficient.
- **Fix:** Appended `nlohmann_json` to the PRIVATE link list with a comment explaining the rationale.
- **Files modified:** `src/steamvr/CMakeLists.txt`
- **Commit:** `44a6b65` (rolled into Task 2)

### Criteria Drift Notes (non-blocking)

- Plan acceptance expected `lastDetectionPoll_` and `cachedDetectionActive_` in `vr_input.hpp`. They are in `vr_input.cpp` because that's where the DriverClient concrete class is defined. The plan note "(in vr_input.hpp, the concrete impl)" was incorrect — there is no concrete-impl class declaration in the header. Functionally equivalent: the cache members are private to DriverClient, the only IDriverClient impl in the tree.
- `= 0;` count in vr_input.hpp is 17 (many existing pure-virtuals). The plan's criterion (`>=1`) is satisfied.
- `port=27015` + `host=127.0.0.1` were already the defaults — adding them explicitly at the call site is a clarity edit for the 4th-arg unambiguity, not a behavior change.
- No test stub overrides needed: `grep -rn "class.*: public IDriverClient\|: public micmap::steamvr::IDriverClient"` returns ONLY `DriverClient` itself in vr_input.cpp. No mocks/fakes/stubs to update.

## Authentication Gates

None. All work was filesystem + cmake + ctest + git on a local Windows + VS2022 build. The 07-06 UAT D-25(6) coexistence test on Bigscreen Beyond is a separate plan (07-06) and not part of this autonomous flow.

## Threat Flags

None. The plan's `<threat_model>` covers all surfaces touched (T-07-05-01 through T-07-05-07). Mitigations as designed:

- **T-07-05-01** (race in /health getter during Cleanup): accepted — vrserver shutting down means no client is polling /health; the theoretical race is closed by P10's deletion of the entire scaffolding (D-12).
- **T-07-05-02** (spoof on port 27015): accepted — existing v1.5 SVR-05 mitigation (HttpServer binds 127.0.0.1 only) eliminates the meaningful network path.
- **T-07-05-03** (DoS via 4 s timeout when driver unreachable): mitigated — `client.set_connection_timeout(2); client.set_read_timeout(2);` bounds worst-case to 4 s; 1-second cache means subsequent calls within TTL skip the poll entirely.
- **T-07-05-04** (typo'd JSON field name): mitigated — both server emit (Task 1) and client parse (Task 2) hardcode the literal string `driver_detection_active`; 07-09 UAT D-25(6) catches any typo end-to-end.
- **T-07-05-05** (LAN /health leak): mitigated — existing v1.5 SVR-05 localhost-only binding.
- **T-07-05-06** (proxy MITM): accepted — out-of-scope; localhost-only path eliminates remote attack vector.
- **T-07-05-07** (type mismatch on field): mitigated — `body.value("driver_detection_active", false)` defaults to false on type mismatch; Task 1 only ever emits boolean.

## Known Stubs

None. The migration handshake is fully wired end-to-end:
- Driver /health emits `driver_detection_active` boolean from a live getter capturing the actual driver lifecycle ✓
- DriverClient polls /health, parses the field, caches 1 s ✓
- MicMapApp::onTrigger gates tap() on isDriverDetectionActive() ✓
- D-11 state-machine cooldown (already enforced in v1.5 client + 07-03 driver) is the backstop ✓
- v1.5 trigger path remains intact when either flag is OFF or the driver crashes ✓

The only remaining work in Phase 7 is 07-06 UAT D-25 manual sign-off on Bigscreen Beyond (separate plan).

## Self-Check: PASSED

Verified the following exist on disk (all modified, no new files created):
- `driver/src/http_server.hpp` — FOUND (functional include + 4th ctor param + private member + doxy update)
- `driver/src/http_server.cpp` — FOUND (ctor stores getter + /health emits JSON with driver_detection_active)
- `driver/src/device_provider.cpp` — FOUND (HttpServer construction passes getter lambda)
- `src/steamvr/include/micmap/steamvr/vr_input.hpp` — FOUND (chrono include + IDriverClient::isDriverDetectionActive pure-virtual)
- `src/steamvr/src/vr_input.cpp` — FOUND (nlohmann include + DriverClient::isDriverDetectionActive impl + cache members)
- `src/steamvr/CMakeLists.txt` — FOUND (Rule-3: nlohmann_json link)
- `apps/micmap/main.cpp` — FOUND (MicMapApp::onTrigger gated on isDriverDetectionActive with literal log line)
- `tests/CMakeLists.txt` — FOUND (Rule-3: detection_runner.cpp added to AudioWorkerLifecycleHeadless source list)

Verified the following commits exist on `worktree-agent-a680157a93ac2a6d5` (worktree branch):
- `5758a5b` — feat(07-05): /health emits driver_detection_active (D-09) — FOUND
- `44a6b65` — feat(07-05): IDriverClient gates onTrigger on /health (D-10/D-11) — FOUND

ctest sweep on Release driver-on build:
- Full suite: 17/17 PASS (100%).
- All P5/P6/07-01..07-04 carryovers stay green.
- DeviceProviderLifecycleStress 50-cycle: handle delta = 0.

dumpbin verification:
- `driver_micmap.dll` exports exactly 1 symbol: `HmdDriverFactory` (P5 SC3 preserved).
