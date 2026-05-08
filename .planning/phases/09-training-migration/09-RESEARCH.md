# Phase 9: Training Migration — Research

**Researched:** 2026-05-08
**Domain:** Driver-resident training pipeline + WAV replay regression harness
**Confidence:** HIGH (every architectural decision is precedented in P5–P8 in-tree; only the WAV decoder is new and is single-header public-domain)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

The full lock list lives in `.planning/phases/09-training-migration/09-CONTEXT.md` <decisions>. The 40 D-nn decisions there are authoritative. Highlights consumed by this research:

- **D-01/D-02/D-03/D-04** — `enum class DriverMode { Detecting, Training }` lives as `std::atomic<DriverMode>` (recommended owner: `DeviceProvider`). DetectionRunner's per-iteration loop branches on the atomic. AudioWorker is unchanged. Mode flips happen on the HTTP thread only. Same SPSC ring, same WASAPI thread, same `weak_ptr<State>` UAF guard.
- **D-05/D-06** — Anti-feature TRAIN-AF-01 enforced **structurally**: client training body at `apps/micmap/main.cpp:962-1027` is **deleted** in P9 Wave 3. New CMake lint `cmake/AssertNoClientTraining.cmake` greps `apps/micmap/` for `\b(addTrainingSample|finishTraining|startTraining|saveTrainingData)\b` and fails on any hit. `apps/mic_test/` is allowlisted.
- **D-07** — `GET /health` JSON gains `"driver_training_active": bool` field. Client polls `/health` (already 1 Hz from v1.5/P7); when true the Train button is replaced by "Training in progress…" + Cancel.
- **D-09/D-10** — `TrainingSession` is owned by `DeviceProvider`, lazy-constructed on `POST /training/start`, destroyed on finalize/cancel/timeout. Single-instance mutex. Linear states `collecting → computing → ready → finalized`; `cancelled` is a terminal sibling.
- **D-11** — Target sample count = `PatternTrainer::TrainingConfig::maxSamples` (default 100). Not configurable via `POST /training/start` payload in P9.
- **D-12** — Bounded collect-window timeout: 30 s of no-new-accepted-sample auto-cancels with `last_error="training_timed_out_no_samples"`. Per-session, resets on every accepted sample.
- **D-13** — `POST /training/cancel` is idempotent — returns 200 OK whether or not a session is active; body `{"cancelled": true|false}`.
- **D-14** — Driver crash mid-session: `training_data.bin` is untouched (single-writer rule). On restart, prior committed profile loads via P7 path at `detection_runner.cpp:160`. **No resume** — collected samples lost.
- **D-15/D-16** — `POST /training/finalize` requires session in `ready` state. Payload `{"confirm": true}` (preferred) or `{"sensitivity": float, "threshold": float}` (TRAIN-03 verbatim). Hand-rolled validators extend `driver/src/settings_validator.cpp`.
- **D-17** — Stored sample set is RAM-only (privacy + cost). Lost on driver restart. Recompute lifetime = session lifetime.
- **D-18/D-19/D-20/D-21** — `POST /training/recompute` payload `{"sensitivity": float}` only. Valid only in `ready` state. Replaces `thresholds_preview` in-place. Must be followed by finalize to commit. Idempotent within `ready`.
- **D-22** — `GET /training/progress` shape: `{samples_collected, target, thresholds_preview | null, state, last_error}`. `thresholds_preview` includes `spectral_profile_summary {mean, stddev, size}` (NOT the full FFT-bin vector).
- **D-23/D-24/D-25/D-26/D-27** — Atomic single-writer cutover for `training_data.bin` in same plan that wires finalize. New `driver/src/training_io.{hpp,cpp}` mirrors `config_io.{hpp,cpp}` shape. ReplaceFileW + corruption-backup retention reused. Path: `%APPDATA%\MicMap\training_data.bin` (unchanged). Driver re-loads after every successful finalize (or in-memory swap).
- **D-28..D-37** — `mic_test --replay` lives in `apps/mic_test/` (TEST-01 invariant — no SteamVR, no driver). CLI: `--replay <wav>`, `--replay-dir <dir>`, `--expect-triggers <N>`, `--expect-triggers-tolerance <±N>`, `--expect-triggers-from <manifest>`, `--profile <path>`, `--config <path>`, `--json-output <path>`, `--max-duration <s>`. Stereo→mono downmix; linear-interp resample; 16-bit PCM + 32-bit float supported (24/8-bit return exit 2). **Flat-out pacing** (no realtime). **Deterministic** — dt computed from sample count. WAV decoder vendored single-header (recommend `dr_wav.h`). Initial corpus seed in `tests/corpus/replay/` + `manifest.json`. CI runs `mic_test_replay_corpus` test on every build.
- **D-38** — Six-wave plan layout (mirrors P8): Wave 0 lints + scaffolds; Wave 1 TrainingSession + training_io + DriverMode; Wave 2 5 HTTP routes + IDriverApi methods + /health field; Wave 3 client UI rewire + cutover; Wave 4 WAV replay harness; Wave 5 UAT.
- **D-39** — UAT regimen has 10 mandatory items (training round-trip, cancel mid-session, recompute→preview→finalize, validation rejection, orphan timeout, driver-down during training UX, lint go-live verification, CI corpus replay, replay determinism, 50-cycle stress).
- **D-40** — P9 does **not** flip any default flag. Training endpoints reachable only when `enable_driver_audio=1`; when audio is off, `POST /training/start` returns HTTP 503 `{"error":"audio_disabled"}`. P10 owns flag flips.

### Claude's Discretion

- DriverMode atomic owner: `DeviceProvider` (recommended) vs `DetectionRunner`. Pick smallest diff.
- Whether `TrainingSession` mirrors `PatternTrainer`'s sample accumulator or trusts its internal storage. Inspect first; recommend trust (PatternTrainer keeps `spectra` vector internally — verified below).
- WAV decoder choice — **`dr_wav.h` recommended** (public-domain / MIT-0, single-header, 0.14.6, no deps).
- `--expect-triggers-from` and `--expect-triggers` mutual exclusion vs stacking — recommend mutually exclusive with stderr error on collision.
- Resample target rate — read from `INoiseDetector::getSampleRate()` if exposed via `TrainingData.sampleRate`; otherwise use the loaded profile's `sampleRate`, fall back to detector ctor's `sampleRate` arg (P9 detector is constructed with the WASAPI capture rate at `audio_worker.cpp` ctor time).
- 30 s timeout (D-12) — tune empirically during UAT.
- Recompute preview wire shape — P9 ships `spectral_profile_summary` (mean/stddev/size); full vector deferred to TRAIN-D2.
- Replay verbosity — `--quiet` for CI summary line, `--verbose` for per-trigger stdout. Pick smaller surface; document.

### Deferred Ideas (OUT OF SCOPE)

