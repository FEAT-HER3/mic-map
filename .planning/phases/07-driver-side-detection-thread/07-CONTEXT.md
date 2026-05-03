# Phase 7: Driver-Side Detection Thread - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning

<domain>
## Phase Boundary

Move FFT detection + state machine into `driver_micmap.dll`. New `DetectionRunner` thread drains an SPSC sample ring fed by the P6 `AudioWorker`'s WASAPI callback, runs `INoiseDetector::analyze()` + `IStateMachine::update()` (both reused from `micmap::core_runtime`), and on rising-edge Triggered pushes `TapCommand` into the existing v1.5 `CommandQueue`. Trigger collapses 7-hop cross-process → 4-hop in-process. Behind `enable_driver_detection` flag (default OFF). v1.5 `POST /button` + `IDriverClient::tap()` survive in parallel as the rollback path until P10.

**In scope:** new files `driver/src/detection_runner.{hpp,cpp}` + `driver/src/sample_ring.hpp`; `default.vrsettings` keys (`enable_driver_detection`, `detection_sensitivity`, `detection_threshold`, `detection_cooldown_ms`, `detection_min_duration_ms`); `AudioWorker` audio-callback rewired to push frames into ring (RMS-log path retained behind debug define); `DeviceProvider::Init`/`Cleanup` extended for DetectionRunner construction/teardown (reverse order, before AudioWorker); lock-free `std::atomic<std::shared_ptr<const DetectionConfig>>` snapshot mechanism + `publish()` API; `GET /health` JSON gains `driver_detection_active` field for client double-trigger suppression; CMake lint extended to assert no `vr::*` in `detection_runner.cpp` / `sample_ring.hpp`; HMD standby pause/resume; 50-cycle Init→500ms→Cleanup stress test (SC4); MIG-06 50ms-propagation headless test; UAT on Bigscreen Beyond + Win11 Pro.

**Out of scope:** `PUT /settings` HTTP endpoint and any new IPC routes (P8 IPC-04); driver as `config.json` reader/writer (P8 IPC-05); `GET /devices`, `GET /telemetry/level`, `GET /state` endpoints (P8); `LIB-04` logger sink injection (P8); training migration / `POST /training/*` endpoints (P9); `POST /button` deletion + `IDriverClient::tap()` removal (P10 MIG-05); `enable_driver_detection` default flip ON (P10 cutover); `OnDefaultDeviceChanged` follow-the-default logic (Pitfall 14, P8+); `DeviceNotificationClient` ComPtr migration (CONCERNS.md, P10+); device pinning via config; tray-icon state glyphs (P10 HEALTH-08); FAIL-01..05 graceful failure UX (P10).

</domain>

<decisions>
## Implementation Decisions

### A. SPSC ring + audio→detection plumbing

