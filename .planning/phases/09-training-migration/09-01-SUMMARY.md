---
phase: 09-training-migration
plan: 01
subsystem: driver-training-core
tags: [phase-9, training-migration, wave-1, training-session, mode-switch, atomic-write, ipc-06, train-01, train-02, train-03, train-04, train-06]
requires: ["09-00"]
provides:
  - "TrainingSession class (Collecting → Computing → Ready → Finalized FSM with Cancelled sibling) hosting a single std::unique_ptr<INoiseDetector>"
  - "training_io::saveTrainingFile atomic ReplaceFileW write of %APPDATA%\\MicMap\\training_data.bin (5-generation corruption-backup ring helper shipped for future load-side use)"
  - "DriverMode atomic + lazy unique_ptr<TrainingSession> + tryStartTrainingSession/resetTrainingSession on DeviceProvider"
  - "DetectionRunner per-iteration DriverMode acquire-load + Detecting/Training branch in RunLoop"
  - "driver/src/driver_mode.hpp single-purpose enum header (avoids circular include between detection_runner.hpp and device_provider.hpp)"
affects:
  - "driver_micmap.dll source set (training_session.cpp + training_io.cpp added; +5 KB binary footprint)"
  - "DetectionRunner ctor signature gains an optional 6th DeviceProvider* parameter (default nullptr keeps existing 4-arg test ctors compiling unchanged)"
  - "All P7/P8 driver-tests' source lists (training_session.cpp + training_io.cpp must accompany detection_runner.cpp + device_provider.cpp at link time — Rule 3 blocking deviation)"
tech-stack:
  added: []
  patterns:
    - "Atomic-snapshot publish/load (mirrors detection_runner.cpp:101 activeConfig_ release/acquire pattern)"
    - "ReplaceFileW + corruption-backup retention (mirrors driver/src/config_io.cpp shape with binary-detector deviation)"
    - "v1.5 detector-as-trainer idiom verbatim port (apps/micmap/main.cpp:298 + :1006 + :404 + :974 — deleted in 09-03 but preserved in git history)"
key-files:
  created:
    - "driver/src/training_session.hpp"
    - "driver/src/training_session.cpp"
    - "driver/src/training_io.hpp"
    - "driver/src/training_io.cpp"
    - "driver/src/driver_mode.hpp"
  modified:
    - "driver/src/detection_runner.hpp"
    - "driver/src/detection_runner.cpp"
    - "driver/src/device_provider.hpp"
    - "driver/src/device_provider.cpp"
    - "driver/CMakeLists.txt"
    - "tests/CMakeLists.txt"
decisions:
  - "TrainingSession hosts ONE std::unique_ptr<INoiseDetector> for the entire session lifecycle — verbatim port of v1.5 client idiom; no PatternTrainer host, no save→load round-trip on recompute (D-09, D-20)"
  - "DriverMode lookup wired via raw-pointer DeviceProvider* on DetectionRunner (default nullptr) rather than introducing a separate atomic-shared accessor — produces the smallest diff against P7 ctor signature"
  - "DeviceProvider::trainingSession() accessor defined INLINE in device_provider.hpp so DetectionRunner-only test exes link without dragging device_provider.cpp + httplib/bindings/etc into their source list (forward-declared TrainingSession is sufficient for unique_ptr::get())"
  - "backupAndRotate helper shipped but NOT called from saveTrainingFile success path (Rule 1 deviation from 09-01-PLAN.md Task 1 — calling it would destroy the just-written file; helper is reserved for the future load-side corruption handler, mirror of config_io.cpp invocation discipline)"
  - "tests/CMakeLists.txt expansion across 9 test target source lists (Rule 3 blocking — TrainingSession symbols are referenced from device_provider.cpp + detection_runner.cpp now; tests linking those TUs need training_session.cpp + training_io.cpp added)"
metrics:
  duration: "~18 minutes"
  completed: 2026-05-08
  tasks: 4
  commits: 4
---

# Phase 9 Plan 01: Driver Training Core Summary

Driver-side training core landed: TrainingSession class, atomic training_data.bin I/O, and DriverMode-aware DetectionRunner branching — all flowing through a single `std::unique_ptr<INoiseDetector>` per session per the verified v1.5 client idiom port.

## What Shipped

### `driver/src/training_session.{hpp,cpp}` (NEW)

Driver-side training-session state owner. Hosts a single `std::unique_ptr<INoiseDetector>` constructed in the ctor via the verified factory `micmap::detection::createFFTDetector(sampleRate, fftSize)` (analog at apps/micmap/main.cpp:298 + driver/src/detection_runner.cpp:102). All training, recompute, and persistence operations flow through that one detector instance — verbatim port of v1.5.

