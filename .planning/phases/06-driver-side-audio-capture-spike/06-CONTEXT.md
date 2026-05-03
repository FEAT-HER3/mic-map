# Phase 6: Driver-Side Audio Capture Spike - Context

**Gathered:** 2026-05-02
**Status:** Ready for planning

<domain>
## Phase Boundary

Prove WASAPI capture is feasible inside `vrserver.exe`'s DLL host on real Bigscreen Beyond + Win11 Pro hardware. Behind a runtime flag `enableDriverAudio` (default OFF). Driver constructs an audio worker thread in `DeviceProvider::Init`, the worker thread owns `CoInitializeEx(MTA)`, opens the system default WASAPI capture endpoint, and logs the first ~1s of RMS readings to `vrserver.txt`. Worker is joined cleanly in `Cleanup`.

**In scope:** runtime flag wiring (read-once in Init via VRSettings), driver-side `AudioWorker` wrapper that owns the thread + `shared_ptr<State>` alive-flag, reuse of `IAudioCapture` from `micmap::core_runtime` (constructor invoked on the worker thread so CoInit lands in MTA), eMultimedia default-capture device selection, `RPC_E_CHANGED_MODE` distinct log path, reverse-order teardown (worker → existing v1.5 HTTP/CommandQueue/HMD-handle reset), real-hardware UAT on Bigscreen Beyond.

**Out of scope:** detection thread / FFT / state machine inside driver (P7), SampleRing + lock-free sample plumbing (P7), trigger-path collapse / `POST /button` deletion (P10), driver-as-config-reader / `PUT /settings` / device pinning (P8), nlohmann/json centralization (P8), logger sink wiring `LIB-04` (P8), `OnDefaultDeviceChanged` device-follow logic (Pitfall 14, P7+), 50-cycle Init/Cleanup stress test (P7 SC4), training migration (P9).

The spike validates the **highest-risk unknown** of the milestone before any detection/IPC work begins. If WASAPI fails inside the vrserver DLL host, escalate before Phase 7.

</domain>

<decisions>
## Implementation Decisions

### Flag plumbing
- **D-01:** Flag lives in the driver's existing `default.vrsettings` at `driver/resources/settings/default.vrsettings` under the existing `driver_micmap` section as a new bool key `enable_driver_audio` (default `false`). Read once in `DeviceProvider::Init` via `vr::VRSettings()->GetBool("driver_micmap", "enable_driver_audio", &err)`. Stored on `DeviceProvider` as `bool driverAudioEnabled_`. Single read at Init — no hot-reload in P6.
- **D-02:** Reading the flag does **not** drag `ConfigManager` / `nlohmann::json` / `AppConfig` into the driver in P6. That wiring belongs to Phase 8 (IPC reshape, when driver becomes the sole `config.json` writer). Using `VRSettings()` is driver-native, requires no shared-lib changes, and keeps Pitfall 15 (json bloat) deferred per P5 D-16.
- **D-03:** Flag-OFF code path: when `driverAudioEnabled_` is false at Init time, the audio worker is **never constructed**. No `std::thread`, no COM, no WASAPI calls. Driver Init/Cleanup ordering and behavior is byte-identical to shipped Phase 5 driver. SC4 satisfied by construction.