- `POST /button` deletion + `IDriverApi::tap()` removal → **P10 (MIG-05)**.
- `enable_driver_detection` default flip → **P10 cutover commit**.
- Client-side detection deletion → **P10**.
- Tray-icon state glyphs (HEALTH-08), FAIL-01..05 graceful failure UX, INST-09, TEST-01..03/05 → **P10**.
- TRAIN-D2 spectral-profile sparkline / A/B threshold preview → **future GUI revamp**.
- TRAIN-D3 configurable target sample count via `POST /training/start` payload → **future**.
- Persistent training-sample storage for cross-session recompute → **rejected (privacy + cost)**.
- Resume training across SteamVR restart → **rejected (complexity / value)**.
- High-quality (sinc / polyphase) WAV resampling → **YAGNI**.
- Realtime-paced replay → **rejected (defeats agent-loop optimization)**.
- 8-bit / 24-bit WAV support → **rejected (>95% of WAVs are 16/32-bit)**.
- Multi-error aggregation on PUT /training/* validation → **future**. P9 inherits P8's first-failed-field reporting.
- `OnDefaultDeviceChanged` device pinning → **P10+**.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TRAIN-01 | `POST /training/start` puts driver in training mode, holds mic, begins sample collection. Detection mutex-paused. | §"DriverMode mode-switch architecture", §"TrainingSession lifecycle". Reuses P7 D-15 atomic-snapshot mechanism. |
| TRAIN-02 | `GET /training/progress` returns `{samples_collected, target, thresholds_preview, state}`; client polls 5–10 Hz. | §"Progress endpoint — lock-free read of atomics", §"Wire shape D-22". Lock-free atomic load on session-internal counters. |
| TRAIN-03 | `POST /training/finalize` accepts final thresholds (or `confirm: true`); persists `training_data.bin`; returns to detection. | §"Atomic single-writer cutover", §"ReplaceFileW pattern". Wraps `INoiseDetector::saveTrainingData` with `ReplaceFileW` + corruption-backup. |
| TRAIN-04 | `POST /training/cancel` aborts in-flight, discards samples, returns to detection mode without modifying file. | §"Cancel — idempotent", §"PatternTrainer::cancelTraining()". Existing API at `pattern_trainer.cpp:278-290` clears all in-memory state. |
| TRAIN-05 | Client never takes mic during training (TRAIN-AF-01 — single-owner WASAPI invariant). | §"Anti-feature enforcement structural, not behavioral". Lint `cmake/AssertNoClientTraining.cmake` greps for `addTrainingSample\|finishTraining\|startTraining\|saveTrainingData` in `apps/micmap/`. |
| TRAIN-06 | `POST /training/recompute {sensitivity}` re-derives thresholds over stored sample set without re-collecting. Returns preview for confirm/discard. | §"Recompute semantics — RAM-only sample buffer". Re-runs `PatternTrainer::finishTraining()` internals over cached `spectra` vector. |
| TEST-04 | `mic_test.exe --replay <wav>` feeds WAV into detection pipeline. Reproducible regression. | §"WAV replay harness — agent-driven QA optimization". `dr_wav.h` single-header; deterministic dt from sample count (matches `detection_runner.cpp:466-471`). |
| IPC-06 | Driver is sole writer of `training_data.bin` (writes only on `POST /training/finalize`). | §"Single-writer cutover (mirrors P8 D-07)", §"AssertNoClientTraining lint". |

</phase_requirements>

## Project Constraints (from CLAUDE.md)

- **Stack locked this milestone:** C++17, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib v0.20.1, nlohmann/json v3.11.2, OpenVR SDK. **No framework changes.** P9 adds exactly one new vendored dep: `dr_wav.h` single-header (recommended).
- **Windows-only:** `ReplaceFileW`, `SHGetFolderPathW`, named mutex via Win32; non-Windows audio stubs already in tree (preserve).
- **Bash via Git Bash; Unix-style paths in shell commands.**
- **Phase artifacts are project memory across context resets — do not skip.**
- **Visual hardware validation mandatory** for "no laser beam" exit criteria — type-checking and build-success do not substitute. P9 inherits this discipline for "training round-trip on real hardware" (UAT D-39 item 1).

## Summary

P9 takes a phase that is **architecturally novel** (no v1.5 prior art for the training UX) but **mechanically familiar**: every primitive used in the new endpoints, the new TrainingSession class, the atomic mode-switch, the sole-writer cutover, and the validation envelope already exists in tree from P5–P8. The training pipeline itself (`PatternTrainer`, `INoiseDetector::saveTrainingData/loadTrainingData`) lives in `micmap_core_runtime` and is fully reusable — P9 wraps it in HTTP/state-machine plumbing rather than rewriting it. The biggest **new** surface is the WAV replay harness, where `dr_wav.h` is the obvious vendored single-header choice (public-domain / MIT-0, ~0.14.6, used by raylib, FAudio, raudio, and other production projects).

The single load-bearing insight is **mode-switch architecture, not pause-detection-thread architecture**. DetectionRunner stays alive; same SPSC ring; same WASAPI thread; same UAF guards. The per-iteration consumer branch flips between `detector_->analyze` (Detecting) and `trainingSession_->addSample` (Training) by reading a single `std::atomic<DriverMode>` once per loop iteration. This inherits P6/P7 lifecycle correctness for free — no new pause/resume gymnastics, no double-init, no COM apartment dance.

The replay harness is **agent-driven QA infrastructure, not a developer toy** (per user direction). Batch directory mode, JSON output, expectation manifests, deterministic flat-out pacing, CI corpus invocation. The state-machine `dt` is already deterministic in tree (computed from sample count at `detection_runner.cpp:466-471` — verified). The replay path mirrors that exact pattern, so determinism is "preserve, don't invent."

**Primary recommendation:** Plan the phase as **structural extension of P7/P8**, not as a new architectural arc. Six waves mirroring P8's shape (D-38). Most of the code is lift-and-adapt from `config_io.cpp` (for `training_io.cpp`), from P8's HTTP route handlers (for the 5 new training endpoints), from P7's DetectionRunner mode-branching (for the new `DriverMode` atomic), and from `PatternTrainer` (which already does the heavy lifting). The replay harness is the only "from-scratch" component, and `dr_wav.h` shrinks even that to ~300 LoC of glue.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Training mode arbitration (Detecting ↔ Training) | API/Backend (driver) | — | Single-owner WASAPI invariant requires it. Client never opens its own capture; only the driver decides who consumes the ring. |
| Sample collection during training | API/Backend (driver) | — | Same WASAPI thread already feeding the SPSC ring; consumer just branches on mode. |
| Threshold computation (FFT averaging, energy/flatness stats) | API/Backend (shared lib) | — | `PatternTrainer::finishTraining()` runs synchronously inside the driver process; `micmap_core_runtime` already linked. |
| `training_data.bin` persistence | API/Backend (driver) | Database/Storage (filesystem) | Driver is sole writer (IPC-06). `INoiseDetector::saveTrainingData` body reused; ReplaceFileW + corruption-backup wraps it. |
| Training progress UI (progress bar, preview confirm) | Frontend (ImGui client) | — | Pure observer — polls `GET /training/progress` at 5–10 Hz, renders. No mic ownership. |
| Train/Cancel/Recompute buttons | Frontend (ImGui client) | API/Backend | UI button → `IDriverApi::startTraining()` etc. → HTTP request → driver state machine. |
| Validation envelope ({field, reason}) | API/Backend (driver) | — | Hand-rolled validators in `driver/src/settings_validator.cpp` (extend, not new file). All-or-nothing per P8 D-14. |
| WAV replay regression harness | Test affordance (`apps/mic_test/`) | — | TEST-01 invariant — no SteamVR, no driver, no `vr::*`. Links `micmap_core_runtime` directly. |
| Anti-feature TRAIN-AF-01 enforcement | Build/Lint (CMake) | — | Structural, not runtime — `cmake/AssertNoClientTraining.cmake` greps the source. |

## Standard Stack

### Core (already in tree, reused)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `micmap_core_runtime` (INTERFACE) | P5 | Aggregates `micmap_audio` + `micmap_detection` + `micmap_core` + `micmap_common` | [VERIFIED: `src/CMakeLists.txt`] Single boundary; LIB-03 enforces no OpenVR symbols cross. |
| `PatternTrainer` (in `micmap_detection`) | in tree | Sample collection + threshold computation | [VERIFIED: `src/detection/include/micmap/detection/pattern_trainer.hpp:48-137`] Full lifecycle (`startTraining`, `addSample`, `finishTraining`, `cancelTraining`, `isTraining`, `isComplete`, `getSpectralProfile`, `getEnergyThreshold`, `getStats`). P9 wraps with HTTP/state-machine plumbing — does not rewrite. |
| `INoiseDetector` (in `micmap_detection`) | in tree | Save/load training_data.bin; runtime detection | [VERIFIED: `src/detection/include/micmap/detection/noise_detector.hpp:84-96`, `src/detection/src/noise_detector.cpp:329-437`] Binary file format with magic + version + spectralProfile vector. P9 wraps `saveTrainingData` with ReplaceFileW. |
| `cpp-httplib` v0.20.1 | bumped P8 D-05 | HTTP server + client | [VERIFIED: `external/CMakeLists.txt:65`] CVE-2025-46728 already addressed. P9 adds 5 new routes via `SetupRoutes()` extension; ctor evolves with 5 new callbacks. |
| `nlohmann/json` v3.11.2 | already in tree | JSON serialization for endpoints | [VERIFIED: `external/CMakeLists.txt:10`] Driver-only TUs (Pitfall 15 / P8 D-01); `AssertNoJsonInCore.cmake` lint enforces. |
| `OpenVR SDK` | in tree | Driver-host API | [VERIFIED: existing] Touched only via `device_provider.cpp` and `manifest_registrar.cpp` (SVR-05 invariant). Training endpoints never call `vr::*`. |

### New (vendored single-header)

| Library | Version | Purpose | License | Why Standard |
|---------|---------|---------|---------|--------------|
| `dr_wav.h` (mackron/dr_libs) | 0.14.6 | WAV file decoding for `mic_test --replay` | Public domain / MIT-0 (choose) | [CITED: https://github.com/mackron/dr_libs/blob/master/dr_wav.h] Single-header, zero deps, used by raylib, FAudio, raudio, kbd-audio, RJModules. Supports 16-bit PCM + IEEE 32-bit float (D-32 requirements). One-shot helper `drwav_open_file_and_read_pcm_frames_f32()` matches the agent-loop "load → process" idiom. [VERIFIED: WebFetch of raw header confirmed version + license + API surface.] |

### Supporting (existing in driver, reused)

| Component | Purpose | Reuse Pattern |
|-----------|---------|---------------|
| `driver/src/config_io.{hpp,cpp}` | ReplaceFileW + corruption-backup retention helpers | [VERIFIED: file read in full] `writeAtomicWindows` + `backupAndRotate` are anonymous-namespace free functions; lift verbatim into `training_io.cpp` OR refactor into a shared header `driver/src/atomic_file_io.hpp`. **Recommend lift-and-modify** (smaller diff, matches P8's choice for the same trade-off). |
| `driver/src/settings_validator.cpp` (P8) | Hand-rolled per-field validators with `optional<ValidationError>` early-return | [VERIFIED: `driver/src/settings_validator.hpp:31-38`] **Extend** with training payload validators (sensitivity range, confirm-required, finalize-payload-shape). All-or-nothing first-failed-field per P8 D-14. |
| `driver/src/http_server.{hpp,cpp}` (P8) | `SetupRoutes()` registers handlers; ctor adds optional callbacks | [VERIFIED: `http_server.hpp:95-104`] Extend ctor with 5 new callback params; add 5 new lambda handlers in `SetupRoutes()`. Same `try { json::parse } catch` envelope. |
| `driver/src/detection_runner.{hpp,cpp}` (P7) | Atomic-snapshot publish/load for `DetectionConfig` | [VERIFIED: `detection_runner.hpp:140`, `detection_runner.cpp:466-471`] **Extend** loop body to branch on `DriverMode`. Sample-count-derived `dt` already in place — replay harness mirrors this exact technique for determinism. |
| `driver/src/audio_worker.{hpp,cpp}` (P6) | WASAPI capture; SPSC ring producer; weak_ptr<State> UAF guard | [VERIFIED: `driver/src/audio_worker.hpp:51-92`] **Unchanged** in P9. Mode flip happens at the consumer; producer doesn't know there are two consumers. |
| `IDriverApi` (P8 D-22 rename) | HTTP client for driver IPC | [VERIFIED: `src/steamvr/include/micmap/steamvr/driver_api.hpp:233-357`] **Add 5 new methods** (`startTraining`, `getTrainingProgress`, `finalizeTraining`, `cancelTraining`, `recomputeTraining`). Mirror P8 D-23/D-24 incremental ctor evolution. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `dr_wav.h` | `libsndfile` (LGPL) | LGPL license ladder, dynamic-link rules, extra build complexity. Project is statically linked; LGPL would force shipping the library object files separately. **Reject.** |
| `dr_wav.h` | `tinywav` (BSD-2) | Smaller scope (no resampling, fewer formats) but BSD-2 is fine. Functionally equivalent for our use case. **Acceptable fallback if `dr_wav.h` becomes unavailable.** |
| `dr_wav.h` | Hand-rolled minimal WAV parser | RIFF chunk parsing + 16-bit PCM unpack + 32-bit float unpack is ~100 LoC, but: stereo→mono, RF64, AIFF, edge-case headers (LIST chunks, data-chunk-size==0xFFFFFFFF for streaming WAVs) become a maintenance tax. **Reject — Don't Hand-Roll.** |
| Single-instance mutex via `std::unique_ptr<TrainingSession>` null check | Named Win32 mutex (`CreateMutexW`) | Driver process is the only writer; in-process check is sufficient. Named mutex would handle multi-process contention which we explicitly do not have (driver is the only thing in the driver process). **Reject.** |
| `std::vector<std::vector<float>>` for sample buffer | Fixed-size flat buffer | `PatternTrainer::Impl::spectra` already uses `std::vector<std::vector<float>>` — matching it minimizes friction. ~150 samples × ~256 FFT bins × 4 bytes = ~150 KB peak. Fine. **Use existing.** |
| Realtime-paced replay (drive at 1× wall-clock) | Flat-out (sample-count dt) | Realtime defeats the agent-loop optimization. 100-WAV corpus would take 1000s of seconds at realtime; flat-out completes in seconds. State machine `dt` already injected (deterministic). **Reject realtime.** |
| Per-test-process replay (CI invokes mic_test once per WAV) | Single-process batch (`--replay-dir`) | Per-process is portable but slow (process spawn ~50 ms × 100 WAVs = 5 s overhead). Batch matches the agent-loop idiom. **Use batch (D-29).** |

**Installation (vendoring):**

```cmake
# external/CMakeLists.txt — append (D-32 / planner discretion)
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG dbbd08d839272fc71325f4642c1bd00f290fbd71  # pin to a known-good commit
)
FetchContent_GetProperties(dr_libs)
if(NOT dr_libs_POPULATED)
    FetchContent_Populate(dr_libs)
endif()
add_library(dr_wav INTERFACE)
target_include_directories(dr_wav INTERFACE ${dr_libs_SOURCE_DIR})
```

Alternative (simpler): commit `vendor/dr_wav/dr_wav.h` directly into the tree at HEAD ~0.14.6. Single-header, no build system needed. Pin to a known commit hash. **Recommend the direct-commit form** (matches the project's "lock the stack this milestone" constraint).

**Version verification:**

```bash
# Verified 2026-05-08 via WebFetch + WebSearch:
# dr_wav.h version: 0.14.6 (or 0.14.5 in some forks; both fine for our needs)
# License: public domain or MIT-0 (choice). [CITED: https://github.com/mackron/dr_libs/blob/master/dr_wav.h]
```

## Architecture Patterns

### System Architecture Diagram

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  CLIENT PROCESS (apps/micmap/main.cpp)                                       │
│  ┌────────────────────────────────────────────────────────────────────────┐  │
│  │  ImGui Training Pane (NEW in P9 — REPLACES :962-1027)                  │  │
│  │   - Train button     → IDriverApi::startTraining()      (HTTP POST)    │  │
│  │   - Cancel button    → IDriverApi::cancelTraining()     (HTTP POST)    │  │
│  │   - Sensitivity slider + Recompute → recomputeTraining (HTTP POST)     │  │
│  │   - Confirm button   → IDriverApi::finalizeTraining({"confirm":true})  │  │
│  │   - Progress bar     ← getTrainingProgress() (HTTP GET, 5 Hz poll)     │  │
│  │   - Train button gated on /health.driver_training_active (D-07)        │  │
│  └────────────────────────────────────────────────────────────────────────┘  │
│        │                                                                     │
│        │ HTTP localhost:27015 (cpp-httplib client)                           │
└────────┼─────────────────────────────────────────────────────────────────────┘
         │
         │
┌────────▼─────────────────────────────────────────────────────────────────────┐
│  DRIVER PROCESS (driver_micmap.dll inside vrserver.exe)                      │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │  HTTP Server Thread (driver/src/http_server.cpp)                     │   │
│  │  POST /training/start    →┐                                          │   │
│  │  GET  /training/progress →┤                                          │   │
│  │  POST /training/finalize →┼─→ DeviceProvider callbacks               │   │
│  │  POST /training/cancel   →┤                                          │   │
│  │  POST /training/recompute→┘                                          │   │
│  │  GET  /health (extended) ──→ driver_training_active getter           │   │
│  └──────────────┬───────────────────────────────────────────────────────┘   │
│                 │ (mutates atomic state; never calls vr::*; never pushes    │
│                 │  to CommandQueue — SVR-05 invariant)                      │
│                 │                                                            │
│  ┌──────────────▼───────────────────────────────────────────────────────┐   │
│  │  DeviceProvider (driver/src/device_provider.cpp)                     │   │
│  │   - std::atomic<DriverMode> mode_         (D-01: Detecting|Training) │   │
│  │   - std::unique_ptr<TrainingSession> trainingSession_  (lazy, D-09)  │   │
│  │   - std::shared_ptr<const AppConfig> configSnapshot_  (P8)           │   │
│  │   - std::shared_ptr<const DriverState> stateSnapshot_ (P8)           │   │
│  │   - 5 new HTTP callbacks: trainingStart, trainingProgress, finalize, │   │
│  │     cancel, recompute                                                │   │
│  └──────────────┬───────────────────────────────────────────────────────┘   │
│                 │                                                            │
│  ┌──────────────▼─────────────────────────────────────────────────────┐    │
│  │  TrainingSession (NEW — driver/src/training_session.{hpp,cpp})     │    │
│  │   - std::unique_ptr<PatternTrainer> trainer_  (hosts shared-lib)   │    │
│  │   - SessionState state_  (collecting → computing → ready ...)      │    │
│  │   - std::atomic<size_t> samples_collected_  (lock-free progress)   │    │
│  │   - thresholds_preview cache (set by computing→ready transition)   │    │
│  │   - std::chrono::steady_clock::time_point lastAcceptedSample_      │    │
│  │     (30 s timeout watchdog — D-12)                                 │    │
│  └──────────────┬─────────────────────────────────────────────────────┘    │
│                 │ addSample(block) when in Training mode                    │
│                 │                                                           │
│  ┌──────────────▼─────────────────────────────────────────────────────┐    │
│  │  DetectionRunner (driver/src/detection_runner.cpp — EXTENDED)      │    │
│  │   Per-iteration loop:                                              │    │
│  │     mode = mode_atomic.load(acquire)                               │    │
│  │     drain ring →                                                   │    │
│  │       if mode==Detecting: detector_->analyze(block) →              │    │
│  │                            stateMachine_->update(conf, dt) →       │    │
│  │                            on rising-edge: commandQueue_->push     │    │
│  │       if mode==Training:  trainingSession_->addSample(block)       │    │
│  │     (state machine + CommandQueue UNTOUCHED in Training mode)      │    │
│  └──────────────┬─────────────────────────────────────────────────────┘    │
│                 │                                                           │
│  ┌──────────────▼─────────────────────────────────────────────────────┐    │
│  │  SampleRing<16, 480>  (driver/src/sample_ring.hpp — UNCHANGED)     │    │
│  └──────────────▲─────────────────────────────────────────────────────┘    │
│                 │ try_push                                                  │
│  ┌──────────────┴─────────────────────────────────────────────────────┐    │
│  │  AudioWorker (driver/src/audio_worker.cpp — UNCHANGED)             │    │
│  │   - WASAPI capture thread; weak_ptr<State> UAF guard               │    │
│  │   - Same producer regardless of consumer mode                       │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  training_io.cpp (NEW)                                             │    │
│  │   saveTrainingFile(path, detector*) →                              │    │
│  │     wraps INoiseDetector::saveTrainingData with                    │    │
│  │     ReplaceFileW + corruption-backup retention                     │    │
│  │     (lifted from driver/src/config_io.cpp's writeAtomicWindows)    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
                                  │ writes once on finalize
                                  ▼
                        %APPDATA%\MicMap\training_data.bin


┌──────────────────────────────────────────────────────────────────────────────┐
│  HEADLESS REPLAY HARNESS (apps/mic_test/main.cpp + wav_replay.{hpp,cpp})     │
│  No SteamVR, no driver, no vr::*  (TEST-01 invariant)                        │
│                                                                              │
│   --replay-dir <dir>                                                         │
│       │                                                                      │
│       ▼                                                                      │
│   for each WAV file:                                                         │
│       drwav_open_file_and_read_pcm_frames_f32() → samples + sampleRate       │
│       stereo→mono downmix (averaging L+R)                                    │
│       linear-interp resample to detector's expected rate                     │
│       feed in 480-frame blocks:                                              │
│           detector.analyze(block) → confidence                               │
│           dt = block_count * 1000 / sampleRate  (sample-count dt — same as   │
│                                                  detection_runner.cpp:469)  │
│           stateMachine.update(confidence, dt)                                │
│           rising-edge Triggered → record {t_s, confidence}                   │
│       compare observed_triggers to expected_triggers (manifest)              │
│       emit JSON record                                                       │
│   write summary JSON; exit 0/1/2                                             │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Recommended Project Structure (NEW + MODIFIED in P9)

```
driver/src/
├── training_session.{hpp,cpp}       # NEW — TrainingSession class (D-09..D-22)
├── training_io.{hpp,cpp}             # NEW — atomic ReplaceFileW save (D-27)
├── http_server.{hpp,cpp}             # MOD — 5 new routes + 5 new callbacks
├── device_provider.{hpp,cpp}         # MOD — atomic<DriverMode>, training session ownership
├── detection_runner.{hpp,cpp}        # MOD — per-iteration mode branch
├── settings_validator.{hpp,cpp}      # MOD — extend with training payload validators
└── (existing files unchanged)

src/steamvr/
├── include/micmap/steamvr/driver_api.hpp   # MOD — 5 new IDriverApi methods
└── src/driver_api.cpp                       # MOD — cpp-httplib client impls

apps/micmap/
└── main.cpp                          # MOD — DELETE :962-1027 + :618 + :991
                                      #       INSERT new endpoint-driven training pane

apps/mic_test/
├── main.cpp                          # MOD — add CLI flag parsing for --replay etc.
├── src/wav_replay.{hpp,cpp}           # NEW — WAV decode + downmix + resample + JSON output
└── CMakeLists.txt                    # MOD — register wav_replay.cpp + dr_wav include

vendor/dr_wav/
└── dr_wav.h                          # NEW — single-header WAV decoder (PD/MIT-0)

cmake/
├── AssertNoClientTraining.cmake      # NEW — sibling of AssertNoConfigWriteInClient.cmake
└── AssertReplayNoVrApi.cmake          # NEW — sibling of AssertNoOpenVRInCore.cmake

tests/
├── corpus/replay/                     # NEW — initial corpus seed
│   ├── positive_001.wav
│   ├── negative_silence_001.wav
│   ├── negative_speech_001.wav
│   ├── manifest.json
│   └── README.md
├── driver/training_session_test.cpp           # NEW — lifecycle tests
├── driver/training_endpoint_validation_test.cpp # NEW — payload validation envelope
└── mic_test/wav_replay_test.cpp                # NEW — decoder + resample + JSON unit tests
```

### Pattern 1: DriverMode atomic mode-switch (extends P7 D-15)

**What:** Single `std::atomic<DriverMode>` member on `DeviceProvider`. HTTP handlers swap with `store(release)`; detection loop reads with `load(acquire)` once per iteration.

**When to use:** When two competing in-process consumers need to coordinate without locking the producer. Hot path (detection loop) is a single acquire-load per iteration; cold path (HTTP handler) is one release-store per state transition.

**Example (D-01/D-02 wiring):**

```cpp
// Source: extension of existing P7 pattern at driver/src/detection_runner.cpp:466-471
//          (which already does atomic-shared_ptr load on activeConfig_).
//          NEW in P9: a simpler atomic-of-enum (no shared_ptr).

// driver/src/device_provider.hpp (NEW members)
enum class DriverMode { Detecting, Training };
std::atomic<DriverMode> mode_{DriverMode::Detecting};

// driver/src/detection_runner.cpp (modified RunLoop body, replaces :467)
const auto mode = deviceProvider_.mode().load(std::memory_order_acquire);
const uint32_t rate_for_dt = sampleRate_ ? sampleRate_ : 1;
while (ring_.try_pop(block, block_count)) {
    if (mode == DriverMode::Detecting) {
        // EXISTING P7 path — verbatim.
        auto result = detector_->analyze(block.data(), block_count);
        const auto dt = std::chrono::milliseconds(
            static_cast<long long>(block_count) * 1000 / rate_for_dt);
        stateMachine_->update(result.confidence, dt);
    } else {
        // NEW P9 path — forward to training session.
        if (auto* session = deviceProvider_.trainingSession()) {
            session->addSample(block.data(), block_count);
        }
        // State machine + CommandQueue NOT touched in Training mode.
    }
}

// driver/src/http_server.cpp (POST /training/start handler — Wave 2)
server_->Post("/training/start", [this](const httplib::Request& req, httplib::Response& res) {
    if (auto err = trainingStart_()) {  // mutates atomic mode_, constructs TrainingSession
        res.status = err->status;
        res.set_content(err->body, "application/json");
        return;
    }
    res.set_content(R"({"status":"ok"})", "application/json");
});
```

**Memory order rationale:** release-store on flip publishes the side effects (TrainingSession construction). acquire-load on read pairs synchronizes-with that store and observes the constructed object. Identical discipline to P7 `activeConfig_` snapshot at `detection_runner.cpp:85, 99`.

### Pattern 2: TrainingSession lifecycle (lazy, single-instance, idempotent)

**What:** `std::unique_ptr<TrainingSession>` member on `DeviceProvider`. Lazy construction on first `POST /training/start`; reset on finalize/cancel/timeout.

**When to use:** Optional state owner where construction is expensive (PatternTrainer + sample buffer) and lifetime is bounded by external triggers.

**Example (D-09 / D-10 / D-12):**

```cpp
// driver/src/training_session.hpp (NEW — D-09..D-22)
namespace micmap::driver {

enum class SessionState { Collecting, Computing, Ready, Cancelled };

struct ThresholdsPreview {
    float sensitivity{0.0f};
    float energy_threshold{0.0f};
    struct { float mean; float stddev; size_t size; } spectral_profile_summary;
};

class TrainingSession {
public:
    explicit TrainingSession(uint32_t sampleRate, size_t fftSize);
    ~TrainingSession();

    // Called from detection thread when DriverMode == Training. Mutates
    // samples_collected_ atomic and lastAcceptedSample_ steady_clock.
    void addSample(const float* samples, size_t count);

    // Called from HTTP thread (POST /training/finalize handler).
    // Returns nullopt on success; ValidationError on insufficient samples
    // or wrong state.
    std::optional<ValidationError> compute();   // collecting → computing → ready
    bool recompute(float sensitivity);          // ready → ready (replaces preview)

    // Lock-free read for GET /training/progress.
    struct ProgressSnapshot {
        size_t samples_collected;
        size_t target;
        std::optional<ThresholdsPreview> thresholds_preview;
        SessionState state;
        std::optional<std::string> last_error;
    };
    ProgressSnapshot snapshot() const;   // mu_-locked except samples_collected_

    // Watchdog tick — called from HTTP thread or a low-cadence timer.
    // Returns true iff session auto-cancelled due to 30 s timeout.
    bool tickTimeout(std::chrono::steady_clock::time_point now);

    SessionState state() const;

private:
    std::unique_ptr<micmap::detection::PatternTrainer> trainer_;
    std::atomic<size_t> samples_collected_{0};
    SessionState state_{SessionState::Collecting};
    std::optional<ThresholdsPreview> preview_;
    std::optional<std::string> last_error_;

    // Bounded collect-window timeout (D-12). Reset on every accepted sample.
    std::chrono::steady_clock::time_point lastAcceptedSample_;

    mutable std::mutex mu_;   // protects state_, preview_, last_error_, lastAcceptedSample_
};

} // namespace micmap::driver
```

**Lifecycle semantics:**
- `POST /training/start` — DeviceProvider checks `if (trainingSession_) return 409` (single-instance mutex via unique_ptr null check, D-09); else flips `mode_=Training` (release), constructs `trainingSession_ = std::make_unique<TrainingSession>(...)`.
- `POST /training/finalize` — calls `trainingSession_->compute()` if state == Collecting (with `confirm: true` + ≥ MIN_TRAINING_SAMPLES guard); then writes file via `training_io.cpp`; flips `mode_=Detecting` (release); destroys session.
- `POST /training/cancel` — idempotent (D-13). If `trainingSession_` is null, return `{"cancelled": false}` 200. Else flip mode, destroy session, return `{"cancelled": true}`.
- Watchdog — driven from HTTP thread on every `/training/progress` request (cheap; same thread that mutates anyway). Or by RunFrame's existing low-cadence callback. Keep it simple: don't add a new thread.

### Pattern 3: Atomic single-writer cutover for training_data.bin (mirrors P8 D-07 + CFG-04)

**What:** Driver wraps `INoiseDetector::saveTrainingData` (which uses plain `std::ofstream` at `noise_detector.cpp:329`) with `ReplaceFileW` + corruption-backup retention. Client write paths deleted in same plan; lint goes live in same plan.

**Example (D-23 / D-27):**

```cpp
// driver/src/training_io.hpp (NEW)
namespace micmap::driver {
std::filesystem::path getDriverTrainingDataPath();  // %APPDATA%\MicMap\training_data.bin
bool saveTrainingFile(const std::filesystem::path& path,
                      micmap::detection::INoiseDetector& detector);
} // namespace

// driver/src/training_io.cpp (NEW)
// LIFT-AND-MODIFY: writeAtomicWindows + backupAndRotate from
// driver/src/config_io.cpp (verbatim except path-suffix string changes).
// Same anonymous-namespace shape; same ReplaceFileW + tmp-file idiom;
// same backup-rotate-keep-5 retention.
//
// Strategy: detector.saveTrainingData(tmpPath) → ReplaceFileW(dest, tmp)
// We rely on INoiseDetector's existing serialization (header + spectralProfile).

bool saveTrainingFile(const std::filesystem::path& path, INoiseDetector& detector) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        MICMAP_LOG_ERROR("training_io: could not create dir: ", ec.message());
        return false;
    }
    const auto tmp = path.parent_path() / (path.filename().wstring() + L".tmp");

    // Stage 1: detector writes to .tmp (existing ofstream path at
    // noise_detector.cpp:329 — passes path through unchanged).
    if (!detector.saveTrainingData(tmp)) {
        std::error_code remEc; fs::remove(tmp, remEc);
        return false;
    }

    // Stage 2: ReplaceFileW (atomic on Windows for same-volume swap).
#ifdef _WIN32
    if (fs::exists(path, ec)) {
        if (!ReplaceFileW(path.c_str(), tmp.c_str(), nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            const DWORD err = GetLastError();
            MICMAP_LOG_ERROR("training_io: ReplaceFileW failed (GetLastError=", err, ")");
            std::error_code remEc; fs::remove(tmp, remEc);
            return false;
        }
    } else {
        if (!MoveFileExW(tmp.c_str(), path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD err = GetLastError();
            MICMAP_LOG_ERROR("training_io: MoveFileExW failed (", err, ")");
            std::error_code remEc; fs::remove(tmp, remEc);
            return false;
        }
    }
#endif
    return true;
}
```

**Anti-pattern:** Don't try to wrap `detector.saveTrainingData` with `ReplaceFileW` in a way that requires `INoiseDetector` to write to a memory buffer. The existing API at `noise_detector.hpp:89` takes a `path` and writes there. The cleanest atomic-write idiom is "ask detector to write to .tmp; rename .tmp over destination." This preserves the v1.5 binary format byte-for-byte (no re-implementation, no drift risk) and matches `config_io.cpp`'s shape.

### Pattern 4: Replay harness — deterministic sample-count dt

**What:** Drive the state machine with `dt = block_count * 1000 / sampleRate` (computed from the WAV, not wall-clock). Identical to `detection_runner.cpp:469-470`. Determinism inherited.

**Example (D-33 / D-34):**

```cpp
// apps/mic_test/src/wav_replay.cpp (NEW)
//
// dt computation IS THE LOAD-BEARING DETERMINISM POINT. Mirror
// detection_runner.cpp:469-470 verbatim. If state machine ever calls
// steady_clock anywhere, that's a bug — verify in PLAN.

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

struct ReplayResult {
    std::filesystem::path wav;
    double duration_s;
    uint32_t sample_rate;
    uint32_t channels;
    int expected_triggers;   // -1 if no expectation
    int observed_triggers;
    int tolerance;
    bool pass;
    std::vector<TriggerEvent> triggers;
};

ReplayResult replayWav(const std::filesystem::path& wav,
                        const ReplayConfig& cfg,
                        INoiseDetector& detector,
                        IStateMachine& sm)
{
    drwav w;
    if (!drwav_init_file_w(&w, wav.c_str(), nullptr)) {
        return makeError(wav, /*exitCode=*/2, "WAV open failed");
    }

    // Bit-depth gate (D-32) — only 16-bit PCM and 32-bit float pass.
    const bool is16bit = (w.bitsPerSample == 16 && w.translatedFormatTag == DR_WAVE_FORMAT_PCM);
    const bool is32f   = (w.bitsPerSample == 32 && w.translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT);
    if (!is16bit && !is32f) {
        drwav_uninit(&w);
        return makeError(wav, 2, "unsupported bit depth (only 16-bit PCM and 32-bit float supported)");
    }

    // Duration cap (D-32 — default 600s).
    const double duration_s = static_cast<double>(w.totalPCMFrameCount) / w.sampleRate;
    if (duration_s > cfg.max_duration_s) {
        drwav_uninit(&w);
        return makeError(wav, 2, "WAV exceeds --max-duration");
    }

    // Read entire WAV as float (interleaved if stereo).
    std::vector<float> raw(w.totalPCMFrameCount * w.channels);
    drwav_read_pcm_frames_f32(&w, w.totalPCMFrameCount, raw.data());
    drwav_uninit(&w);

    // Stereo → mono downmix (D-32). Average L+R.
    std::vector<float> mono;
    if (w.channels == 1) {
        mono = std::move(raw);
    } else {
        mono.resize(w.totalPCMFrameCount);
        for (size_t i = 0; i < w.totalPCMFrameCount; ++i) {
            float sum = 0.0f;
            for (uint32_t c = 0; c < w.channels; ++c) {
                sum += raw[i * w.channels + c];
            }
            mono[i] = sum / static_cast<float>(w.channels);
        }
    }

    // Linear-interp resample to detector's rate (D-32).
    const uint32_t target_rate = detector.getTrainingData().sampleRate
                                  ? detector.getTrainingData().sampleRate
                                  : 48000;   // fallback
    std::vector<float> resampled = (w.sampleRate == target_rate)
        ? std::move(mono)
        : linearResample(mono, w.sampleRate, target_rate);

    // Feed in 480-frame blocks (matches SampleRing<16,480> and detection_runner
    // block size). Drive state machine with sample-count dt.
    constexpr size_t kBlockFrames = 480;
    size_t cursor = 0;
    int observed_triggers = 0;
    std::vector<TriggerEvent> triggers;

    sm.setTriggerCallback([&]{
        const double t_s = static_cast<double>(cursor) / target_rate;
        triggers.push_back({t_s, /*confidence=*/lastConfidence, /*state=*/"Triggered"});
        ++observed_triggers;
    });

    while (cursor < resampled.size()) {
        const size_t n = std::min(kBlockFrames, resampled.size() - cursor);
        auto result = detector.analyze(&resampled[cursor], n);
        const auto dt = std::chrono::milliseconds(
            static_cast<long long>(n) * 1000 / target_rate);
        sm.update(result.confidence, dt);
        cursor += n;
    }

    return {
        wav, duration_s, w.sampleRate, w.channels,
        cfg.expected_triggers, observed_triggers, cfg.tolerance,
        /*pass=*/checkPass(observed_triggers, cfg.expected_triggers, cfg.tolerance),
        std::move(triggers)
    };
}
```

**Why this works:** the existing `IStateMachine::update(confidence, dt)` interface at `state_machine.hpp:87` already takes `dt` as a parameter and is implemented via `timeInState_ += deltaTime` at `state_machine.cpp:35` — no `steady_clock` calls in the cooldown logic. **[VERIFIED via grep at `src/core/src/state_machine.cpp:34-35`].** Replay determinism is preserved by construction.

### Anti-Patterns to Avoid

- **Pause-detection-thread architecture for training.** Don't shut down DetectionRunner and respawn it for training. The mode-switch is a single atomic store; teardown/restart adds COM/WASAPI lifecycle risk for zero benefit. (CONTEXT.md "Specifics" — explicitly called out.)
- **Persistent training-sample storage.** Privacy concern (raw mic-cover audio = user voice/room) + disk cost + rejected by user direction. RAM-only is the lock (D-17).
- **Realtime-paced replay.** Defeats the agent-loop QA optimization. 100 WAVs at realtime = ~2000 s; flat-out = ~5 s. Don't add a `--realtime` flag in P9.
- **Hand-rolled WAV parser.** RIFF + LIST chunks + RF64 + AIFF + 8/12/16/24/32-bit PCM + IEEE float + a-law/u-law + ADPCM is a long tail. `dr_wav.h` handles all of it. Don't reimplement.
- **Validating training payload via JSON Schema.** Hand-rolled per-field validators in `settings_validator.cpp` are fewer LoC, easier to evolve, and match P8's choice. (P8 D-15 explicit.)
- **File-watching `training_data.bin` from the driver.** Same hard rule as `config.json` (Pitfall 5). Driver writes only on `POST /training/finalize`. The "client edited the file outside the API" path doesn't exist.
- **Multi-process training session.** Single-instance mutex is in-process only (unique_ptr null check). Don't add `CreateMutexW` for a multi-process scenario we don't have.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WAV decoding | RIFF chunk parser + format converters + LIST/RF64/AIFF handling | `dr_wav.h` (PD/MIT-0, single-header) | Long tail of edge cases; mature library used by raylib, FAudio, raudio. ~10 KLOC of well-tested C in one file. |
| Atomic file write on Windows | Custom temp-file rename loop | `ReplaceFileW` (already wrapped in `config_io.cpp::writeAtomicWindows`) | Win32-native; preserves ACLs, attributes, alternate streams; survives anti-virus mid-write better than `std::rename`. |
| Threshold computation from training samples | Custom averaging + stddev + L2-normalization | `PatternTrainer::finishTraining()` (existing) | Already in `micmap_core_runtime`; well-tested in v1.5 client. P9 hosts it inside TrainingSession; doesn't reimplement. |
| Binary training_data.bin format | New format | Reuse `INoiseDetector::saveTrainingData` / `loadTrainingData` (existing magic + version + spectralProfile) | Backward compatibility with v1.5 profiles. Driver can load profiles trained by v1.5 client without reformatting. |
| HTTP routing + JSON parsing | Hand-rolled HTTP framework | `cpp-httplib` v0.20.1 (already in tree) + nlohmann/json (already in tree) | Existing P8 patterns; same `try { json::parse } catch` envelope. |
| Per-field validation with `optional<error>` early-return | Custom error-aggregator | Extend `driver/src/settings_validator.cpp` (existing P8 pattern) | First-failed-field reporting is the project convention (P8 D-14/D-15). |
| Sample-count dt computation | Custom timer | Mirror `detection_runner.cpp:469-470` verbatim | Determinism is already-shipped property; preserve by reuse. |
| Single-instance mutex for training session | Win32 named mutex | `std::unique_ptr<TrainingSession>` null check | In-process scope only; no multi-process scenario. Cheaper. |

**Key insight:** P9 is mostly **plumbing**, not algorithm. The training algorithm lives in `PatternTrainer` (200 LoC of FFT averaging, energy/flatness stats, L2 normalization). The persistence format lives in `INoiseDetector` (binary header + float vector). The atomic-write helpers live in `config_io.cpp`. The HTTP route shape lives in `http_server.cpp`. The IDriverApi extension shape lives in `driver_api.cpp`. P9's job is to wire these existing pieces together with one new class (`TrainingSession`), one new helper module (`training_io.{hpp,cpp}`), one new atomic (`DriverMode`), and one external dependency (`dr_wav.h`).

## Runtime State Inventory

> P9 includes a rebrand-style data migration concern (`training_data.bin` ownership transferring from client to driver), so this section is mandatory.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | `%APPDATA%\MicMap\training_data.bin` — file exists from v1.5 client trainings on every user machine. **Format-compatible across client and driver** (same `INoiseDetector` factory, same magic+version+vector). | **No data migration.** Driver reads the existing v1.5-trained file at Init via P7 path (`detection_runner.cpp:160`). On first `POST /training/finalize`, driver overwrites with the same-format file via `ReplaceFileW`. Existing v1.5 profile remains loadable until overwritten. |
| Stored data | Backup files: `%APPDATA%\MicMap\training_data.bin.corrupted.<timestamp>` may exist from previous v1.5 corruption-recovery rotations. | **None — preserve.** P9 inherits the corruption-backup retention pattern (5-file ring) from `config_io.cpp::backupAndRotate`. Existing backups stay; new ones rotate the existing 5-file window. |
| Live service config | None — training is not a configured service. Settings (sensitivity/threshold/cooldown/min-duration) are owned by `config.json` (P8 single-writer) and the `default.vrsettings` (P7 D-13). | **None.** P9 doesn't reshape settings; it adds endpoints. |
| OS-registered state | None — training is a transient runtime mode, not an OS-registered task or service. SteamVR's existing driver registration (vrpathreg) is unchanged. | **None.** |
| Secrets and env vars | None. No secrets are involved in training. The `enable_driver_audio` flag (existing P6 key) gates whether `POST /training/start` is reachable — code reads this via existing `vr::VRSettings()` path. | **None.** P9 reads existing flag; doesn't add new flags. |
| Build artifacts / installed packages | If a developer has built v1.5 client and runs the new driver, the binary `driver_micmap.dll` will pick up the existing v1.5-trained `training_data.bin` cleanly (same format). No stale artifacts. **Wave 0 lints (`AssertNoClientTraining.cmake`) need explicit go-live timing** — Wave 0 ships in skip-on-NOT-EXISTS mode; Wave 3 flips to enforcing mode at the same moment as the cutover. | Document the Wave 0 → Wave 3 lint go-live transition explicitly in the plan. |

**The canonical question** — *After every file in the repo is updated, what runtime systems still have the old string cached, stored, or registered?* — answer: **nothing.** This is not a rename phase. The training pipeline transfers from one process (client) to another (driver) within the same machine, but the persistent on-disk format is identical and the file path is unchanged (`%APPDATA%\MicMap\training_data.bin`). The existing v1.5 trained profile is forward-compatible with the driver's `INoiseDetector` instance (same factory, same code, same shared lib).

## Common Pitfalls

### Pitfall 1: TrainingSession constructor races with detection-loop iteration

**What goes wrong:** HTTP thread calls `mode_.store(Training, release)` then `trainingSession_ = std::make_unique<TrainingSession>(...)`. Detection thread does `mode_.load(acquire)`, sees Training, calls `deviceProvider_.trainingSession()->addSample(...)` — but the session pointer hasn't been published yet.

**Why it happens:** The release on `mode_.store` synchronizes-with the acquire on `mode_.load`, but only IF the constructor's side effects happen-before the store. If the order is "store mode → construct session," the constructor isn't sequenced before the release.

**How to avoid:** Construct the session FIRST, THEN store mode. Use the same `std::atomic_store_explicit` pattern as P7 D-15 for the session pointer if it needs to be lock-free; or use a `std::mutex` on the HTTP-thread side because mode flips are infrequent.

```cpp
// CORRECT (D-09 / D-10 lifecycle):
{
    std::lock_guard<std::mutex> lk(httpMu_);   // single HTTP-side serialization
    if (trainingSession_) return 409;
    trainingSession_ = std::make_unique<TrainingSession>(sampleRate, fftSize);
    // Constructor's writes happen-before this release-store:
    mode_.store(DriverMode::Training, std::memory_order_release);
}