**Public surface:**
- `TrainingSession(uint32_t sampleRate, size_t fftSize)` — ctor calls `detector_->startTraining()` (v1.5 idiom at apps/micmap/main.cpp:1006)
- `bool addSample(const float* samples, size_t count)` — hot-path; called from DetectionRunner thread when `DriverMode == Training`. Forwards to `detector_->addTrainingSample` (v1.5 idiom at apps/micmap/main.cpp:404); increments `samples_collected_` atomic; stamps `lastAcceptedSample_`.
- `std::optional<std::string> compute()` — Collecting → Computing → Ready transition via `detector_->finishTraining()`; builds preview from `detector_->getTrainingData()` (mean / stddev / size summary of FFT bins).
- `bool recompute(float sensitivity)` — Ready-only; calls `detector_->setSensitivity()` directly per D-20 (energy_threshold + spectral_profile stay constant — sample-derived, not sensitivity-derived in v1.5 algorithm).
- `ProgressSnapshot snapshot() const` — lock-free `samples_collected` (atomic acquire-load); mu_-locked for state / preview / last_error.
- `bool tickTimeout(now)` — 30 s no-new-accepted-sample watchdog (D-12); transitions Collecting → Cancelled with `last_error="training_timed_out_no_samples"`.
- `void cancel()` — idempotent; safe on any non-Finalized state (D-13).
- `void markFinalized()` — invoked by 09-02 finalize handler after disk write succeeds.
- `INoiseDetector* detector()` — accessor for the finalize handler (passes to `training_io::saveTrainingFile`).

**ZERO OpenVR API surface** in this TU — no `<openvr*>` includes, no `vr::*` references.

### `driver/src/training_io.{hpp,cpp}` (NEW)

Atomic ReplaceFileW write of `%APPDATA%\MicMap\training_data.bin`.

- `getDriverTrainingDataPath()` — resolves the AppData path via SHGetFolderPathW (mirror of `getDriverConfigPath`).
- `saveTrainingFile(path, INoiseDetector&)` — asks the detector to serialize directly to a sibling `.tmp` (preserves v1.5 binary format byte-for-byte via the existing ofstream code at `src/detection/src/noise_detector.cpp:329`); ReplaceFileW(REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS) swaps tmp over dest atomically; MoveFileExW(MOVEFILE_REPLACE_EXISTING) fallback on rename failure.
- `backupAndRotate` (anonymous namespace) — shipped as future infrastructure for the load-side corruption handler; 5-generation `training_data.bin.corrupted.<ts>` retention ring; **NOT** invoked from save path (would destroy the just-written file).

ZERO JSON dependency in this TU (training_data.bin is binary).

### `driver/src/driver_mode.hpp` (NEW)

Single-purpose header: `enum class DriverMode : uint8_t { Detecting, Training }`. Lives outside both `detection_runner.hpp` and `device_provider.hpp` to avoid the circular include (device_provider.hpp already includes detection_runner.hpp).

### `driver/src/detection_runner.{hpp,cpp}` (MODIFIED)

- **Header:** added `class DeviceProvider;` forward decl, `#include "driver_mode.hpp"`, optional 6th ctor parameter `DeviceProvider* deviceProvider = nullptr`, raw-pointer member `deviceProvider_{nullptr}`.
- **Implementation:** added `#include "device_provider.hpp"` + `#include "training_session.hpp"`, threaded the new ctor parameter through, modified RunLoop body at the per-iteration ring-drain to:
  - Read `deviceProvider_->mode().load(std::memory_order_acquire)` once per drain cycle (mirrors line 101 activeConfig_ pattern).
  - `DriverMode::Detecting` branch — `detector_->analyze + state_machine update` verbatim P7 path (unchanged).
  - `DriverMode::Training` branch — null-checks `deviceProvider_->trainingSession()`, calls `session->addSample(block.data(), block_count)` if non-null. Drops the block on transient null read (Pitfall 2 mitigation).
- All P7 invariants preserved: Pause/Resume/Start/Stop/EnterStandby/LeaveStandby/applyConfig/trigger callback install/reverse-order teardown all unchanged.

### `driver/src/device_provider.{hpp,cpp}` (MODIFIED)

- **Header:** added `#include "driver_mode.hpp"` + `<mutex>`, forward-declared `class TrainingSession`, added public `mode()` / `trainingSession()` (inline) / `tryStartTrainingSession()` / `resetTrainingSession()` accessors, added private `mode_{Detecting}` atomic + lazy `unique_ptr<TrainingSession> trainingSession_` + `trainingMutex_`.
- **Implementation:** added `#include "training_session.hpp"`, threaded `this` as the new DeviceProvider* parameter into the DetectionRunner ctor call, added `resetTrainingSession()` at the start of Cleanup as defense-in-depth, implemented `tryStartTrainingSession` (Pitfall 1 — release-store mode_=Training AFTER ctor) and `resetTrainingSession` (Pitfall 2 — release-store mode_=Detecting BEFORE reset; destruction outside the mutex to avoid blocking concurrent readers on detector teardown).