### Code reuse — existing `IAudioCapture` runs on the worker thread
- **D-04:** Reuse the existing `IAudioCapture` factory (`createAudioCapture()` in `src/audio/`) inherited via `micmap::core_runtime` from Phase 5 D-10. **No refactor of `WASAPIAudioCapture`** — the existing constructor already calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` (`audio_capture.cpp:196`); we satisfy Pitfall 1 by ensuring the constructor itself runs on the audio worker thread, not on `DeviceProvider::Init`.
- **D-05:** New driver-only file: `driver/src/audio_worker.{hpp,cpp}`. `AudioWorker` owns:
  1. A `std::thread` whose entry function constructs `IAudioCapture`, calls `start()`, drains the audio callback, and (on shutdown signal) calls `stop()` + lets the `IAudioCapture` destructor run on the worker thread (so `CoUninitialize` lands on the same apartment as `CoInitializeEx`).
  2. A `std::shared_ptr<State>` where `State` carries `std::atomic<bool> alive` (default true) and the captured-frame counters. The audio callback (and any IMMNotificationClient-driven path that surfaces through the existing capture's `onDeviceRemoved` lambda) captures the shared_ptr and bails when `alive` is false (Pitfall 13 mitigation).
  3. A `std::condition_variable` + `std::atomic<bool> shutdown_` for clean stop signalling.
- **D-06:** `RPC_E_CHANGED_MODE` (`0x80010106`) is currently swallowed by `WASAPIAudioCapture` — `comInitialized_` is set on `SUCCEEDED(hr) || hr == S_FALSE`, treating `RPC_E_CHANGED_MODE` as failure of `comInitialized_`. P6 does not modify this in shared lib. Instead, the driver-side `AudioWorker` thread entry function calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` itself **before** constructing `IAudioCapture`, captures the HRESULT, and if it sees `RPC_E_CHANGED_MODE` logs `"MicMap: audio worker thread already in another COM apartment (RPC_E_CHANGED_MODE) — bailing out"` to `DriverLog`, sets `last_error`, and exits the thread. This keeps the shared-lib behavior unchanged (P5 byte-identical guarantee preserved at the shared-lib layer) while satisfying SC2 in driver-only code.
  - Note: the WASAPI capture's *own* `CoInitializeEx` call from its constructor will then return `S_FALSE` (already initialized same apartment) which the existing code accepts. No double-init failure.
- **D-07:** No `vr::*` API call is made from the worker thread. The worker only writes to `DriverLog` (which is thread-safe per OpenVR convention) and to its own `State` atomics. SC3 + Pitfall 3 + v1.5 SVR-05 invariant unchanged. P6 introduces no new `CommandQueue` producer (no `TapCommand` push from audio); the existing v1.5 HTTP-thread → CommandQueue → RunFrame trigger path remains the only OpenVR write path.

### Spike scope — RMS-log-only
- **D-08:** Audio callback computes RMS over each delivered buffer and logs the first **~1 second** of RMS readings (the SC1 contract). The "1 second" budget is interpreted as: log the first **N** RMS values where `N * buffer_period_ms ≈ 1000ms` — at WASAPI's typical 10ms shared-mode period that's ~100 lines. After the budget is met, the callback continues to drain frames (so WASAPI buffer never overflows) but skips DriverLog writes. This avoids flooding `vrserver.txt` while still validating that capture stays alive for the lifetime of the SteamVR session.
- **D-09:** **No `SampleRing`, no FFT, no state machine, no `TapCommand` push** in P6. Those are P7 (Detection Thread). Frames after the RMS-budget window are dropped on the floor inside the audio callback. P6 deliberately does not preempt P7's data-flow design.
- **D-10:** The audio callback runs on the WASAPI capture thread (the existing `WASAPIAudioCapture` runs its own internal capture thread per `audio_capture.cpp` event loop). The `AudioWorker` thread itself just owns the lifecycle (Init / start → wait-on-shutdown → stop / Cleanup). This matches the v1.5 client architecture and avoids re-architecting the capture class.

### Device selection
- **D-11:** P6 opens the system default capture device (`eMultimedia` role) — whatever `IAudioCapture::startWithDefaultDevice()` (or its equivalent in the existing API) selects. **No reading from `config.json`**, no device pinning, no enumeration UI. Pitfall 14 (`OnDefaultDeviceChanged` follow-the-default behavior) is explicitly deferred — the existing `WASAPIAudioCapture` already returns `S_OK` no-op on `OnDefaultDeviceChanged` (`audio_capture.cpp:166-169`), which is fine for a single-cycle spike.
- **D-12:** Device pinning (read `config.json.audio.deviceId` at Init, fall back to default) is a Phase 8 concern — the driver becomes the sole `config.json` reader/writer in P8, and that's the natural place to wire pinning. Adding it in P6 would force ConfigManager + nlohmann/json into the driver prematurely, contradicting D-02.