- **D-01:** New driver-only header `driver/src/sample_ring.hpp`. Single-producer (WASAPI capture thread) / single-consumer (DetectionRunner thread) bounded ring of fixed-size `float` blocks, contiguous backing storage, power-of-two slot count, `std::atomic<size_t>` head/tail with acquire/release fences. Header-only (template on slot-count is fine; default 16 slots). Stays in `driver/` (not shared lib) — coupled to detection-thread design, isolated TU enables targeted unit tests.
- **D-02:** Slot payload = `std::array<float, kSlotFrames>` where `kSlotFrames` matches WASAPI shared-mode buffer cadence (~480 samples = 10ms @ 48kHz). 16 slots × 480 frames ≈ 160ms backlog ceiling. Claude's discretion on exact slot count and payload shape — pick whatever keeps push() branch-free.
- **D-03:** Overflow policy = **drop-OLDEST**. On full ring, producer advances tail (reclaims oldest slot) and writes the new block. Audio thread NEVER blocks (Pitfall 12 hard rule). Detection cares about recent acoustic state — old frames during a stall are stale. Drop count tracked in an `atomic<uint32_t>` for diagnostic logging.
- **D-04:** Wakeup = `std::condition_variable` + `std::mutex` on DetectionRunner. Audio thread `notify_one()` after each push (cheap; cv backed by SRWLOCK on Windows). Detection thread `cv_.wait_for(lk, kWakeTimeout, predicate)` — predicate = `shutdown_ || paused_ || ring_has_data()`. Short timeout (Claude's discretion 25–50ms) bounds shutdown latency without busy-spinning.
- **D-05:** Audio callback is rewired in `AudioWorker::RunWorker()`'s `setAudioCallback` lambda (currently RMS-log only). New shape: (1) `weak_ptr<State>` lock + `alive` check (Pitfall 13, unchanged from P6 D-15/D-16); (2) push frames into ring (drop-oldest on full); (3) `cv_.notify_one()` on the DetectionRunner if attached; (4) RMS-log block from P6 retained behind a `#ifdef MICMAP_DEBUG_RMS_LOG` so production driver doesn't flood `vrserver.txt`.

### B. HMD standby behavior

- **D-06:** `EnterStandby` / `LeaveStandby` pause **detection only**. AudioWorker keeps capturing — closing/reopening WASAPI on every HMD wake/sleep would cycle Pitfall 4's lifecycle pressure 1000× more often than needed (audio device handles are expensive to release/reacquire and `IMMNotificationClient` re-register is the highest-risk call in the worker).
- **D-07:** Pause mechanism: `DetectionRunner` exposes `Pause()` / `Resume()` callable from `RunFrame` (the only thread allowed to react to standby events). `Pause()` flips `paused_` atomic + `cv_.notify_one()`. Detection thread on wake checks `paused_` → enters drain-and-discard loop (drains ring without running FFT/state machine — keeps the ring from filling, prevents drop-storm logspam on wake) → `cv_.wait` again. State machine is NOT reset on pause (resume continues from same state; cooldown timers keep ticking).
- **D-08:** SC3 verification: HMD wake/sleep ×2 cycles in UAT D-17(2). Process Explorer handle count stable; no leaked WASAPI/COM handles; detection thread resumes producing TapCommands within one detection cycle of `LeaveStandby`. Reuses P6 D-17(2) test rig shape.

### C. Double-trigger coexistence (Pitfall 10)

- **D-09:** `GET /health` JSON response gains a `"driver_detection_active": bool` field. Field is `true` iff `enable_driver_detection=1` AND `enable_driver_audio=1` AND DetectionRunner constructed successfully (i.e., the driver actually has its own trigger path live). Field added in P7; client consumption added in same phase (small client diff).
- **D-10:** Client polls `/health` (already does in v1.5 connection indicator). When `driver_detection_active=true` is observed, client suppresses its own trigger pipeline (audio→detection→`IDriverClient::tap()`) at the trigger site — keeps detection running for the UI's own confidence/RMS visualization but drops the trigger call. When the field flips false (driver flag off, or driver crashed/restarted), client resumes its own trigger path automatically. No restart required.
- **D-11:** Belt-and-suspenders: state machine cooldown (already enforced) squashes accidental back-to-back triggers within the cooldown window. So even if /health hasn't been polled yet at the moment the flag flips, worst case = one transient double-tap, then the cooldown swallows the rest until the client catches up. Acceptable per Pitfall 10 guidance.
- **D-12:** P10 deletes `POST /button`, `IDriverClient::tap()`, and the `driver_detection_active` field + suppression dance entirely. P7's coexistence machinery is transient migration scaffolding by design.

### D. Settings source for driver detection in P7

- **D-13:** Detection settings read once at `DeviceProvider::Init` from VRSettings (driver-native, no shared-lib touch — same single-read pattern as P6 D-01). New keys added to `default.vrsettings` `driver_micmap` section:
  - `enable_driver_detection` (bool, default `false`)
  - `detection_sensitivity` (float, default 0.7)
  - `detection_threshold` (float, default 0.6)
  - `detection_cooldown_ms` (int32, default 1000)
  - `detection_min_duration_ms` (int32, default 200)
  Defaults mirror v1.5 client-side config.json defaults (client-side defaults remain authoritative reference; document the value-mirror in PLAN.md so P8 cutover doesn't drift).
- **D-14:** P7 does NOT add `PUT /settings`, does NOT read `config.json`, does NOT pull `ConfigManager`/`nlohmann::json` into the driver. Those are P8 (IPC-04, IPC-05). Sole reason: P5 D-15/D-16 + P6 D-02 deferral chain — keep json bloat (Pitfall 15) out of the driver until the IPC reshape phase commits to it.
- **D-15:** MIG-06 50ms-propagation requirement is satisfied by the **mechanism**, not by an HTTP path in P7. `DetectionRunner` exposes `publish(std::shared_ptr<const DetectionConfig>)` that swaps an internal `std::atomic<std::shared_ptr<const DetectionConfig>>` (C++20 `atomic<shared_ptr>` if available; otherwise `std::atomic_store_explicit` on a `shared_ptr` member with documented platform fallback). Detection hot path reads via `std::atomic_load` once per detection cycle — no lock. P7 ships a headless test (`tests/driver/detection_settings_propagation_test.cpp`) that calls `publish()` from a worker thread, measures `steady_clock` until the detection thread observes the new value, asserts < 50ms. P8 wires `PUT /settings` HTTP handler to call `publish()`.
- **D-16:** Driver only consumes the `DetectionConfig` subset of `AppConfig` in P7 (sensitivity / threshold / cooldown / min-duration). Audio device selection still uses P6 D-11 fallback chain ("Beyond" → `isDefault` → first). Training data still owned by client. AppConfig as a whole stays out of the driver until P8.

### E. DetectionRunner shape + lifecycle

- **D-17:** New driver-only files `driver/src/detection_runner.{hpp,cpp}`. `DetectionRunner` owns:
  1. A `std::thread` running `DetectionLoop()`.
  2. `std::unique_ptr<INoiseDetector>` constructed via `detection::createFFTDetector(sampleRate, 2048)` — sample rate fetched from AudioWorker's `IAudioCapture::getSampleRate()`. FFT size 2048 matches v1.5 client default (CONCERNS.md Performance Bottleneck #2 flagged but not addressed in P7 — defer).
  3. `std::unique_ptr<IStateMachine>` constructed via `core::createStateMachine(StateMachineConfig{...})` populated from the VRSettings-read snapshot.
  4. `std::atomic<std::shared_ptr<const DetectionConfig>> activeConfig_` snapshot (D-15).
  5. `std::atomic<bool> shutdown_`, `std::atomic<bool> paused_`, `std::condition_variable cv_`, `std::mutex mu_`.
  6. Reference (or weak ref) to AudioWorker's `SampleRing` and to the existing `CommandQueue`.
- **D-18:** `DetectionLoop()` per-iteration:
  - `cv_.wait_for(lk, kWakeTimeout, [&]{ return shutdown_ || paused_ || ring_->has_data(); })`
  - if shutdown → break
  - if paused → drain ring to discard buffer (no FFT, no state machine) → continue
  - drain ring → for each block: `detector->analyze(block, count)` → `stateMachine->update(result.confidence, dt)` → if state machine fires its trigger callback (rising-edge Triggered), `commandQueue_->push(TapCommand{})`
  - reload `activeConfig_` snapshot at top of each iteration; if changed, push new sensitivity/threshold/cooldown into detector + state machine
  - State machine cooldown is the natural CommandQueue rate-limit (Pitfall 12 D-14 — at most one TapCommand per cooldown window).

### F. Lifecycle ordering — Init / Cleanup (extends P6 D-13/D-14)

- **D-19:** `DeviceProvider::Init` order:
  1. `VR_INIT_SERVER_DRIVER_CONTEXT` (existing v1.5)
  2. `PatchGenericHmdBindings` (existing v1.5)
  3. `commandQueue_` construct (existing v1.5)
  4. `httpServer_->Start()` (existing v1.5; HTTP route registration extended for `/health` JSON shape)
  5. Read VRSettings: `enable_driver_audio`, `enable_driver_detection`, `detection_*` keys (D-13)
  6. If `enable_driver_audio` → construct `AudioWorker`, `Start()` (P6 D-14)
  7. **NEW:** if `enable_driver_detection` AND audioWorker_ alive → construct `DetectionRunner` wired to AudioWorker's ring + commandQueue_, `Start()`. If `enable_driver_detection=true` but audioWorker_ is null (audio failed to start, or `enable_driver_audio=false`), log warning `"MicMap: enable_driver_detection requires enable_driver_audio — skipping detection construction"` and skip — do NOT fail Init (P6 D-14 fail-soft semantics extended).
- **D-20:** `DeviceProvider::Cleanup` order (strict reverse construction):
  1. **NEW:** `detectionRunner_.reset()` FIRST. Destructor: signal `shutdown_`, `cv_.notify_all()`, join thread with 2s watchdog (matching P6 AudioWorker pattern). Detection thread exits cleanly before AudioWorker so the ring's producer stops feeding a dead consumer.
  2. `audioWorker_.reset()` (P6 D-13 step 1)
  3. `httpServer_->Stop()` + `commandQueue_.reset()` (existing v1.5)
  4. HMD-state reset (existing v1.5)
  5. `VR_CLEANUP_SERVER_DRIVER_CONTEXT` (existing v1.5)
- **D-21:** `DeviceProvider::RunFrame` extension for HMD standby (D-07): on `VREvent_TrackedDeviceDeactivated` for HMD index, additionally call `detectionRunner_->Pause()` if alive. On the next-frame HMD-handle-recreate path (state transitions back to Ready), call `detectionRunner_->Resume()`. Alternatively wire to `EnterStandby`/`LeaveStandby` driver-host callbacks (current implementations are no-op log lines at `device_provider.cpp:253-259` — clean splice point). Claude's discretion on which event source to use; `EnterStandby`/`LeaveStandby` is the more semantically-correct choice and avoids re-pausing on every HMD-deactivate/reactivate cycle that isn't actually a sleep.

### G. Pitfall enforcement — Pitfall 3 lint extended

- **D-22:** Extend P6's planned/shipped `cmake/AssertAudioWorkerNoVrApi.cmake` (or add a sibling `AssertDetectionRunnerNoVrApi.cmake`) to include `detection_runner.cpp`, `sample_ring.hpp`, and any new TUs added in P7 in the no-`vr::*`-API allowlist. SC2 of Phase 7 explicitly demands `grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost\|VRSettings'` returns matches only in `device_provider.cpp` + `manifest_registrar.cpp`. CI runs on every build.
- **D-23:** `DetectionRunner` does NOT register its own `IMMNotificationClient` or any other COM notifier. The audio-device-removed surface is owned by `WASAPIAudioCapture` (registered inside AudioWorker per P6 D-15). DetectionRunner only ever touches: SPSC ring (read), AppConfig snapshot (atomic load), state machine (own instance), noise detector (own instance), CommandQueue (push). UAF surface = ring lifetime tied to AudioWorker; D-20 ordering (DetectionRunner.reset() BEFORE AudioWorker.reset()) prevents ring read-after-free.
- **D-24:** Pitfall 13 (IMMNotificationClient UAF) hardening surface unchanged from P6 D-15/D-16 (alive-flag in AudioWorker). Full ComPtr migration of `DeviceNotificationClient` (CONCERNS.md flagged item) deferred to P10.

### H. UAT regimen on Bigscreen Beyond + Win11 Pro

- **D-25:** Mandatory UAT before phase-complete (extends P6 D-17 shape):
  1. **Flag-ON in-process trigger.** `enable_driver_audio=1` + `enable_driver_detection=1`. Boot SteamVR. Cover mic → dashboard toggles. `vrserver.txt` confirms TapCommand pushed from detection thread (log line `"MicMap detection: TapCommand pushed"` or equivalent); zero `POST /button` traffic in driver HTTP log (httpServer_ access log). SC1.
  2. **HMD wake/sleep ×2.** With (1) still active, sleep HMD, wake, repeat. Detection thread pauses+resumes cleanly (log line `"MicMap detection: paused"` / `"resumed"`); no leaked WASAPI/COM handles in Process Explorer; cover-mic still triggers after each resume. SC3.
  3. **50-cycle Init→500ms→Cleanup stress.** Drive via headless test (`tests/driver/device_provider_lifecycle_stress_test.cpp` — constructs/destroys DeviceProvider directly without SteamVR) OR a scripted SteamVR-restart loop; pick whichever is faster. Process Explorer handle count stable across 50 iterations; no `AUDCLNT_E_DEVICE_IN_USE` in vrserver.txt. SC4.
  4. **Settings propagation < 50ms.** Headless test calls `DetectionRunner::publish()` from a worker thread, measures `steady_clock::now()` to detection-thread-observed-config swap; assert < 50ms. SC5 / MIG-06.
  5. **Flag-OFF regression.** `enable_driver_audio=0` + `enable_driver_detection=0`. Boot SteamVR. Run `hmd_button_test.exe` against v1.5 trigger path (POST /button → CommandQueue → `/input/system/click`). Driver behavior byte-identical to P6 closeout baseline.
  6. **Coexistence (P10's primary risk surface, validated early).** Both client UI running with detection ON AND driver flags ON. Confirm `/health` returns `driver_detection_active=true`, client suppresses local trigger, single dashboard toggle per mic-cover (no double-tap). Then disable driver flag mid-session, confirm client resumes local trigger within ≤ 1 health-poll cycle.
- **D-26:** UAT artifacts: `vrserver.txt` excerpts + screenshots committed to `.planning/phases/07-driver-side-detection-thread/07-UAT.md`. Stress-test (3) is the new bar — D-17(3) in P6 was a single-cycle spot-check; SC4 explicitly demands 50.

### I. Merge strategy + flag default discipline

- **D-27:** Land on `main` branch with both flags default OFF (`enable_driver_audio=false` from P6, `enable_driver_detection=false` new). Rationale identical to P6 D-19: SC4 demands single-binary that runs identically to v1.5 when flags=0; flag-discoverable via `default.vrsettings` for testing; flag-OFF lets shipped users continue on P5/P6 baseline while team validates. P10 (cutover) flips both defaults ON in one commit.
- **D-28:** Document in PLAN.md that any `enable_driver_detection=true` deployment WITHOUT P8's IPC reshape means sensitivity/threshold/cooldown can ONLY be changed via editing `default.vrsettings` and restarting SteamVR. This is acceptable for P7 (developer-only flag); P8 lands the runtime `PUT /settings` path that satisfies MIG-06 end-to-end.

### Claude's Discretion

- Exact SPSC ring slot count (8 / 16 / 32) and per-slot frame count — pick whatever lines up with WASAPI shared-mode buffer cadence and keeps push branch-free.
- DetectionRunner `cv_.wait_for` timeout (25ms / 50ms / 100ms) — balance shutdown latency vs idle CPU.
- Whether `SampleRing` is template-on-T or hardcoded for `float`.
- Whether `DetectionConfig` snapshot uses C++20 `std::atomic<std::shared_ptr<>>` directly or `std::atomic_load`/`store` free functions on a member; pick by toolchain support (MSVC v19.30+ has the C++20 form).
- Detection thread log verbosity — pick what reads cleanly in vrserver.txt without flooding (use the same `MicMap detection:` prefix convention as P6's `MicMap audio:`).
- File layout — single `detection_runner.{hpp,cpp}` vs split runner/state/snapshot. Match P6's single-file precedent unless detector instantiation grows beyond ~300 LoC.
- Headless test layout — extend existing `tests/driver/audio_worker_lifecycle_headless.cpp` vs new `detection_runner_*_test.cpp`. Mirror the P6 test's RED-tolerant scaffold pattern.
- Whether to wire HMD pause to `EnterStandby`/`LeaveStandby` (semantically correct) vs `VREvent_TrackedDeviceDeactivated`/reactivate (already-handled in `RunFrame`). Recommend `EnterStandby` for clarity.
- Drop-count diagnostic logging cadence (per-drop / per-N-drops / per-cleanup-summary) — pick what surfaces the "ring overflowing under load" smell without flooding.

### Folded Todos

None — STATE.md "Pending Todos" was empty for Phase 7.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` §"Phase 7: Driver-Side Detection Thread" — goal, dependency on Phase 6, Success Criteria 1–5, research flags (STANDARD threading + NEEDS VALIDATION HMD sleep/wake), v1.5 SVR-05 invariant restated.
- `.planning/REQUIREMENTS.md` §"Driver-Resident Detection (MIG)" — MIG-02 (detection thread → CommandQueue, no `vr::*`), MIG-03 (HMD activation cycles, EnterStandby/LeaveStandby pause/resume), MIG-04 (50-cycle Init/Cleanup stress, reverse-order teardown), MIG-06 (atomic snapshot for settings, < 50ms propagation).
- `.planning/PROJECT.md` §"Current Milestone: v1.6 Feature Migration" + §"Key Decisions" — locked stack, "POST /button survives until P10" decision (rollback path during phased migration), trigger-path-collapse goal.
- `.planning/STATE.md` §"Current Position" + §"Blockers/Concerns" — Phase 6 spike outcome GO; Phase 7 unblocked; pending question on hmd_button_test.exe retirement (TEST-05 vs TEST-02 — addressed P10, not P7).

### Pitfall mitigations Phase 7 owns
- `.planning/research/PITFALLS.md` §"Pitfall 3: Audio thread (WASAPI render thread / capture thread) calls `UpdateBooleanComponent` directly" — hard rule, lifted from v1.5: only RunFrame calls OpenVR API. **D-22, D-23 enforce in P7 (lint extension + DetectionRunner discipline).**
- `.planning/research/PITFALLS.md` §"Pitfall 4: Driver lifecycle vs WASAPI device lifecycle" — reverse-order teardown, 2s watchdog. **D-19, D-20 implement Init/Cleanup ordering. D-25(3) drives the 50-cycle SC4 stress.**
- `.planning/research/PITFALLS.md` §"Pitfall 10: Migration boundary states — double-trigger and stale-callsite hazards" — "Single feature toggle at runtime" guidance. **D-09, D-10, D-11, D-12 implement the `driver_detection_active` /health field + client suppression handshake.**
- `.planning/research/PITFALLS.md` §"Pitfall 12: WASAPI capture buffer and FFT pacing inside RunFrame's 11ms budget" — drop-newest/drop-oldest queue policy, state-machine rising-edge throttle. **D-03 (drop-OLDEST), D-18 (state-machine cooldown is the natural CommandQueue rate-limit).**
- `.planning/research/PITFALLS.md` §"Pitfall 13: IMMNotificationClient callback runs after driver is unloaded" — alive-flag pattern. **D-23, D-24 inherit from P6 D-15/D-16 (notifier surface stays in AudioWorker; DetectionRunner has no notifier of its own).**
- `.planning/research/PITFALLS.md` §"Pitfall 14: OnDefaultDeviceChanged" — explicitly **deferred** in P7 (audio-device surface unchanged from P6 D-11). Read for awareness; P8+ owns device pinning and follow-the-default logic.
- `.planning/research/PITFALLS.md` §"Pitfall 15: Symbol bloat from `nlohmann/json` in three binaries" — **D-14 explicitly keeps json out of the driver in P7** (carries P5 D-15/D-16 + P6 D-02 deferral chain).
- `.planning/research/PITFALLS.md` §"Pitfall 11: v1.5 priors" — atomic config write helper, `VR_Init` reentry. P7 reuses `VRSettings()->GetBool/GetFloat/GetInt32` single-read pattern from P6 D-01; no new `VR_Init` calls.

### Migration shape and architecture
- `.planning/research/SUMMARY.md` §"Phase 3: Driver-Side Detection Thread" (research-numbered Phase 3 = roadmap Phase 7) — research-derived rationale for the in-process trigger collapse.
- `.planning/research/ARCHITECTURE.md` — driver vs client thread model target state; SPSC ring + DetectionRunner placement.
- `.planning/codebase/ARCHITECTURE.md` §"Detection to State Machine to Action" — current v1.5 client-side detection flow that P7 mirrors inside the driver.
- `.planning/codebase/STRUCTURE.md` — `driver/src/`, `src/audio/`, `src/detection/`, `src/core/` interface layouts.
- `.planning/codebase/STACK.md` — locked stack; no new deps in P7 (KissFFT already pulled via `micmap::core_runtime`).
- `.planning/codebase/CONCERNS.md` — Performance Bottleneck #2 (FFT-on-every-frame) flagged; **NOT addressed in P7** (preserves v1.5 client-default behavior; defer to a focused perf phase).

### Phase boundary inheritance
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` §"Implementation Decisions" — D-10/D-11 (driver links `micmap::core_runtime` PRIVATE), D-12 (no `MICMAP_DRIVER_BUILD` in shared lib), D-13 (no dllexport), D-15/D-16 (json + LIB-04 deferred to P8). All carry into P7.
- `.planning/phases/06-driver-side-audio-capture-spike/06-CONTEXT.md` §"Implementation Decisions" — D-01 VRSettings flag pattern (P7 D-13 extends to detection_* keys), D-04/D-05 AudioWorker apartment trick (P7 keeps unchanged), D-13/D-14 reverse-order teardown (P7 D-19/D-20 extends), D-15/D-16 IMMNotificationClient alive-flag (P7 inherits via AudioWorker), D-19/D-20 main-branch flag-default-OFF strategy (P7 D-27 mirrors).

### v1.5 invariants carried forward
- `.planning/milestones/v1.5-ROADMAP.md` SVR-05 — HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. **P7 adds DetectionRunner as a NEW PRODUCER to the same CommandQueue; the boundary is unchanged.** No new `vr::*` callers introduced.
- `driver/src/device_provider.cpp` — current Init / Cleanup / RunFrame source. P7 modifies Init (steps 5–7 added), Cleanup (step 1 added), and RunFrame (HMD-deactivate / standby splice for `Pause()`/`Resume()`).
- `driver/src/command_queue.hpp` — existing `kMaxDepth=8` bounded queue. P7 adds the new audio-detection producer; HTTP-thread producer continues unchanged (still drives v1.5 `POST /button` rollback path until P10).

### In-tree code touched by P7
- `driver/src/audio_worker.{hpp,cpp}` (existing P6 — modified) — `setAudioCallback` lambda rewired: keep `weak_ptr<State>` UAF guard, replace RMS-only logic with ring-push + `cv_.notify_one()`. RMS-log block guarded by `MICMAP_DEBUG_RMS_LOG` define.
- `driver/src/detection_runner.{hpp,cpp}` (NEW) — class shape per D-17, loop per D-18, lifecycle per D-19/D-20.
- `driver/src/sample_ring.hpp` (NEW) — header-only SPSC bounded ring per D-01/D-02/D-03/D-04.
- `driver/src/device_provider.{hpp,cpp}` — add `std::unique_ptr<DetectionRunner> detectionRunner_` member + `bool driverDetectionEnabled_` + `DetectionConfig` defaults; extend Init/Cleanup/RunFrame per D-19/D-20/D-21.
- `driver/src/http_server.cpp` — extend `/health` JSON response to include `driver_detection_active` field per D-09. Server must read this from DeviceProvider state — Claude's discretion on the wiring (passed-in callback, shared atomic, etc.).
- `driver/CMakeLists.txt` — add new TUs `detection_runner.cpp` (and `sample_ring.hpp` is header-only). No new link dependencies (`INoiseDetector` + `IStateMachine` already pulled via `micmap::core_runtime` from P5 D-10).
- `driver/resources/settings/default.vrsettings` — add `enable_driver_detection`, `detection_sensitivity`, `detection_threshold`, `detection_cooldown_ms`, `detection_min_duration_ms` to existing `driver_micmap` section.
- `cmake/AssertAudioWorkerNoVrApi.cmake` (P6) — extend allowlist OR add `cmake/AssertDetectionRunnerNoVrApi.cmake` sibling. Either way, SC2 grep must pass on `detection_runner.cpp` + `sample_ring.hpp`.
- `apps/micmap/main.cpp` (or wherever the trigger site lives) — small client diff: poll `/health.driver_detection_active`, suppress local trigger when true. **Client-side detection logic itself stays in P7 unchanged** (P10 deletes it).
- `tests/driver/detection_settings_propagation_test.cpp` (NEW) — MIG-06 50ms-propagation headless test per D-25(4).
- `tests/driver/device_provider_lifecycle_stress_test.cpp` (NEW or extension) — 50-cycle Init/Cleanup stress per D-25(3).

### Reusable shared-lib factories (already in `micmap::core_runtime` from P5)
- `src/detection/include/micmap/detection/noise_detector.hpp` — `INoiseDetector` interface + `createFFTDetector(uint32_t sampleRate, size_t fftSize)` at `src/detection/src/noise_detector.cpp:745`. P7 instantiates with sample rate from `IAudioCapture::getSampleRate()` and `fftSize=2048`.
- `src/core/include/micmap/core/state_machine.hpp` — `IStateMachine` interface + `createStateMachine(const StateMachineConfig&)` at `src/core/src/state_machine.cpp:160`. P7 instantiates with VRSettings-derived config.
- `src/audio/include/micmap/audio/audio_capture.hpp` — `IAudioCapture::setAudioCallback`, `IAudioCapture::getSampleRate` (line 93), `IAudioCapture::getCurrentDevice` (line 81). All unchanged by P7.
- `src/core/include/micmap/core/config_manager.hpp` — `AppConfig` + `DetectionConfig` schema. P7 reads only the `DetectionConfig` subset and reconstructs it from VRSettings (no `ConfigManager` instance pulled into driver — preserves P5 D-15 deferral).

### Sister-project reference (existence proof, not code-copy)
- `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — already informed P6 AudioWorker shape. Has nothing to add for P7's detection thread or SPSC ring; do not re-read for code patterns.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/detection/src/noise_detector.cpp:745` — `createFFTDetector(sampleRate, fftSize)` factory in shared lib. **DetectionRunner instantiates this on its own thread** (no thread-affinity concerns — the FFT detector is stateless across calls except for its own internal buffers).
- `src/core/src/state_machine.cpp:160` — `createStateMachine(StateMachineConfig)` factory in shared lib. **DetectionRunner owns one instance**, drives it from the detection loop. Existing trigger callback contract (rising-edge Triggered) is the natural CommandQueue rate-limiter (Pitfall 12 D-14).
- `src/audio/include/micmap/audio/audio_capture.hpp:93` — `IAudioCapture::getSampleRate()` provides the sample rate DetectionRunner needs to construct `createFFTDetector`. Read once after AudioWorker `Start()` reports success.
- `driver/src/audio_worker.cpp:241-263` — existing `setAudioCallback` lambda in `RunWorker`. **P7 rewires this** to push frames into the ring (drop-oldest), `cv_.notify_one()` the DetectionRunner, and (debug-only) emit RMS log lines.
- `driver/src/audio_worker.{hpp,cpp}` — `weak_ptr<State>` + `alive` flag pattern (D-15/D-16 from P6). **P7 reuses unchanged** — DetectionRunner does NOT add its own notifier surface (D-23).
- `driver/src/command_queue.hpp` — `kMaxDepth=8` bounded queue, existing v1.5 single-producer (HTTP) → single-consumer (RunFrame) shape. **P7 adds a second producer (DetectionRunner)**; queue's existing locking handles concurrent push (verify in PLAN — current impl uses `std::lock_guard` per Pitfall 3 §"How to avoid").
- `driver/src/device_provider.cpp:80-110` — P6's flag-read + AudioWorker construction block. **P7 extends in-place** — adds new VRSettings reads + DetectionRunner construction (D-19).
- `driver/src/device_provider.cpp:113-150` — P6's reverse-order Cleanup. **P7 prepends `detectionRunner_.reset()` as the new step 1** (D-20).
- `driver/src/device_provider.cpp:253-259` — `EnterStandby` / `LeaveStandby` no-op log lines. **P7 splices `detectionRunner_->Pause()` / `Resume()` here** (D-21 — recommended over `VREvent_TrackedDeviceDeactivated`).
- `driver/src/http_server.cpp` — existing `/health` route handler. **P7 extends response JSON** to include `driver_detection_active` boolean (D-09); needs read access to DeviceProvider state.
- `driver/resources/settings/default.vrsettings` — existing `driver_micmap` section already carries `enable`, `http_port`, `http_host`, `enable_driver_audio` (P6). **P7 appends 5 new keys** (D-13).

### Established Patterns
- **VRSettings single-read at Init.** P6 D-01 introduced this for `enable_driver_audio`; P7 D-13 extends with same pattern for 5 detection keys. Single read, no hot-reload — runtime mutation goes through `publish()` (D-15). Default-on-`UnsetSettingHasNoDefault` discipline mirrored from `device_provider.cpp:85-95`.
- **Reverse-order Cleanup with 2s watchdog.** P6 AudioWorker's `Stop()` (`audio_worker.cpp:90-122`) is the precedent: signal shutdown CV, poll `thread_finished_` for 2s, last-resort detach. **DetectionRunner replicates this pattern verbatim** (D-20).
- **`weak_ptr<State>` + `alive` UAF guard for cross-thread callbacks.** P6 D-15/D-16 (`audio_worker.cpp:241-263`). DetectionRunner does NOT add its own notifier (D-23) so this pattern is not duplicated; the only cross-thread surface is the SampleRing (lock-free atomic head/tail) and the CommandQueue (already-locked).
- **Driver-only files under `driver/src/`.** P5 D-10/D-11 enforced; P6 followed with `audio_worker.{hpp,cpp}`; **P7 follows with `detection_runner.{hpp,cpp}` + `sample_ring.hpp`**. No shared-lib reach.
- **No `vr::*` API call from non-RunFrame threads.** v1.5 SVR-05 invariant; P6 D-07 followed; P7 D-22/D-23 follows + extends CMake lint.
- **CMake lint as the structural guardrail.** P5 introduced `cmake/AssertNoOpenVRInCore.cmake`; P6 introduced `cmake/AssertAudioWorkerNoVrApi.cmake` (per its plans). **P7 extends or sibling-adds to cover detection TUs** (D-22).
- **HMD-handle invalidation pattern** (`device_provider.cpp:166-178`) — RunFrame handles `VREvent_TrackedDeviceDeactivated`. **P7's HMD-pause splice prefers `EnterStandby`/`LeaveStandby` over this event** (D-21) for clearer semantics.

### Integration Points
- `driver/src/device_provider.hpp` — add `std::unique_ptr<DetectionRunner> detectionRunner_`, `bool driverDetectionEnabled_`, and (Claude's discretion) cached `DetectionConfig` defaults struct. Existing class shape preserved.
- `driver/src/audio_worker.hpp` — add a method/accessor for DetectionRunner to attach (e.g., `SetSampleSink(std::function<void(const float*, size_t)>)`) OR the AudioWorker holds a back-reference to the ring. Claude's discretion on the wiring direction; "AudioWorker owns ring, DetectionRunner reads it" is the simplest shape.
- `driver/src/http_server.cpp` — `/health` handler needs DeviceProvider state read access for `driver_detection_active` field. Pattern options: pass an `std::function<bool()>` getter at construction; pass a shared atomic flag; expand the existing CommandQueue-style indirection. Pick whichever stays consistent with v1.5 HTTP-server design.
- `driver/CMakeLists.txt` — register `detection_runner.cpp` source + `sample_ring.hpp` header. No new link deps.
- `apps/micmap/main.cpp` — client trigger site adds a `/health.driver_detection_active` check before calling `IDriverClient::tap()`. Smallest possible diff; deleted entirely in P10.
- `tests/driver/` — new headless tests for SC4 (50-cycle stress) and SC5 / MIG-06 (50ms snapshot propagation). Reuse P6's RED-tolerant CMake-EXISTS scaffold pattern from `06-01-PLAN.md`.

</code_context>

<specifics>
## Specific Ideas

- **"Detection thread is a new producer to the same CommandQueue — the boundary is unchanged."** This is the single load-bearing sentence for P7. Every other decision flows from it. If P7 grows a temptation to call `vr::*` from detection (or from the audio callback), stop and re-read Pitfall 3.
- **AudioWorker stays in charge of WASAPI lifecycle.** Don't move WASAPI device handles, COM init, or `IMMNotificationClient` registration into DetectionRunner — that's how Pitfall 4 + Pitfall 13 surface area doubles. AudioWorker = device. DetectionRunner = math + state. SampleRing = bridge.
- **Drop-OLDEST is the right ring policy for this domain.** Drop-newest would lose the *most recent* acoustic state during a stall — exactly the data detection needs. Drop-oldest preserves the recent window. The audio thread MUST never block (Pitfall 12 hard rule).
- **`/health.driver_detection_active` is the migration handshake, not an end-state API.** P10 deletes it along with `POST /button`, `IDriverClient::tap()`, and the client-side detection body. Don't generalize the field into a richer contract — it's transient scaffolding.
- **MIG-06 propagation is validated by mechanism in P7, not by HTTP path.** P7 ships the `publish()` API + atomic-snapshot reader + headless < 50ms test. P8 is what makes the path end-to-end through `PUT /settings`. Don't preempt P8 just to "make MIG-06 demonstrable through HTTP" — that's the IPC reshape phase's job.
- **Defaults mirror v1.5 client-side `config.json` defaults.** Document the value-mirror in PLAN.md so P8 cutover (when driver becomes config.json reader) doesn't drift the defaults silently.
- **50-cycle stress test is the new SC bar.** P6 D-17(3) was a single-cycle spot-check. P7 SC4 demands 50 cycles with zero leaked handles. Build the headless lifecycle harness — don't try to drive 50 SteamVR-restarts manually.
- **Pause-detection-only-on-standby beats pause-everything.** Audio device handles are expensive (Pitfall 4). Closing/reopening WASAPI on every HMD wake/sleep cycles the most fragile lifecycle surface in the codebase. Detection paused while audio keeps producing means the ring drops oldest until resume — no harm.

</specifics>

<deferred>
## Deferred Ideas

- **`PUT /settings` HTTP endpoint + driver-as-`config.json`-writer** — **Phase 8 (IPC Contract Reshape)**, IPC-04 + IPC-05. P7 ships the publish-and-snapshot mechanism; P8 wires the HTTP path that calls publish() and persists to disk.
- **`GET /devices`, `GET /telemetry/level`, `GET /state` endpoints** — **Phase 8** (HEALTH + IPC clusters).
- **Logger sink injection (`DriverLogSink` + `FileLogSink`)** — **Phase 8** (LIB-04). DetectionRunner uses raw `DriverLog` in P7, same as P6 AudioWorker.
- **Training migration (`POST /training/*` endpoints, `training_data.bin` ownership transfer)** — **Phase 9** (TRAIN cluster). DetectionRunner in P7 only consumes existing training data via the shared-lib `INoiseDetector` (which loads from the existing client-written `training_data.bin` path). Driver doesn't write training data in P7.
- **`POST /button` deletion + `IDriverClient::tap()` removal** — **Phase 10** (MIG-05). v1.5 trigger path stays alive in parallel during P7 as the rollback.
- **`enable_driver_detection` default flip ON** — **Phase 10** (cutover commit). P7 ships flag default OFF.
- **Client-side detection deletion** — **Phase 10**. P7 keeps client-side detection running (with /health-driven trigger suppression) so client UI's confidence/RMS visualization keeps working until the new IPC surface (`GET /telemetry/level`) lands.
- **`OnDefaultDeviceChanged` follow-the-default behavior + device pinning** — **Phase 8 (IPC reshape, IPC-03 GET /devices)**. Pitfall 14 mitigation. P7 inherits P6 D-11's "Beyond → isDefault → first" fallback.
- **`DeviceNotificationClient` ComPtr migration** — **Phase 10**. CONCERNS.md flagged manual `InterlockedIncrement`/`Decrement`; spike-grade alive-flag (P6 D-15/D-16) carries through P7.
- **FFT-on-every-frame perf cost** — CONCERNS.md Performance Bottleneck #2. **NOT addressed in P7** — preserves v1.5 client-default `fftSize=2048` behavior. Targeted perf phase or backlog candidate.
- **Tray-icon state glyphs (HEALTH-08), FAIL-01..05 graceful failure UX, INST-09 installer co-versioning, TEST-01..03/05** — **Phase 10**.
- **`hmd_button_test.exe` retirement** — TEST-05 keeps it as developer tool; STATE.md flagged the TEST-05 vs TEST-02 overlap as a P10 question, not P7. P7 D-25(5) actively uses `hmd_button_test.exe` as the flag-OFF regression harness.
- **cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728)** — already deferred to a Phase 8 prerequisite plan per P5 D-15. P7 unchanged.
- **Detection-accuracy work in noisy environments (DET-01/02)** — out of v1.6 scope per PROJECT.md.

</deferred>

---

*Phase: 07-driver-side-detection-thread*
*Context gathered: 2026-05-03*