### `driver/CMakeLists.txt` (MODIFIED)

Added `training_session.cpp` + `training_io.cpp` to the `driver_micmap` source list.

### `tests/CMakeLists.txt` (MODIFIED — Rule 3 blocking deviation)

Added `training_session.cpp` + `training_io.cpp` to **9 test-target source lists** that pull `device_provider.cpp` or `detection_runner.cpp` directly:
- `test_audio_worker_lifecycle_headless` (only training_session needed; no device_provider)
- `test_detection_settings_propagation`
- `test_device_provider_lifecycle_stress`
- `test_get_state_shape`
- `test_get_telemetry_level`
- `test_get_devices_cache`
- `test_get_settings_shape`
- `test_put_settings_round_trip`
- `test_put_settings_validation`
- `test_put_settings_stress100`
- `test_state_clear_error`

## Verification

| Gate                                                               | Status  |
| ------------------------------------------------------------------ | ------- |
| `driver/src/training_io.{hpp,cpp}` exist, ReplaceFileW + backup ring | ✅ PASS |
| `driver/src/training_session.{hpp,cpp}` exist, FSM + 30 s timeout    | ✅ PASS |
| `driver/src/driver_mode.hpp` exists with DriverMode enum             | ✅ PASS |
| RunLoop branches on `mode == DriverMode::Detecting`                  | ✅ PASS |
| DeviceProvider gains atomic + lazy unique_ptr + accessors            | ✅ PASS |
| `driver/CMakeLists.txt` lists training_session.cpp + training_io.cpp | ✅ PASS |
| driver_micmap.dll compiles cleanly                                   | ✅ PASS |
| AssertDetectionRunnerNoVrApi lint GREEN (3 files scanned)            | ✅ PASS |
| All P7/P8 test exes compile + link                                   | ✅ PASS |
| ZERO OpenVR symbols in training_session.{hpp,cpp}                    | ✅ PASS |

## Deviations from Plan

### Rule 1 — Bug fix

**1. `backupAndRotate(dest)` removed from `saveTrainingFile` success path**

- **Found during:** Task 1 implementation
- **Issue:** `09-01-PLAN.md` Task 1 action explicitly directed `backupAndRotate(dest); return true;` after a successful ReplaceFileW. But `backupAndRotate` RENAMES `dest` to `dest.corrupted.<timestamp>` — that would destroy the valid file we just wrote.
- **Fix:** Define `backupAndRotate` (with `[[maybe_unused]]` to silence MSVC C4505) but do not invoke it from the save success path. Mirror of `config_io.cpp` invocation discipline where `backupAndRotate` fires only from `loadConfigJson` on parse failure, not from `saveConfigJson`.
- **Files modified:** `driver/src/training_io.cpp`, `driver/src/training_io.hpp` (doc-block updated)
- **Commit:** 0d0dba9

### Rule 3 — Blocking issue fix

**2. `tests/CMakeLists.txt` source-list expansion across 9 P7/P8 test targets**

- **Found during:** Task 4 build verification
- **Issue:** P7/P8 tests that compile `device_provider.cpp` or `detection_runner.cpp` directly (rather than linking the driver DLL — Pitfall 6) emitted unresolved-external-symbol link errors for `TrainingSession::addSample`, `TrainingSession::TrainingSession`, and `TrainingSession::~TrainingSession` once the driver TUs gained references to those symbols. Even though some tests pass `nullptr` for `deviceProvider_` (so the runtime never dereferences), MSVC still emits the symbol references and the linker needs them resolved.
- **Fix:** Added two `if(EXISTS .../training_session.cpp) list(APPEND ... training_session.cpp)` blocks (and the same for training_io.cpp) to each affected test target's source list. Pattern mirrors the existing P7/P8 EXISTS-gated source-list expansion.
- **Files modified:** `tests/CMakeLists.txt` (9 blocks edited)
- **Commit:** fb70e88

### Architectural — inlined `trainingSession()` accessor

**3. `DeviceProvider::trainingSession()` defined INLINE in `device_provider.hpp` rather than out-of-line in `device_provider.cpp`**

- **Found during:** Task 4 link verification of `test_detection_settings_propagation`
- **Issue:** `detection_runner.cpp` references `deviceProvider_->trainingSession()`. Out-of-line definition in `device_provider.cpp` would force every test exe linking `detection_runner.cpp` to also drag `device_provider.cpp` + its full transitive dependency chain (httplib, bindings, config_io, settings_validator, sinks, etc.) into the link line — explosion of test-target source lists.
- **Fix:** Define both `trainingSession()` overloads inline in `device_provider.hpp`. `unique_ptr<TrainingSession>::get()` does not require the full TrainingSession type — only the dtor, reset, and operator-> do — so the existing `class TrainingSession;` forward decl is sufficient. Comment in the header documents the rationale.
- **Files modified:** `driver/src/device_provider.hpp`, `driver/src/device_provider.cpp`
- **Commit:** fb70e88