// Detection thread (acquire-load + nullable read):
const auto mode = mode_.load(std::memory_order_acquire);
if (mode == DriverMode::Training) {
    if (auto* s = trainingSession_.get()) {   // pointer read — see Pitfall 2
        s->addSample(...);
    }
}
```

**Warning signs:** Test scaffold `tests/driver/training_session_test.cpp` should include a "rapid start/cancel cycle" case (mirrors UAT D-39 item 10) that catches this.

### Pitfall 2: TrainingSession destruction races with in-flight addSample

**What goes wrong:** HTTP thread calls cancel → flips `mode_=Detecting` → destroys `trainingSession_`. Detection thread, mid-`addSample`, observes the destruction.

**Why it happens:** Detection thread reads `mode` then dereferences `trainingSession_`. Even if mode flips between, the unique_ptr reset can race with the dereference.

**How to avoid:** Order matters — flip mode FIRST (release-store), wait one detection-loop cycle (or rely on the fact that the next iteration will re-read mode and skip), THEN destroy. Or: hold a `shared_ptr<TrainingSession>` and copy it in the detection thread (lock-free, ref-counted).

```cpp
// CORRECT — mirror P7 D-15 atomic-shared_ptr discipline:
std::shared_ptr<TrainingSession> trainingSession_;   // accessed via atomic_load/store