### Lifecycle / Cleanup ordering (Pitfall 4)
- **D-13:** New `Cleanup` order in `DeviceProvider::Cleanup` (extending the v1.5 sequence at `driver/src/device_provider.cpp:81-107`):
  1. **`audioWorker_.reset()` first** — `AudioWorker` destructor sets `state->alive = false`, signals shutdown, joins the worker thread. Worker thread before exiting calls `IAudioCapture::stop()` (which already unregisters `IMMNotificationClient`, releases `ComPtr`s, joins the WASAPI capture thread per `audio_capture.cpp:222-230`), lets the `IAudioCapture` destructor run on the worker thread, then returns. Bounded by a 2s watchdog (matches v1.5 `VREvent_Quit` shutdown watchdog precedent — Pitfall 4 "How to avoid").
  2. Existing v1.5 sequence: `httpServer_->Stop()`, `commandQueue_.reset()`, HMD-handle reset, `VR_CLEANUP_SERVER_DRIVER_CONTEXT()`.
- **D-14:** `Init` order is the construction-mirror of `Cleanup`: bindings patch (existing) → CommandQueue → HttpServer (existing) → **then if flag set, construct `AudioWorker`**. Audio worker constructed last so any audio failure does not corrupt the existing v1.5 trigger path.

### `IMMNotificationClient` alive-flag (Pitfall 13 / SC5)
- **D-15:** SC5 requires the notification client be registered on the audio worker thread. The existing `WASAPIAudioCapture` constructor already does this register call inside `CoCreateInstance(MMDeviceEnumerator)` + `RegisterEndpointNotificationCallback` (`audio_capture.cpp:217`). Because D-04 puts the `WASAPIAudioCapture` constructor on the worker thread, register-on-worker is satisfied automatically. Unregister happens in the destructor (`audio_capture.cpp:225-229`), which D-13 ensures also runs on the worker thread.
- **D-16:** The Pitfall 13 alive-flag mitigation is added in driver-only code (`AudioWorker::State`). The existing `DeviceNotificationClient` lambda inside `WASAPIAudioCapture` captures the audio capture's `this` and forwards to `onDeviceRemoved` — the alive-flag wrap is on the AudioWorker side: AudioWorker installs an `onDeviceRemoved` callback into the capture that captures `weak_ptr<State>` + checks `alive` before doing anything. Spike-grade mitigation; full Pitfall 13 hardening (CONCERNS.md item — manual `InterlockedIncrement`/`Decrement` → `ComPtr`) deferred to P7 where the notification client becomes load-bearing.

### Real-hardware UAT regimen (single SC1+SC2+SC4+SC5 cycle, multi-cycle deferred)
- **D-17:** Mandatory UAT on Bigscreen Beyond + Win11 Pro rig before phase-complete:
  1. **Flag-ON capture run.** `enable_driver_audio = true` in `default.vrsettings`. Boot SteamVR. Confirm `vrserver.txt` shows: audio worker thread constructed, `CoInitializeEx` outcome (S_OK or S_FALSE — both pass; RPC_E_CHANGED_MODE is the bail-out path), WASAPI device opened, ~100 RMS lines covering the first ~1s, capture stays alive for ≥30s. SC1 satisfied.
  2. **HMD wake/sleep × 2.** With the run from (1) still active, sleep the HMD, wake it, repeat once. Audio worker survives both cycles; no leaked handles in Process Explorer; no crash. (P7 will tighten this to the full HMD-reactivation handshake in `RunFrame`.)
  3. **SteamVR-restart-without-quit single cycle.** With the run from (1) still active, "Restart SteamVR" via the SteamVR UI (does not quit `vrserver.exe`). Driver `Cleanup` then `Init` runs in-process. Confirm second Init starts a fresh audio worker + WASAPI session (no `AUDCLNT_E_DEVICE_IN_USE`), no leaked handle in Process Explorer. SC2/SC5 lifecycle exit-criterion. **Single cycle only** — 50-cycle stress test is P7 SC4.
  4. **Flag-OFF regression.** Set `enable_driver_audio = false`. Boot SteamVR. Run `hmd_button_test.exe` against the v1.5 trigger path (POST /button → CommandQueue → `/input/system/click`) — confirm dashboard toggles. Driver behavior is byte-identical to Phase 5 v1.5 baseline. SC4 satisfied.