## Architecture Notes

### Atomic Mode Discipline

DeviceProvider::tryStartTrainingSession (HTTP thread):
1. Lock trainingMutex_; check trainingSession_ is null (single-instance per D-09).
2. Construct TrainingSession (heavy — calls `createFFTDetector` + `detector_->startTraining()`).
3. release-store `mode_ = Training` AFTER the ctor completes (Pitfall 1).
4. Unlock.

DeviceProvider::resetTrainingSession (HTTP thread / Cleanup):
1. release-store `mode_ = Detecting` BEFORE touching the unique_ptr (Pitfall 2).
2. Lock trainingMutex_; move the unique_ptr out into a local; unlock.
3. Local unique_ptr destructor runs OUTSIDE the mutex — concurrent `trainingSession()` readers don't block on detector teardown.

DetectionRunner::RunLoop (detection thread):
- `auto mode = deviceProvider_->mode().load(std::memory_order_acquire);` once per drain cycle.
- Stale `mode==Detecting` after flip-to-Training: harmless — analyzes against still-valid trained profile.
- Stale `mode==Training` after flip-back-to-Detecting: harmless — null-checks `trainingSession()` and drops the block.

### Threading Surface Summary

| Method                        | Thread       | Mutex               | Atomic                  |
|-------------------------------|--------------|---------------------|-------------------------|
| TrainingSession::addSample    | detection    | mu_                 | samples_collected_      |
| TrainingSession::compute      | HTTP         | mu_                 | -                       |
| TrainingSession::recompute    | HTTP         | mu_                 | -                       |
| TrainingSession::snapshot     | HTTP         | mu_ (partial)       | samples_collected_ (lock-free read) |
| TrainingSession::tickTimeout  | HTTP         | mu_                 | -                       |
| TrainingSession::cancel       | HTTP         | mu_                 | -                       |
| DeviceProvider::tryStart...   | HTTP         | trainingMutex_      | mode_ (release-store)   |
| DeviceProvider::reset...      | HTTP/Cleanup | trainingMutex_      | mode_ (release-store)   |
| DeviceProvider::trainingSession() | HTTP/detection | trainingMutex_  | -                       |
| DetectionRunner::RunLoop      | detection    | -                   | mode_ (acquire-load)    |

## Test Status (Wave 0 → Wave 1 progression)

| Test                                | Wave 0 | Wave 1 (post-09-01) | Reason                                |
|-------------------------------------|--------|---------------------|---------------------------------------|
| test_training_session               | RED-build | partial-RED      | compiles past `training_session.hpp`; fails on `validateFinalizePayload` (lands 09-02) |
| test_training_endpoint_validation   | RED-build | RED-build        | http_server training routes land 09-02 |
| test_wav_replay                     | RED-build | RED-build        | wav_replay.{hpp,cpp} land 09-04        |
| test_detection_settings_propagation | GREEN | GREEN              | source list extended with training_session.cpp |
| test_device_provider_lifecycle_stress | GREEN | GREEN            | source list extended with training_session.cpp + training_io.cpp |
| test_state_clear_error              | GREEN | GREEN              | source list extended with training_session.cpp + training_io.cpp |
| (all other P7/P8 tests)             | GREEN | GREEN              | source list extended where required    |

## Self-Check: PASSED

**Files created (5):**
- `driver/src/training_session.hpp` — FOUND
- `driver/src/training_session.cpp` — FOUND
- `driver/src/training_io.hpp` — FOUND
- `driver/src/training_io.cpp` — FOUND
- `driver/src/driver_mode.hpp` — FOUND

**Files modified (6):**
- `driver/src/detection_runner.hpp` — modified (forward decl + ctor + member)
- `driver/src/detection_runner.cpp` — modified (include + ctor + RunLoop branch)
- `driver/src/device_provider.hpp` — modified (forward decl + accessors + members)
- `driver/src/device_provider.cpp` — modified (include + ctor wiring + Cleanup + impls)
- `driver/CMakeLists.txt` — modified (2 sources added)
- `tests/CMakeLists.txt` — modified (9 source-list expansions)

**Commits:**
- 0d0dba9 — feat(09-01): add training_io for atomic training_data.bin write
- 2689262 — feat(09-01): add TrainingSession class hosting INoiseDetector
- 95f3a48 — feat(09-01): add DriverMode atomic + per-iteration mode branch
- fb70e88 — feat(09-01): wire DriverMode + lazy TrainingSession on DeviceProvider

All commits verified present in `git log`.