// HTTP cancel handler:
mode_.store(DriverMode::Detecting, std::memory_order_release);
std::atomic_store_explicit(&trainingSession_, std::shared_ptr<TrainingSession>{},
                            std::memory_order_release);
// Old session destructs when last reference (the detection thread's copy) drops.

// Detection thread:
auto session = std::atomic_load_explicit(&trainingSession_, std::memory_order_acquire);
if (mode == DriverMode::Training && session) {
    session->addSample(...);   // session keeps it alive even if HTTP just stored null
}
```

**Recommendation:** **Use `std::shared_ptr<TrainingSession>` with `atomic_load/store_explicit`.** Same C++17 free-function form as `activeConfig_` at `detection_runner.cpp:140`. Symmetric with the existing pattern; zero new concepts.

**Warning signs:** UAT D-39 item 10 (50 rapid start/cancel cycles) is the explicit regression check.

### Pitfall 3: 30 s timeout watchdog races with finalize/cancel

**What goes wrong:** Watchdog thread (or HTTP thread inside `tickTimeout`) auto-cancels at 30 s while a `POST /training/finalize` is mid-flight. Result: file might or might not be written; client sees 200 OK but session is already gone.

**Why it happens:** Two cancellation sources (timeout + explicit cancel) compete for the same atomic mode-flip + session destruction.

**How to avoid:** Single mutex on the session-state-transition path. Watchdog checks state under lock; finalize/cancel handlers acquire the same lock. If finalize is mid-execution, watchdog skips this tick and tries again next call.

```cpp
// CORRECT — single mutex protects the {state, last_error, lastAcceptedSample} triplet.
bool TrainingSession::tickTimeout(steady_clock::time_point now) {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ != SessionState::Collecting) return false;
    if (now - lastAcceptedSample_ < std::chrono::seconds(30)) return false;
    state_ = SessionState::Cancelled;
    last_error_ = "training_timed_out_no_samples";
    return true;   // caller (DeviceProvider) does the mode flip + unique_ptr reset.
}
```

**Warning signs:** UAT D-39 item 5 (orphan timeout) and item 10 (stress) catch this.

### Pitfall 4: PatternTrainer's internal sample-interval rate-limiting

**What goes wrong:** `PatternTrainer::addSample` rate-limits via `sampleInterval` (default 100 ms at `pattern_trainer.hpp:30`). Detection thread feeds blocks at WASAPI cadence (~10 ms per 480-frame block at 48 kHz). Most calls return false — the trainer is internally pacing.

**Why it happens:** v1.5 client called `addTrainingSample` from the WASAPI callback at audio-frame cadence; the rate-limit is on purpose.

**How to avoid:** **This is correct behavior — don't fight it.** The `samplesAccepted` counter increments at the rate-limited cadence; `samples_collected_` atomic for `GET /training/progress` should mirror `samplesAccepted` (not raw call count). Document in PLAN: "samples_collected reflects accepted samples after PatternTrainer's 100 ms rate-limit; client UI shows 100 samples × 100 ms = ~10 s of nominal training time."

```cpp
// driver/src/training_session.cpp
void TrainingSession::addSample(const float* samples, size_t count) {
    const bool accepted = trainer_->addSample(samples, count);
    if (accepted) {
        samples_collected_.store(trainer_->getStats().samplesAccepted,
                                  std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu_);
        lastAcceptedSample_ = std::chrono::steady_clock::now();
    }
}
```

**Warning signs:** If `GET /training/progress` shows `samples_collected` ticking at 100 Hz (the WASAPI block rate), the wiring is wrong. It should tick at ~10 Hz (the rate-limit).

### Pitfall 5: thresholds_preview has wrong shape for client UI rendering

**What goes wrong:** Client expects `{sensitivity, energy_threshold, spectral_profile_summary: {mean, stddev, size}}` (D-22), driver returns the full `spectralProfile` vector (256 floats × 100 sessions = wire bloat).

**Why it happens:** `PatternTrainer::getSpectralProfile()` returns the full vector. Naive serialization sends it all.

**How to avoid:** Compute `mean` and `stddev` over the spectralProfile vector at finalize time; send only those + size. Document in PLAN. Future TRAIN-D2 sparkline differentiator can expose the full vector when needed.

### Pitfall 6: ReplaceFileW fails on cross-volume paths

**What goes wrong:** `%APPDATA%` is on `C:\`; user's Steam install is on `D:\`. If something points the training-data path at `D:\`, ReplaceFileW fails (cross-volume rename is not atomic).

**Why it happens:** ReplaceFileW only works for same-volume swaps.

**How to avoid:** Always resolve via `SHGetFolderPathW(CSIDL_APPDATA)`, which lives on the user's `%APPDATA%` volume. Don't accept user-supplied paths for `training_data.bin`. The .tmp file lives in the same parent directory — same volume by construction. Existing `config_io.cpp::writeAtomicWindows` pattern at line 99 (`const auto tmp = dest.parent_path() / (...)`) handles this correctly. **Just lift it.**

### Pitfall 7: WAV decoder reads entire file into RAM

**What goes wrong:** `drwav_open_file_and_read_pcm_frames_f32` reads the whole file. A 10-minute stereo 48-kHz 32-bit float WAV is 230 MB. CI machine OOMs on a malicious corpus entry.

**Why it happens:** `dr_wav.h` has streaming APIs (`drwav_init_file` + `drwav_read_pcm_frames_f32` in chunks) but the convenience function loads everything.

**How to avoid:** Implement the duration cap (D-32 — default 600 s) BEFORE allocating the buffer. Compute `expected_bytes = totalPCMFrameCount * channels * 4` and reject if > some sane cap (e.g., 1 GB). Use the chunked API for large files. Or just trust the duration-cap and use the convenience function; 600 s × 48 kHz × 2 channels × 4 bytes = 230 MB is the worst case at the cap, fine for CI.

```cpp
// CORRECT — duration cap BEFORE allocating buffer:
drwav w;
if (!drwav_init_file_w(&w, path.c_str(), nullptr)) return error(2, "open failed");
const double duration_s = double(w.totalPCMFrameCount) / w.sampleRate;
if (duration_s > cfg.max_duration_s) {
    drwav_uninit(&w);
    return error(2, "exceeds --max-duration");
}
// Now safe to allocate.
```

### Pitfall 8: --replay-dir glob picks up partial-write WAVs

**What goes wrong:** Agent is mid-write of a new corpus WAV when CI runs. mic_test reads the partial file, dr_wav fails, exit code 2 propagates as build failure.

**Why it happens:** No atomicity on agent-side WAV creation; CI doesn't synchronize.

**How to avoid:** Document in `tests/corpus/replay/README.md` that agents should write `.wav.tmp` then `rename` to `.wav` (Windows: `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`). mic_test's directory glob matches `*.wav` only — `.wav.tmp` is invisible. Same pattern as `config.json` writes. Document in PLAN as part of corpus-management discipline.

## Code Examples

### Verified pattern: HTTP route handler with structured 400 envelope (extends P8)

```cpp
// Source: driver/src/http_server.cpp existing P8 patterns at :131-154
//          (POST /button) and :565-604 (PUT /settings) — same shape verbatim.