- **D-18:** UAT artifacts: copy of `vrserver.txt` excerpt covering the flag-ON 1s RMS window committed to `.planning/phases/06-driver-side-audio-capture-spike/06-UAT.md`. Process Explorer screenshot for D-17(3) device handle check optional (text observation acceptable for spike). Cycle counts deliberately small.

### Merge strategy
- **D-19:** Land on `main` branch with flag default OFF. Reasons: (a) SC4 explicitly demands a single binary that runs identically to v1.5 when flag=0 — a branch-only spike does not satisfy SC4 because main never sees the toggle; (b) bey-closer-t1 was branch-only because it was an external reference experiment, MicMap is the production code; (c) flag-OFF lets shipped users continue on Phase 5 behavior while the team validates; (d) flag-discoverable via `default.vrsettings` for follow-up debugging.
- **D-20:** Phase 7 will flip default ON only when its own SC are met. P6 ships flag default OFF and stays that way until P7 closes.

### Macro/ODR hygiene carried from Phase 5
- **D-21:** No `#ifdef MICMAP_DRIVER_BUILD` (or any host-switching macro) introduced in `src/{audio,detection,core,common}/` during P6 (P5 D-12 invariant). The new wiring lives entirely in `driver/src/audio_worker.{hpp,cpp}` + the existing `device_provider.cpp` modifications. Shared lib unchanged.
- **D-22:** No `__declspec(dllexport)` / `__declspec(dllimport)` introduced (P5 D-13). `dumpbin /exports driver_micmap.dll` must continue to show only `HmdDriverFactory` (Phase 5 SC3 carryover).

### Claude's Discretion
- Exact buffer-period/RMS-line accounting (D-08): the "100 lines" target is approximate; the implementation can pick a sample-count threshold or a steady-clock cutoff, whichever reads more naturally.
- DriverLog line format for RMS values (`"MicMap: rms=%f dB"` vs `"MicMap audio: %d ms RMS=%f"` etc.) — pick whatever is least ambiguous for `vrserver.txt` grepping.
- Whether the 2s Cleanup watchdog (D-13) uses a dedicated `std::condition_variable::wait_for` or detaches the worker as last-resort. Match v1.5 watchdog precedent.
- File layout under `driver/src/` for the new `AudioWorker` — single hpp/cpp pair vs split state struct. Follow the existing v1.5 driver convention.
- Whether to log the resolved device's friendly name + sample rate at Init. Useful for UAT D-17 evidence; doesn't affect SC.

### Folded Todos
None — STATE.md "Pending Todos" was empty for Phase 6.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase scope and requirements
- `.planning/ROADMAP.md` §"Phase 6: Driver-Side Audio Capture Spike" — goal, dependencies (Phase 5), Success Criteria 1–5, research flag (NEEDS VALIDATION), and the load-bearing v1.5 SVR-05 invariant.
- `.planning/REQUIREMENTS.md` §"Driver-Resident Detection (MIG)" — MIG-01 (audio worker thread + CoInit + WASAPI capture device + SPSC ring). P6 owns MIG-01 partial: capture-only, no SPSC ring (ring is P7).
- `.planning/PROJECT.md` §"Current Milestone: v1.6 Feature Migration" — milestone framing, locked stack, sister-project bey-closer-t1 reference and "WASAPI in vrserver DLL host" feasibility unknown.
- `.planning/STATE.md` §"Blockers/Concerns" — Phase 6 spike outcome dependency: if WASAPI fails in DLL context, escalate before Phase 7.