server_->Post("/training/start", [this](const httplib::Request& req, httplib::Response& res) {
    // Optional payload (currently no fields per D-11 — just for forward compat).
    if (!req.body.empty()) {
        try { auto _ = nlohmann::json::parse(req.body); }
        catch (const nlohmann::json::exception&) {
            res.status = 400;
            res.set_content(R"({"error":"malformed JSON body"})", "application/json");
            return;
        }
    }
    auto result = trainingStart_();
    if (result.status != 200) {
        res.status = result.status;
        res.set_content(result.body, "application/json");
        return;
    }
    res.set_content(R"({"status":"ok"})", "application/json");
});

server_->Post("/training/finalize", [this](const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try { body = nlohmann::json::parse(req.body); }
    catch (const nlohmann::json::exception&) {
        res.status = 400;
        res.set_content(R"({"field":"(structural)","reason":"malformed JSON body"})",
                        "application/json");
        return;
    }
    // Hand-rolled validator — extends settings_validator.cpp.
    if (auto v = validateFinalizePayload(body); v.has_value()) {
        res.status = 400;
        nlohmann::json err = {{"field", v->field}, {"reason", v->reason}};
        res.set_content(err.dump(), "application/json");
        return;
    }
    auto result = trainingFinalize_(body);
    res.status = result.status;
    res.set_content(result.body, "application/json");
});
```

### Verified pattern: PatternTrainer host inside TrainingSession (D-09 / D-22)

```cpp
// Source: src/detection/include/micmap/detection/pattern_trainer.hpp:48-137
//          (PatternTrainer's full public API)
//          + src/detection/src/pattern_trainer.cpp:104-326 (verified
//          startTraining/addSample/finishTraining bodies).

// driver/src/training_session.cpp (NEW)
TrainingSession::TrainingSession(uint32_t sampleRate, size_t fftSize) {
    auto analyzer = micmap::detection::createSpectralAnalyzer(sampleRate, fftSize);
    micmap::detection::TrainingConfig cfg;   // defaults: maxSamples=100, sampleInterval=100ms
    trainer_ = std::make_unique<micmap::detection::PatternTrainer>(
        std::move(analyzer), cfg);
    trainer_->startTraining();
    state_ = SessionState::Collecting;
    lastAcceptedSample_ = std::chrono::steady_clock::now();
}

void TrainingSession::addSample(const float* samples, size_t count) {
    const bool accepted = trainer_->addSample(samples, count);
    if (accepted) {
        samples_collected_.store(trainer_->getStats().samplesAccepted,
                                  std::memory_order_release);
        std::lock_guard<std::mutex> lk(mu_);
        lastAcceptedSample_ = std::chrono::steady_clock::now();
    }
}

std::optional<ValidationError> TrainingSession::compute() {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ != SessionState::Collecting) {
        return ValidationError{"state", "not in collecting state"};
    }
    if (trainer_->getStats().samplesAccepted < MIN_TRAINING_SAMPLES * 3) {
        return ValidationError{"samples_collected",
            "need at least " + std::to_string(MIN_TRAINING_SAMPLES * 3) + " samples"};
    }
    state_ = SessionState::Computing;
    if (!trainer_->finishTraining()) {
        state_ = SessionState::Cancelled;
        last_error_ = "compute_failed";
        return ValidationError{"trainer", "finishTraining returned false"};
    }
    // Build preview from trainer's outputs.
    const auto& profile = trainer_->getSpectralProfile();
    float sum = 0.0f, sumSq = 0.0f;
    for (float v : profile) { sum += v; sumSq += v * v; }
    const float mean = profile.empty() ? 0.0f : sum / profile.size();
    const float stddev = profile.empty() ? 0.0f
        : std::sqrt(std::max(0.0f, sumSq / profile.size() - mean * mean));
    preview_ = ThresholdsPreview{
        /*sensitivity=*/0.7f,   // mirrors v1.5 default; recompute can replace
        /*energy_threshold=*/trainer_->getEnergyThreshold(),
        {mean, stddev, profile.size()}
    };
    state_ = SessionState::Ready;
    return std::nullopt;
}
```

### Verified pattern: `dr_wav` minimal usage (D-29 / D-32)

```cpp
// Source: https://github.com/mackron/dr_libs/blob/master/dr_wav.h (Quickstart)
// (CITED via WebFetch + WebSearch 2026-05-08)

#define DR_WAV_IMPLEMENTATION   // EXACTLY ONE .cpp file in mic_test does this
#include "dr_wav.h"

#include <vector>
#include <filesystem>
#include <iostream>