### Pitfall mitigations Phase 6 owns
- `.planning/research/PITFALLS.md` §"Pitfall 1: COM apartment incompatibility between vrserver, WASAPI, and the driver DLL" — mandates dedicated audio worker thread that calls CoInitializeEx itself; treat `RPC_E_CHANGED_MODE` distinctly. **D-04, D-05, D-06 implement this.**
- `.planning/research/PITFALLS.md` §"Pitfall 3: Audio thread calls UpdateBooleanComponent directly" — hard rule: only RunFrame calls OpenVR API; audio thread enqueues to CommandQueue. **D-07 enforces no `vr::*` from worker.** P6 doesn't even push to CommandQueue — that's P7.
- `.planning/research/PITFALLS.md` §"Pitfall 4: Driver lifecycle vs WASAPI device lifecycle — leaked capture session on Cleanup/Init cycles" — reverse-order teardown, IMMNotificationClient unregister before COM Release, 2s shutdown watchdog. **D-13, D-14 implement this.** Note: P7 owns the 50-cycle stress test (P7 SC4); P6 D-17(3) is a single-cycle spot-check.
- `.planning/research/PITFALLS.md` §"Pitfall 13: IMMNotificationClient callback runs after driver is unloaded → process crash" — `shared_ptr<State>` + `atomic<bool> alive`. **D-15, D-16 implement this in driver-only code.**
- `.planning/research/PITFALLS.md` §"Pitfall 14: OnDefaultDeviceChanged makes the default mic move under the driver's feet" — explicitly **deferred** in P6 (D-11/D-12 use eMultimedia default capture, no follow-the-default logic). Read for awareness; P7+ owns the device-pinning UI/IPC work.
- `.planning/research/PITFALLS.md` §"Pitfall 11: v1.5 priors — VR_Init reentry, IsApplicationInstalled poll guard, atomic config write — recurring with new shapes" — context for why `VRSettings()->GetBool` (D-01) is a safe single-read pattern.

### Migration shape and architecture
- `.planning/research/SUMMARY.md` §"Phase 2: Driver-Side Audio Capture Spike" (research-numbered Phase 2 = roadmap Phase 6) — research-derived rationale for spike-first ordering.
- `.planning/research/SUMMARY.md` §"Critical Pitfalls" 1, 2, 4 — top three Phase 6 risks summarized.
- `.planning/research/ARCHITECTURE.md` — driver vs client thread model target state; audio worker thread placement.
- `.planning/codebase/STRUCTURE.md` — `src/audio/` interface layout (`include/micmap/audio/audio_capture.hpp`), `driver/src/` file conventions.
- `.planning/codebase/STACK.md` — locked stack; no new deps in P6.
- `.planning/codebase/CONCERNS.md` — flagged: manual InterlockedIncrement/Decrement on `DeviceNotificationClient` (audio_capture.cpp ~119-133). Pitfall 13 hardening item; spike-grade alive-flag in P6, full ComPtr migration deferred.