bool decodeWav(const std::filesystem::path& path,
               std::vector<float>& outFrames,
               uint32_t& outSampleRate,
               uint32_t& outChannels) {
    drwav w;
#ifdef _WIN32
    if (!drwav_init_file_w(&w, path.c_str(), nullptr)) {
#else
    if (!drwav_init_file(&w, path.string().c_str(), nullptr)) {
#endif
        std::cerr << "ERROR: drwav_init_file failed: " << path.string() << "\n";
        return false;
    }

    // Bit-depth gate (D-32): only 16-bit PCM and 32-bit float pass.
    const bool ok = (w.bitsPerSample == 16
                     && w.translatedFormatTag == DR_WAVE_FORMAT_PCM)
                 || (w.bitsPerSample == 32
                     && w.translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT);
    if (!ok) {
        std::cerr << "ERROR: unsupported WAV bit depth/format: "
                  << w.bitsPerSample << "-bit, fmt=" << w.translatedFormatTag << "\n";
        drwav_uninit(&w);
        return false;
    }

    outFrames.resize(w.totalPCMFrameCount * w.channels);
    drwav_read_pcm_frames_f32(&w, w.totalPCMFrameCount, outFrames.data());
    outSampleRate = w.sampleRate;
    outChannels   = w.channels;
    drwav_uninit(&w);
    return true;
}
```

## State of the Art

| Old Approach (v1.5) | Current Approach (P9) | When Changed | Impact |
|---------------------|----------------------|--------------|--------|
| Client opens its own WASAPI capture during training (`apps/micmap/main.cpp:962-1027`) | Driver is sole mic owner; client is observer-only | P9 Wave 3 cutover | Removes the dual-owner risk at the OS level. TRAIN-AF-01 enforced by lint. |
| Client writes `training_data.bin` directly via `std::ofstream` | Driver wraps `INoiseDetector::saveTrainingData` with `ReplaceFileW` + corruption-backup | P9 Wave 1 (training_io.cpp lands) + Wave 3 (cutover) | Mirror of P8 D-07 for `config.json`. Single-writer rule. |
| No replay regression harness | `mic_test.exe --replay <wav>` + `--replay-dir` + corpus + JSON output + CI invocation | P9 Wave 4 | Agent-driven QA over user-generated WAV corpora. Deterministic; flat-out pacing. |
| Plain `std::ofstream` for training_data.bin | Atomic ReplaceFileW + corruption-backup retention | P9 D-23/D-27 | Survives mid-write process kill; existing v1.5 profile preserved on driver crash. |
| `std::atomic<DriverMode>` did not exist | New atomic-of-enum at DeviceProvider | P9 D-01 | Mode-switch architecture; no detection-thread tear-down/restart. |
| `IDriverApi` had 12 methods (after P8) | P9 adds 5 more (`startTraining`, `getTrainingProgress`, `finalizeTraining`, `cancelTraining`, `recomputeTraining`) | P9 Wave 2 | Full training-side IPC surface complete after P9. |

**Deprecated/outdated:**
- v1.5 client-side `detector->startTraining/addTrainingSample/finishTraining/saveTrainingData` body at `apps/micmap/main.cpp:962-1027` — **deleted in P9 Wave 3**. Lint (`AssertNoClientTraining.cmake`) prevents reintroduction.
- v1.5 `detector->saveTrainingData(...)` call sites at `apps/micmap/main.cpp:618` (on quit) and `apps/micmap/main.cpp:991` (on training stop) — **deleted in P9 Wave 3**.
- Note: `apps/micmap/main.cpp:300` (`detector->loadTrainingData(...)` startup load for client-side detection) survives P9 unchanged. P10 deletes when client-side detection is removed.
- Note: `apps/mic_test/main.cpp:783, :808` (mic_test's `saveTrainingData` / `loadTrainingData`) survive **forever** — TEST-01 invariant + dev workflow. Allowlisted by `AssertNoClientTraining.cmake` (D-06).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `dr_wav.h` 0.14.6 supports both 16-bit PCM and IEEE 32-bit float decoding via `drwav_read_pcm_frames_f32`. | Standard Stack / Code Examples | LOW — verified via WebFetch + WebSearch. License + version + API surface confirmed against the official GitHub raw header. |
| A2 | `INoiseDetector::saveTrainingData(path)` writes to the path argument verbatim (no path mangling, no extension change). | Pattern 3 / training_io.cpp lift | LOW — verified by reading `noise_detector.cpp:329-374`. The function does `std::ofstream file(path, std::ios::binary); file.write(header); file.write(profile.data(), ...)`. Direct path usage. Safe to pass `.tmp` path then ReplaceFileW. |
| A3 | `IStateMachine::update(confidence, dt)` is purely deterministic — no internal `steady_clock` calls. | Pattern 4 / replay determinism | LOW — verified by grep at `src/core/src/state_machine.cpp:34-35`: `void update(...) { timeInState_ += deltaTime; ... }`. The grep on the whole `src/core/src` directory found only one `steady_clock` call site and it's NOT in the cooldown logic. |
| A4 | `PatternTrainer::Impl::spectra` (the per-sample FFT vector storage) survives across `addSample` / `finishTraining` calls and can be reused for recompute without re-collecting. | Pattern 2 / Recompute D-17 | MEDIUM — verified by reading `pattern_trainer.cpp:32 (spectra: vector<vector<float>>)` and `cancelTraining` at `:278-290` (which clears spectra). However, `finishTraining` at `:207-276` does NOT clear spectra. So **after `finishTraining` returns**, `impl_->spectra` is intact and recompute can re-call into the impl. **PLAN must verify** by reading `pattern_trainer.cpp` once more during planning OR by adding a `recomputeWithSensitivity()` method to `PatternTrainer` rather than re-calling `finishTraining()` (cleaner API). |
| A5 | `PatternTrainer::TrainingConfig::maxSamples = 100` is the right target sample count for client UI progress bar denominator. | D-11 / Pattern 2 | LOW — D-11 explicitly locks this at 100. Document in PLAN.md as the denominator for the progress bar. |
| A6 | The 30 s collect-window timeout (D-12) is long enough for normal user thinking pauses but short enough to recover from orphaned client. | Pitfall 3 / D-12 | MEDIUM — D-12 explicitly says "Claude's discretion; planner can tune." UAT D-39 item 5 is the empirical validation. PLAN should accept the 30 s value and let UAT confirm. |
| A7 | `dr_wav.h` license is "public domain or MIT-0 (choose)" — no GPL ladder, no attribution-required clause. | Standard Stack / Don't Hand-Roll | LOW — confirmed via WebFetch + WebSearch. `mackron/dr_libs` uses this license for all its single-header libs. Used by raylib, FAudio, raudio, kbd-audio, RJModules — all production projects. |
| A8 | `apps/mic_test/main.cpp` is currently a Win32 GUI (uses `WIN32` subsystem, message pump, OPENFILENAMEW dialogs). The replay harness extension can either coexist with the GUI (CLI args bypass UI) or fork a separate `--headless` mode. | D-29 / replay harness | MEDIUM — verified via Read of `apps/mic_test/main.cpp:1-100` and `:770-825`. The current entry point is `WinMain` (Win32 subsystem). Adding `--replay <wav>` args means the planner must decide: (a) parse CLI args before WinMain enters the message pump and exit if `--replay` present; (b) split into `mic_test_gui.exe` + `mic_test.exe` (CLI-only). **PLAN must address this in Wave 4.** Recommend (a) — minimal restructure, parse argc/argv inside WinMain prologue, branch to `int replayMain(int, char**)` if CLI args present. |
| A9 | The driver's `INoiseDetector` instance, after a successful `POST /training/finalize`, can swap its in-memory profile via either (a) a fresh `loadTrainingData(path)` call after the file is written, or (b) a yet-to-be-exposed `setSpectralProfile` method. | D-24 / Recompute commit | MEDIUM — D-24 says "Claude's discretion; recommend the direct in-memory swap because the file just got written." Reading `noise_detector.hpp:107-138` shows no `setSpectralProfile` method — only `setSensitivity` and `setMinDetectionDuration`. **Option (a) is the only currently-available path.** Document in PLAN: after finalize writes the file, call `detector_->loadTrainingData(path)` to round-trip the profile. ~10 ms overhead; well-bounded for an HTTP handler. |
| A10 | The replay harness can compute `linearResample(mono, src_rate, target_rate)` deterministically without floating-point drift on different CPUs. | Pattern 4 / replay determinism | MEDIUM — linear interpolation is deterministic in IEEE-754 if the formula is `out[i] = a + (b-a) * frac`. PLAN must verify the resampler implementation uses no `cmath` functions that vary across platforms (e.g., `sin` for FIR filters). Stick to multiply-add only. |

**Summary:** 10 assumptions logged. **A4 (PatternTrainer::spectra survives finishTraining)**, **A8 (mic_test current shape)**, **A9 (no setSpectralProfile)** are the three to confirm during planning. The rest are LOW risk and well-precedented.

## Open Questions

1. **Recompute API: re-call `PatternTrainer::finishTraining()` vs add new method?**
   - What we know: `PatternTrainer::Impl` retains `spectra`, `energies`, `spectralFlatnesses` after `finishTraining()` returns (verified by reading `pattern_trainer.cpp:207-276` — none of these are cleared in finishTraining; only cancelTraining at :278 clears them).
   - What's unclear: Does re-calling `finishTraining()` after the first call work cleanly? It sets `impl_->training = false` at :213 then early-returns at :209 (`if (!impl_->training) return false;`). So **the existing API does NOT support recompute via re-finishTraining**.
   - Recommendation: Add a method `bool PatternTrainer::recomputeProfile(float sensitivity)` that runs the threshold-derivation block (`pattern_trainer.cpp:230-263`) over the cached spectra without resetting `impl_->training`. **OR** wrap the threshold-derivation logic into a static helper that both `finishTraining` and TrainingSession can call. Either way, **modifies `pattern_trainer.{hpp,cpp}` in P9** — document in Wave 1 plan as a dependency. Lint impact: zero (header is in `micmap_core_runtime`; no new includes).

2. **mic_test.exe entry point: CLI-args branch vs separate binary?**
   - What we know: Current mic_test is `WIN32` subsystem with `WinMain` and OPENFILENAMEW dialogs (verified `apps/mic_test/main.cpp:1-30, :770-825`).
   - What's unclear: Adding `--replay <wav>` to a Win32-subsystem binary works but is awkward (no stdout by default). The CONSOLE subsystem gives clean stdout.
   - Recommendation: **Option (a)** — keep mic_test as WIN32 but allocate a console with `AllocConsole()` if `argc > 1` and any of the recognized `--replay*` flags are present. Pipe stdout/stderr to the console. Branch to `replayMain(argc, argv)` and exit before entering the GUI message pump. Minimal restructure; preserves the existing GUI workflow for non-replay use.
   - Alternative: introduce a new `mic_test_replay.exe` (CONSOLE subsystem) target sharing `wav_replay.cpp`. Cleaner separation; bigger CMake diff. **Don't recommend** unless the GUI path becomes hostile.

3. **`DriverMode` atomic owner: DeviceProvider or DetectionRunner?**
   - What we know: D-01 says "Claude's discretion; recommend `DeviceProvider`."
   - What's unclear: The HTTP handlers need access to the atomic; DeviceProvider already exposes getters/setters for similar atomic snapshots (`stateSnapshot_`, `configSnapshot_` per P8 D-23). DetectionRunner is the consumer; making it the owner means DetectionRunner exports a public mutator.
   - Recommendation: **DeviceProvider owns; DetectionRunner takes a `std::function<DriverMode()>` getter callback at construction** (mirrors P7 D-09 `driverDetectionActiveGetter` shape exactly). Smallest diff; matches existing pattern. PLAN locks this in Wave 1.

4. **Watchdog tick mechanism: HTTP-thread-on-poll vs RunFrame-driven vs new thread?**
   - What we know: D-12 says "Claude's discretion." Existing detection thread runs at WASAPI cadence; HTTP server is request-driven.
   - What's unclear: Where does `tickTimeout` get called from?
   - Recommendation: **Call `tickTimeout(now)` from inside `GET /training/progress` handler** before computing the snapshot. Client polls at 5–10 Hz, so timeout-detection latency is bounded by 100–200 ms (well within the 30 s window). No new thread, no RunFrame coupling. **Edge case:** if no client polls (e.g., client crashed silently), timeout never fires. Mitigation: piggyback a tick on `GET /health` (which client polls at 1 Hz from v1.5/P7); add a single `if (mode_==Training) trainingSession_->tickTimeout(now)` in the /health handler. Two callsites; both cheap.

5. **CI corpus: where do the seed WAVs come from?**
   - What we know: D-35 says "ship initial corpus seed under `tests/corpus/replay/` with 3 minimum files."
   - What's unclear: Generating `positive_001.wav` (mic-cover-like white noise burst) is straightforward (programmatic). `negative_speech_001.wav` requires either royalty-free audio or self-recorded (privacy concern).
   - Recommendation: **Generate all three seeds programmatically.** A 50-line Python script with `numpy` + `scipy.io.wavfile` produces synthetic white noise (positive), silence (negative), and pink-noise-with-formant-modulation (synthetic "speech-like" negative — won't match a trained mic-cover profile, doesn't trigger). Reproducible; no copyright issues. Document the generation script in `tests/corpus/replay/README.md`. PLAN ships the WAVs + the generator script.

## Environment Availability

> P9 phase has external dependencies (CMake build of `dr_wav.h`, presence of `OpenVR SDK` for non-test driver builds, Windows SDK for `ReplaceFileW`).

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Windows SDK (ReplaceFileW, SHGetFolderPathW, MoveFileExW) | training_io.cpp | ✓ (verified by P8 config_io.cpp working) | Windows 11 Pro 26200 build | None — Windows-only milestone (CLAUDE.md). |
| CMake 3.21+ (FetchContent) | external/CMakeLists.txt for dr_wav (if vendored via FetchContent) | ✓ (existing project uses FetchContent for nlohmann_json, kissfft, cpp_httplib, imgui) | — | Direct vendoring `vendor/dr_wav/dr_wav.h` (no FetchContent — recommended). |
| OpenVR SDK | driver build (existing) | ✓ (P5–P8 build green) | — | mic_test.exe is OpenVR-independent (TEST-01); replay harness builds with `-DMICMAP_BUILD_DRIVER=OFF`. |
| `dr_wav.h` (mackron/dr_libs) | mic_test replay harness | ✗ (not in tree yet) | 0.14.6 | None blocking — vendor in P9 Wave 4. Direct-commit to `vendor/dr_wav/` recommended. |
| nlohmann/json v3.11.2 | http_server new training routes | ✓ (existing P8) | 3.11.2 | None — locked stack. |
| cpp-httplib v0.20.1 | driver_api new IDriverApi methods | ✓ (existing P8 D-05 bump) | 0.20.1 | None — locked stack. |
| Bigscreen Beyond + Win11 Pro hardware | UAT D-39 (mandatory) | ✓ (existing rig from P6/P7/P8) | — | None — visual hardware validation is mandatory per CLAUDE.md. |
| Python (for corpus seed generation) | One-time tooling for `tests/corpus/replay/` | likely ✓ (CI image typically has Python; confirm during Wave 4 planning) | 3.x | If not available: hand-author WAVs via a tiny C++ tool that writes RIFF directly (~50 LoC). Recommend just using Python — easier. |

**Missing dependencies with no fallback:** none.

**Missing dependencies with fallback:** `dr_wav.h` is the only new dep; vendoring via direct-commit is the standard project pattern (zero new build complexity). Python for corpus generation is one-time; C++ fallback exists if needed.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Plain-main exit-code convention with `MM_CHECK` macro (project-standard since v1.5; **NOT GoogleTest** even though `MICMAP_USE_GTEST=ON` exists at `tests/CMakeLists.txt:8`). [VERIFIED: `tests/driver/audio_worker_lifecycle_headless.cpp:33-35`, `tests/driver/detection_settings_propagation_test.cpp:30-76`, P7 PATTERNS.md Pattern F.] |
| Config file | `tests/CMakeLists.txt` (CTest registrations) |
| Quick run command | `ctest --test-dir build -R '^Training\|WavReplay\|TrainingEndpoint' --output-on-failure` (Wave 0 scaffolds; expand naming as plans land) |
| Full suite command | `ctest --test-dir build --output-on-failure` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TRAIN-01 | `POST /training/start` flips mode atomic; constructs TrainingSession | unit (driver, headless) | `ctest -R TrainingSessionLifecycle` | ❌ Wave 0 |
| TRAIN-01 | TRAIN-AF-01 enforced — no `addTrainingSample` etc. in `apps/micmap/` | lint (CMake script) | `ctest -R AssertNoClientTraining` | ❌ Wave 0 |
| TRAIN-02 | `GET /training/progress` returns shape per D-22 | unit (driver, headless) | `ctest -R TrainingEndpointValidation` | ❌ Wave 0 |
| TRAIN-03 | `POST /training/finalize` writes file atomically; returns to detection mode | unit + integration (driver, headless) | `ctest -R TrainingFinalizePersist` | ❌ Wave 0 |
| TRAIN-04 | `POST /training/cancel` is idempotent; samples discarded; file unmodified | unit (driver, headless) | `ctest -R TrainingCancelIdempotent` | ❌ Wave 0 |
| TRAIN-05 | TRAIN-AF-01 lint (same as TRAIN-01) | lint | `ctest -R AssertNoClientTraining` | ❌ Wave 0 |
| TRAIN-06 | `POST /training/recompute` re-derives thresholds; doesn't write file | unit (driver, headless) | `ctest -R TrainingRecomputePreview` | ❌ Wave 0 |
| TEST-04 | `mic_test --replay <wav>` decodes WAV, feeds detector, emits triggers | unit (mic_test, headless) | `ctest -R WavReplayDecoder` | ❌ Wave 0 |
| TEST-04 | `mic_test --replay-dir` + `--expect-triggers-from manifest.json` | integration (CI corpus) | `ctest -R MicTestReplayCorpus` | ❌ Wave 4 |
| TEST-04 | Replay determinism — same input → byte-identical JSON output across 3 runs | integration (CI corpus) | `ctest -R MicTestReplayDeterminism` | ❌ Wave 4 |
| TEST-04 | Replay harness has no OpenVR dependency | lint | `ctest -R AssertReplayNoVrApi` | ❌ Wave 0 |
| IPC-06 | Driver is sole writer of `training_data.bin` (lint forbids client write paths) | lint | `ctest -R AssertNoClientTraining` (covers IPC-06 since lint regex matches `saveTrainingData`) | ❌ Wave 0 |
| IPC-06 | Atomic ReplaceFileW write (no torn writes on process kill mid-write) | unit (driver, headless) | `ctest -R TrainingIoAtomicPersist` | ❌ Wave 0 |
| (cross-cutting) | DriverMode atomic flip propagates within one detection-loop iteration (< 50 ms) | unit (driver, headless) | `ctest -R DriverModePropagation` | ❌ Wave 0 |
| (cross-cutting) | 50-cycle Init/Cleanup stress with mode-flip iterations leaks no handles | stress (driver, headless) | `ctest -R DeviceProviderTrainingStress` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --test-dir build --output-on-failure` (~30 s on the existing rig with full suite).
- **Per wave merge:** Full suite green + manual D-39 spot-check of the wave's UAT items (e.g., Wave 3 spot-checks D-39 items 1, 2, 7).
- **Phase gate:** Full UAT D-39 (10 items, mandatory) on Bigscreen Beyond + Win11 Pro before `/gsd-verify-work`.

### Wave 0 Gaps

- [ ] `tests/driver/training_session_test.cpp` — covers TRAIN-01 (start), TRAIN-04 (cancel idempotency), TRAIN-06 (recompute preview), 30 s timeout (D-12), single-instance mutex (D-09).
- [ ] `tests/driver/training_endpoint_validation_test.cpp` — covers TRAIN-02 (progress shape), TRAIN-03 (finalize payload), validation envelope (D-15/D-16/D-22 D-32 boundaries).
- [ ] `tests/driver/training_finalize_persist_test.cpp` — covers TRAIN-03 (atomic write + corruption-backup retention).
- [ ] `tests/driver/training_io_atomic_persist_test.cpp` — covers IPC-06 (sole writer + ReplaceFileW + .tmp cleanup on failure).
- [ ] `tests/driver/driver_mode_propagation_test.cpp` — covers cross-cutting mode-flip latency invariant.
- [ ] `tests/driver/device_provider_training_stress_test.cpp` — covers 50-cycle Init/Cleanup with mode flips (extends P7 stress test).
- [ ] `tests/mic_test/wav_replay_test.cpp` — covers TEST-04 (decoder + downmix + resample + JSON output schema).
- [ ] `tests/corpus/replay/positive_001.wav` + `negative_silence_001.wav` + `negative_speech_001.wav` + `manifest.json` + `README.md` — corpus seed (D-35).
- [ ] `cmake/AssertNoClientTraining.cmake` (skip-on-NOT-EXISTS in Wave 0; enforcing in Wave 3) — covers TRAIN-AF-01 / IPC-06.
- [ ] `cmake/AssertReplayNoVrApi.cmake` (skip-on-NOT-EXISTS in Wave 0; enforcing in Wave 4) — covers TEST-01 invariant for new replay TUs.
- [ ] CTest registrations EXISTS-gated for all new tests in `tests/CMakeLists.txt` (mirror P7 D-25 / P8 Wave 0 RED-tolerant pattern).
- [ ] Framework install: none — plain-main convention requires no new install.

## Security Domain

> `security_enforcement` is enabled by default unless explicitly disabled. Project does not have an explicit disable flag, so include this section.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | Localhost-only HTTP server (IPC-07); same-machine same-user trust model. No auth required for `127.0.0.1` binding (matches P8). |
| V3 Session Management | no | Stateless HTTP API; no sessions, no cookies, no tokens. Each request is independent. |
| V4 Access Control | yes | Driver enforces `enable_driver_audio=true` gate on `POST /training/start` (returns 503 if false — D-40). No further authorization (single-user, single-machine). |
| V5 Input Validation | yes | Hand-rolled per-field validators in `driver/src/settings_validator.cpp` (extending P8 pattern). All-or-nothing first-failed-field. Range checks on sensitivity (0.0–1.0), threshold, min-duration, cooldown. JSON parse-fail returns 400 with structured envelope. |
| V6 Cryptography | no | No secrets, no signatures, no encryption. `training_data.bin` is plain binary (header + float vector). |

### Known Threat Patterns for {C++17 / cpp-httplib / WASAPI / nlohmann/json / dr_wav}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| HTTP route DoS via large request body | Denial of Service | cpp-httplib has `set_payload_max_length` (default ~8 MB). Verify in PLAN — current default is fine for training endpoints (payloads are small JSON). |
| JSON deserialization stack overflow on deeply-nested input | Denial of Service | nlohmann/json v3.11.2 has built-in depth limit; relevant CVEs (e.g., CVE-2022-21800 in older versions) addressed. v3.11.2 release notes confirm safe defaults. [CITED: nlohmann/json release notes]. |
| WAV file format fuzz attack on dr_wav | Denial of Service / Tampering | dr_wav has known fuzzer-found CVEs in older versions. v0.14.x has been fuzzed via OSS-Fuzz; integer-overflow and out-of-bounds reads are the historical class. **Mitigation:** apply duration-cap (D-32, default 600 s) + bit-depth gate BEFORE allocating buffers. Don't trust adversarial WAVs in CI corpus — corpus is curated, not arbitrary user input. Replay harness is dev-only; not exposed via network. |
| ReplaceFileW symlink-redirect attack | Tampering | Path is resolved via `SHGetFolderPathW(CSIDL_APPDATA)` — cannot be redirected by HTTP-supplied paths. `%APPDATA%` is per-user, ACL-protected. Same threat model as P8 `config.json`. |
| training_data.bin tampering by another process | Tampering | Out of scope. Same-machine same-user trust model; if the user's account is compromised, the training file is the least of their concerns. |
| Buffer overflow on incoming HTTP body | Tampering / RCE | nlohmann/json + cpp-httplib are well-fuzzed. v0.20.1 of cpp-httplib addresses CVE-2025-46728. P9 doesn't introduce new HTTP parsing — reuses P8 patterns. |
| WASAPI capture from unauthorized process | Information Disclosure | Driver binary is signed (existing); requires admin to install (existing). Mic permission is per-Windows-account (existing). P9 doesn't widen this surface. |
| Recompute endpoint replay attack | Tampering | No persistent state to replay against; each session starts fresh. Recompute only valid in `ready` state of an active session. After finalize, session is destroyed. |

**Phase 9 security posture summary:** P9 inherits P8's threat model verbatim. New surface area is the WAV decoder (only reachable via `mic_test.exe` CLI — not network-exposed) and the 5 new HTTP endpoints (localhost-only, structured input validation, hand-rolled per-field validators). No new secrets, no new auth surface, no new file-write paths beyond the controlled `training_data.bin`. **No incremental security work needed beyond following the existing P8 patterns.** Document in PLAN that the `mic_test --replay` corpus must be curated (not user-controlled fuzzer input) for the dr_wav threat model to remain LOW.

## Sources

### Primary (HIGH confidence — in-tree reads, verbatim verification)

- `.planning/phases/09-training-migration/09-CONTEXT.md` — 40 D-decisions covering full phase scope; read in full.
- `.planning/REQUIREMENTS.md` — TRAIN-01..06, TEST-04, IPC-06 verbatim.
- `.planning/STATE.md` — phase 9 NEEDS VALIDATION flag, blocker list, Phase 6/7/8 outcomes.
- `.planning/ROADMAP.md` — phase 9 goal, depends-on chain, success criteria.
- `.planning/phases/07-driver-side-detection-thread/07-PATTERNS.md` — DetectionRunner pattern map; atomic-snapshot publish/load discipline.
- `.planning/phases/08-ipc-contract-reshape/08-PATTERNS.md` — http_server route patterns, settings_validator shape, atomic-cutover protocol, RED-tolerant Wave 0 conventions.
- `src/detection/include/micmap/detection/pattern_trainer.hpp` — full PatternTrainer interface.
- `src/detection/src/pattern_trainer.cpp` — verified: `Impl::spectra` survives `finishTraining()` (NOT cleared, only cleared in `cancelTraining()`).
- `src/detection/include/micmap/detection/noise_detector.hpp` — `INoiseDetector::saveTrainingData` / `loadTrainingData` API.
- `src/detection/src/noise_detector.cpp:329-437` — verified binary file format (magic + version + spectralProfile + thresholds).
- `src/core/include/micmap/core/state_machine.hpp` — `IStateMachine::update(confidence, dt)` signature.
- `src/core/src/state_machine.cpp:34-35` — verified `update` is dt-pure (no internal `steady_clock` calls).
- `driver/src/detection_runner.{hpp,cpp}` — atomic mode read pattern, sample-count dt at `:466-471`.
- `driver/src/http_server.{hpp,cpp}` — ctor evolution pattern, route handler shape.
- `driver/src/config_io.cpp` — verified ReplaceFileW + corruption-backup helpers (lift-and-modify source for training_io.cpp).
- `driver/src/settings_validator.hpp` — extension target.
- `src/steamvr/include/micmap/steamvr/driver_api.hpp` — IDriverApi extension target.
- `apps/micmap/main.cpp:580-1027` — verified deletion targets (`:618`, `:962-1027`, `:991`).
- `apps/mic_test/main.cpp:1-100, :770-825` — verified Win32 GUI subsystem, training save/load call sites, allowlist scope.
- `external/CMakeLists.txt` — FetchContent vendoring pattern.

### Secondary (MEDIUM confidence — verified via WebFetch + WebSearch)

- `dr_wav.h` 0.14.6 license + version + API surface — [WebFetch: https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h] + [WebSearch: "dr_wav.h mackron public domain MIT-0 license github"]. License: public domain or MIT-0 choice. Used by raylib, FAudio, raudio, kbd-audio, RJModules. Single-header, zero deps.
- `cpp-httplib` v0.20.1 CVE-2025-46728 mitigation — [P8 D-05 + P8 RESEARCH.md citations].
- nlohmann/json v3.11.2 safe defaults — release notes referenced via P8 patterns; no fresh CVE check needed (project-locked version).

### Tertiary (LOW confidence — none flagged for validation)

None. All claims in this research either reference in-tree code reads or external sources verified via WebFetch/WebSearch.

## Metadata

**Confidence breakdown:**
- Standard stack: **HIGH** — every component is either in-tree (P5–P8) or single-header public-domain with verified API surface.
- Architecture: **HIGH** — every architectural pattern is precedented (mode atomic ≈ P7 D-15; HTTP route shape ≈ P8 D-23; ReplaceFileW ≈ P8 D-14; lint-go-live ≈ P8 D-07).
- Pitfalls: **HIGH** — TrainingSession lifecycle race + dr_wav fuzzing + ReplaceFileW symlink + state machine determinism all have explicit mitigations grounded in verified code reads.
- WAV replay harness: **MEDIUM** — `dr_wav.h` API verified via WebFetch but not exercised in tree yet; mic_test entry-point restructure (Open Q2) needs planner decision; corpus generation script (Open Q5) needs Python availability check during Wave 4.

**Research date:** 2026-05-08
**Valid until:** 2026-06-08 (30 days for stable stack; longer if corpus generation discoveries surface).

---

*Phase: 09-training-migration*
*Research completed: 2026-05-08*