### Phase 5 boundary inheritance
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` §"Implementation Decisions" — D-04 (configure-time guard), D-10/D-11 (driver links `micmap::core_runtime` PRIVATE, link-only), D-12 (no `MICMAP_DRIVER_BUILD` in shared lib), D-13 (no dllexport), D-15/D-16 (json + LIB-04 deferred to P8). **All carry forward into P6.**

### v1.5 invariants carried forward
- `.planning/milestones/v1.5-ROADMAP.md` SVR-05 — HTTP-thread → CommandQueue → RunFrame is the only path that touches OpenVR API. P6 does not introduce a new producer to CommandQueue (P7 does); P6 absolutely does not call `vr::*` from audio worker.
- `driver/src/device_provider.cpp` — current Init/Cleanup/RunFrame source. P6 modifies Init (read flag, optionally construct AudioWorker last) and Cleanup (reset AudioWorker first). RunFrame untouched.
- `driver/src/command_queue.hpp` — existing kMaxDepth=8 bounded queue. Read-only context for P6; P7 adds the new audio→CommandQueue producer path.

### In-tree code touched
- `src/audio/src/audio_capture.cpp` — existing `WASAPIAudioCapture` ctor at :186-220 calls `CoInitializeEx(MTA)` and registers `IMMNotificationClient`. **D-04 reuses this unchanged**, but ensures the ctor runs on the audio worker thread.
- `src/audio/include/micmap/audio/audio_capture.hpp` — existing `IAudioCapture` interface + factory. Read-only inheritance via `micmap::core_runtime`.
- `driver/resources/settings/default.vrsettings` — existing `driver_micmap` section (with `enable`, `http_port`, `http_host`). **D-01 adds `enable_driver_audio: false` here.**

### Sister-project reference
- `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — WASAPI + audio thread + SteamVR driver coexistence validated once externally. Documents the "audio thread inside DLL host" pattern P6 is replicating in MicMap. Read for "how it can work" reassurance, not for code copy (bey-closer-t1 is a different driver shape).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/audio/src/audio_capture.cpp:186-230` — `WASAPIAudioCapture` ctor + dtor handle COM init, enumerator creation, `IMMNotificationClient` register/unregister, ComPtr cleanup. **Reused as-is in P6 by ensuring construction site is the audio worker thread.** No shared-lib modifications.
- `src/audio/include/micmap/audio/audio_capture.hpp` — `IAudioCapture` interface + `createAudioCapture()` factory (or equivalent). Inherited by driver via `micmap::core_runtime` per Phase 5 D-10.
- `driver/src/device_provider.cpp:58-79` — existing `Init` flow with `VR_INIT_SERVER_DRIVER_CONTEXT`, bindings patch, CommandQueue + HttpServer construction. **Insertion point for `VRSettings()->GetBool` and conditional AudioWorker construction.**
- `driver/src/device_provider.cpp:81-107` — existing `Cleanup` flow with reverse-order teardown. **Insertion point for `audioWorker_.reset()` as the new first step.**
- `driver/src/command_queue.hpp` (existing) — bounded queue used for HTTP→RunFrame. **Untouched in P6**; P7 adds the audio-thread producer.

### Established Patterns
- `driverLogSink` (`device_provider.cpp:28-30`) — wraps `DriverLog` into the shared-lib `LogSink` shape, used by `bindings_patcher`. Audio worker can use raw `DriverLog` directly (driver-only code, no LogSink injection needed in P6 — LIB-04 sink injection is P8).
- Driver-only files live under `driver/src/` and are linked into `driver_micmap.dll` only — no shared-lib reach. Phase 5 D-10/D-11 enforced this. **P6 follows: `driver/src/audio_worker.{hpp,cpp}` is driver-only.**
- vrsettings keys read via `vr::VRSettings()->GetBool/GetInt32/GetString` — this is the **first** P6 read; existing `default.vrsettings` keys (`enable`, `http_port`, `http_host`) are not yet code-consumed. P6 introduces the read pattern; P7+ may extend.
- HMD-handle invalidation pattern (`device_provider.cpp:121-135` — VREvent_TrackedDeviceDeactivated handling) — read-only context. P6 audio worker is independent of HMD-handle state.
- `Microsoft::WRL::ComPtr` is the project's preferred COM smart pointer (already used in `WASAPIAudioCapture`); manual `InterlockedIncrement`/`Decrement` on `DeviceNotificationClient` (audio_capture.cpp:119-133) is flagged in CONCERNS.md but **not refactored in P6** (Pitfall 13 mitigation is the alive-flag, not the ref-count rework).

### Integration Points
- `default.vrsettings` (`driver/resources/settings/default.vrsettings`) — add `"enable_driver_audio": false` to existing `driver_micmap` section.
- `driver/CMakeLists.txt` — already links `PRIVATE micmap::core_runtime` per Phase 5 D-10. **P6 adds new TUs `audio_worker.cpp` to the driver target** (no new link dependencies — `IAudioCapture` is already pulled via `micmap::core_runtime`).
- `driver/src/device_provider.hpp` — add `bool driverAudioEnabled_`, `std::unique_ptr<AudioWorker> audioWorker_` members. Existing class shape preserved.
- `apps/mic_test/` — **untouched in P6**. Headless invariant carried from P5 (mic_test does not link the driver). The audio capture surface mic_test exercises is identical to what the driver pulls.
- `apps/micmap/` (client) — **untouched in P6**. Client still owns audio + detection + trigger via POST /button. Phase 5 client-side parity preserved.

</code_context>

<specifics>
## Specific Ideas

- **"Validate the highest-risk unknown before building on top of it."** P6 exists because WASAPI inside the vrserver DLL host is the load-bearing assumption for the entire v1.6 architecture. Treat the UAT D-17(1) RMS log as the milestone go/no-go signal.
- **Constructor placement is the apartment trick.** The single hardest design decision in P6 is "how do you reuse `WASAPIAudioCapture` without violating Pitfall 1?" — answer: don't refactor the class, just construct it on the worker thread. The `comInitialized_` member ends up tracking the worker thread's COM apartment, which is exactly the contract Pitfall 1 wants. Document this clearly in `audio_worker.cpp` so a future maintainer doesn't "fix" it by moving construction back into `DeviceProvider::Init`.
- **Bey-closer-t1 is the existence proof, not the implementation guide.** Sister project `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` validated WASAPI-in-DLL-host once externally. Read it for confidence that the architecture is feasible. **Do not** copy its code — different driver shape, different lifecycle.
- **Spike scope discipline.** SC1 says "first 1 second of RMS readings." Not "wired to FFT," not "feeds detection," not "pushes TapCommand." If P6 starts to grow into "let's also try the SPSC ring," stop and defer to P7. Each phase ends in a self-consistent UATable state — that's the migration discipline.
- **Flag-OFF means flag-OFF.** SC4 demands byte-identical behavior to v1.5 when `enable_driver_audio = false`. The audio worker isn't constructed at all in that path (D-03). No idle thread, no dormant state — just the v1.5 driver, exactly. Verify with `hmd_button_test.exe` before declaring phase done.

</specifics>

<deferred>
## Deferred Ideas

- **SPSC SampleRing + audio→detection plumbing** — **Phase 7 (Driver-Side Detection Thread)**. P6 drops frames after the 1s RMS budget; P7 introduces the lock-free ring (research SUMMARY component #3) and the `DetectionRunner` consumer.
- **`OnDefaultDeviceChanged` follow-the-default behavior + device pinning UI** — **Phase 7+ / Phase 8 (IPC reshape)**. Pitfall 14 mitigation. Driver becomes config reader in P8; device pinning UI lands when the IPC surface supports `GET /devices` + `PUT /settings`.
- **50-cycle Init/Cleanup stress test** — **Phase 7 SC4**. P6 D-17(3) is a single-cycle spot-check sufficient for the spike. Full Pitfall 4 hardening + Process Explorer leak verification belongs to the phase that adds the detection thread (which is the phase that actually exercises the lifecycle under load).
- **`DeviceNotificationClient` ComPtr migration** — flagged in `.planning/codebase/CONCERNS.md` (manual `InterlockedIncrement`/`Decrement`). Spike-grade alive-flag is enough for P6 (D-15/D-16); the full ComPtr-ification is a Pitfall 13 follow-up belonging to **Phase 7** when the notification client becomes load-bearing.
- **`config.json.audio.deviceId` reading from driver / driver as sole config writer** — **Phase 8 (IPC Reshape)** per IPC-05. Adding ConfigManager/nlohmann/json into the driver in P6 would prematurely fight the P5 D-15/D-16 deferrals.
- **LIB-04 logger sink injection (`DriverLogSink` + `FileLogSink`)** — **Phase 8**. P6 audio worker uses raw `DriverLog` directly; sink injection is a separate refactor that lands when the IPC contract reshapes.
- **`RPC_E_CHANGED_MODE` shared-lib handling** — D-06 puts the distinct-log-line in driver-only code. If a future phase needs the shared lib itself to surface this distinctly, that's a P7+ refactor of `WASAPIAudioCapture::comInitialized_`. P6 deliberately leaves shared lib unchanged.
- **`hmd_button_test.exe` retirement** — open question per ROADMAP "hmd_button_test.exe decision" (TEST-05 vs TEST-02 overlap). P6 D-17(4) actively uses it as the v1.5 regression harness, so retirement is **at earliest Phase 10**.
- **cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728)** — already deferred to **Phase 8** prerequisite plan per P5 D-15. P6 unchanged.

</deferred>

---

*Phase: 06-driver-side-audio-capture-spike*
*Context gathered: 2026-05-02*
