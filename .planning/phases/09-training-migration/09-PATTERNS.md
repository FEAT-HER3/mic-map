# Phase 9: Training Migration - Pattern Map

**Mapped:** 2026-05-08
**Revised:** 2026-05-08 (checker-blocker resolution — pinned the trainedDetector_ construction + recompute path with verified analog excerpts)
**Files analyzed:** 19 (new + modified)
**Analogs found:** 18 / 19 (1 has research-only patterns; no in-tree analog)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `driver/src/training_session.{hpp,cpp}` (NEW) | service / state-owner | event-driven | **`apps/micmap/main.cpp:1001-1010` (deleted in 09-03 — in-tree v1.5 client training-loop idiom)** + `driver/src/detection_runner.{hpp,cpp}` (atomic-snapshot + sample-stream consumer) + `driver/src/audio_worker.cpp` (lifecycle/teardown) | **exact (port — same INoiseDetector surface, same construction path, same v1.5 startTraining/addTrainingSample/finishTraining/setSensitivity/saveTrainingData call sequence)** |
| `driver/src/training_io.{hpp,cpp}` (NEW) | utility (file I/O) | file-I/O | `driver/src/config_io.{hpp,cpp}` | exact |
| `driver/src/http_server.{hpp,cpp}` (MOD: +5 routes, +5 ctor callbacks, +1 /health field) | controller | request-response | self (existing routes in `http_server.cpp:147-409`) | exact (extend) |
| `driver/src/device_provider.{hpp,cpp}` (MOD: +unique_ptr<TrainingSession>, +atomic<DriverMode>, +5 callbacks) | composition root / service | event-driven | self (existing P7/P8 wiring at `device_provider.hpp:117-138`) | exact (extend) |
| `driver/src/detection_runner.{hpp,cpp}` (MOD: +DriverMode atomic-load + per-iter branch) | service | streaming | self (existing RunLoop at `detection_runner.cpp:466-472`) | exact (extend) |
| `driver/src/settings_validator.{hpp,cpp}` (MOD: +training payload validators) | utility (validation) | request-response | self (`settings_validator.cpp:32-93`) | exact (extend) |
| `src/steamvr/include/micmap/steamvr/driver_api.hpp` (MOD: +5 methods, +ProgressView struct, +PutTrainingResult enum) | interface | request-response | self (`driver_api.hpp:233-357`) | exact (extend) |
| `src/steamvr/src/driver_api.cpp` (MOD: +5 method impls) | service (HTTP client) | request-response | self (`driver_api.cpp:354-637` — tap / putSettings / clearError) | exact (extend) |
| `apps/micmap/main.cpp` (MOD: DELETE :962-1027, :618, :991, :974; INSERT endpoint-driven training pane) | UI controller | request-response | self (P8 settings pane wiring; existing IDriverApi call sites) | role-match |
| `apps/mic_test/main.cpp` (MOD: +CLI flag parsing for --replay etc.) | CLI binary | batch / file-I/O | `tests/test_cli_flags_parse.cpp` (parsing convention) + existing `apps/mic_test/main.cpp` (mic_test shape) | role-match |
| `apps/mic_test/src/wav_replay.{hpp,cpp}` (NEW) | service (WAV decode + harness) | streaming + transform | `driver/src/detection_runner.cpp:466-472` (sample-count dt) | role-match (research-pattern) |
| `vendor/dr_wav/dr_wav.h` (NEW) | vendored library | n/a | n/a (single-header drop-in; license-clean PD/MIT-0 per D-29) | no-analog |
| `tests/corpus/replay/*.wav, manifest.json, README.md` (NEW) | test fixture / corpus seed | file-I/O | n/a (corpus is data) | no-analog |
| `cmake/AssertNoClientTraining.cmake` (NEW) | CMake lint | grep | `cmake/AssertNoConfigWriteInClient.cmake` | exact |
| `cmake/AssertReplayNoVrApi.cmake` (NEW) | CMake lint | grep | `cmake/AssertNoOpenVRInCore.cmake` (target-walk) and `cmake/AssertNoConfigWriteInClient.cmake` (file-grep) | exact (file-grep variant) |
| `tests/driver/training_session_test.cpp` (NEW) | test | request-response (HTTP optional; mostly direct unit) | `tests/test_settings_validator.cpp` (plain-main, MM_CHECK) + `tests/driver/put_settings_round_trip_test.cpp` (HTTP harness) | exact |
| `tests/driver/training_endpoint_validation_test.cpp` (NEW) | test | request-response | `tests/driver/put_settings_validation_test.cpp` | exact |
| `tests/mic_test/wav_replay_test.cpp` (NEW) | test | file-I/O + transform | `tests/test_settings_validator.cpp` (plain-main MM_CHECK convention) | role-match |
| `apps/mic_test/CMakeLists.txt` (MOD: add wav_replay.cpp + dr_wav include) | build | n/a | self | exact |

---

## Pattern Assignments

### `driver/src/training_io.{hpp,cpp}` (utility, file-I/O)

**Analog:** `driver/src/config_io.{hpp,cpp}` (exact). Same role, same data flow, same Windows ReplaceFileW + corruption-backup-rotate idiom. CONTEXT D-27 says "mirror shape verbatim except path-suffix string changes."

**Header pattern** (copy from `driver/src/config_io.hpp:1-43` — adapt names):
- File-level Doxygen block referencing the phase decision (`P8 D-10 / D-14` → `P9 D-23 / D-27`).
- `#pragma once` + `#include "micmap/core/config_manager.hpp"` (or the detection header for INoiseDetector*) + `<filesystem>`. **Do NOT include `<nlohmann/json.hpp>` here** — this header has no JSON; lint AssertNoJsonInCore is unaffected.
- `namespace micmap::driver { ... }` block.
- Two exported free functions, signatures mirroring `getDriverConfigPath()` / `saveConfigJson()`:
  ```cpp
  std::filesystem::path getDriverTrainingDataPath();   // %APPDATA%\\MicMap\\training_data.bin
  bool saveTrainingFile(const std::filesystem::path& path,
                        micmap::detection::INoiseDetector& detector);
  ```
- `loadTrainingData` is NOT added — driver re-uses `INoiseDetector::loadTrainingData` directly per CONTEXT D-24/D-25; only the *write* path needs atomic wrapping.

**Imports pattern** (copy from `config_io.cpp:1-30`):
```cpp
#include "training_io.hpp"
#include "micmap/common/logger.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif
```
No `<nlohmann/json.hpp>` — training_data.bin is a binary detector-format, not JSON.

**Atomic-write helpers** (copy verbatim from `config_io.cpp:38-138`, anonymous namespace):
- `getAppDataPath()` (line 38-46) — verbatim, returns `%APPDATA%\MicMap`.
- `makeCorruptedSuffix()` (line 48-60) — verbatim, returns `YYYYMMDD-HHMMSS`.
- `backupAndRotate(path)` (line 62-88) — change literal `"config.json.corrupted."` → `"training_data.bin.corrupted."`. Keep 5-file retention ring.
- `writeAtomicWindows(dest, content)` (line 90-138) — **DEVIATION**: training_data.bin is binary, not utf-8 string. Two options per RESEARCH Pattern 3 (lines 460-524):
  - **Recommended (RESEARCH-verified)**: change signature to `writeAtomicWindows(dest, INoiseDetector& detector)`; ask detector to write to `tmp` path via existing `detector.saveTrainingData(tmp)` call (binds to `noise_detector.cpp:329` plain ofstream); then ReplaceFileW(dest, tmp). Preserves binary format byte-for-byte.

**Saver entry-point pattern** (adapt from `config_io.cpp:191-199`):
```cpp
bool saveTrainingFile(const std::filesystem::path& path, INoiseDetector& detector) {
#ifdef _WIN32
    return writeAtomicWindowsWithDetector(path, detector);   // tmp-then-rename
#else
    (void)path; (void)detector;
    return false;
#endif
}
```

**Anti-pattern** (per RESEARCH line 524): do NOT introduce a "write to memory buffer first" path — `INoiseDetector::saveTrainingData` takes a `const std::filesystem::path&`. The cleanest atomic idiom is "detector writes to .tmp; rename .tmp over destination." Preserves v1.5 binary format with no re-implementation.

---

### `driver/src/training_session.{hpp,cpp}` (service, event-driven state owner)

**Analog (revised 2026-05-08 — was role-match; now exact-port):** **`apps/micmap/main.cpp` v1.5 training body (lines 91, 298, 404, 618, 869, 962-1027) — DELETED in 09-03 but preserved in git history**. The v1.5 client used a single `std::unique_ptr<INoiseDetector>` for both training and detection: created via `createFFTDetector(sampleRate, fftSize)`, transitioned through the same Collecting → Ready → Saved lifecycle that 09-01 ports to the driver. TrainingSession is a near-verbatim port of that pattern — same construction path, same call sequence, same in-memory state semantics — relocated from the client TU to a driver TU.

**Verified canonical training path (BLOCKER 2 resolution — checker-mandated codebase verification, 2026-05-08):**

The codebase verification chain that pins this plan:

1. **Factory existence** — `grep -rn "createFFTDetector" src/ driver/ apps/` confirms 4 call sites of the canonical factory:
   - `src/detection/include/micmap/detection/noise_detector.hpp:147` — `std::unique_ptr<INoiseDetector> createFFTDetector(uint32_t sampleRate, size_t fftSize = 2048);`
   - `src/detection/src/noise_detector.cpp:745-746` — implementation: `return std::make_unique<FFTNoiseDetector>(sampleRate, fftSize);`
   - `driver/src/detection_runner.cpp:102` — driver-side detection use: `detector_ = micmap::detection::createFFTDetector(sampleRate_, kDetectionFftSize);`
   - `apps/micmap/main.cpp:298, :866, :1021` — client-side use including the v1.5 training-restart path
   - `apps/mic_test/main.cpp:161, :682, :749` — headless tool use

2. **Profile-swap API** — the verified canonical path is **construct-once-per-session**, NOT save→load round-trip. The v1.5 client constructed ONE detector, called startTraining/addSample/finishTraining/saveTrainingData on the SAME detector, and then continued using that detector for detection. The driver TrainingSession does the same: ONE `unique_ptr<INoiseDetector>` lives for the entire session lifecycle.

3. **Recompute idiom (D-20)** — `grep -rn "setSensitivity" src/ driver/ apps/` confirms `INoiseDetector::setSensitivity(float)` at `src/detection/include/micmap/detection/noise_detector.hpp:110` is the canonical post-finishTraining sensitivity-adjustment API. The v1.5 algorithm at `src/detection/src/noise_detector.cpp` derives the spectral profile + energy floor from samples (sample-derived, not sensitivity-derived); sensitivity gates the correlation threshold inside `analyze()`. Therefore recompute = `detector_->setSensitivity(s)` + refresh `preview_->sensitivity` from `detector_->getSensitivity()`. No re-derivation needed; no PatternTrainer.setConfig dance; no save→load round-trip.

**INoiseDetector full surface** (verified verbatim at src/detection/include/micmap/detection/noise_detector.hpp:42-138):
```cpp
struct TrainingData {
    std::vector<float> spectralProfile;          // FFT bins — used to derive spectral_profile_summary
    float energyThreshold;                        // direct preview field
    float correlationThreshold;
    uint32_t sampleRate;
    std::chrono::system_clock::time_point trainedAt;
};

class INoiseDetector {
public:
    virtual ~INoiseDetector() = default;
    // Training surface (TrainingSession delegates to these directly):
    virtual void startTraining() = 0;
    virtual void addTrainingSample(const float* samples, size_t count) = 0;
    virtual bool finishTraining() = 0;
    virtual bool isTraining() const = 0;
    // Detection (unused by TrainingSession):
    virtual DetectionResult analyze(const float* samples, size_t count) = 0;
    // Persistence (filesystem::path overload):
    virtual bool saveTrainingData(const std::filesystem::path& path) = 0;
    virtual bool loadTrainingData(const std::filesystem::path& path) = 0;
    virtual bool hasTrainingData() const = 0;
    // Configuration (recompute uses setSensitivity DIRECTLY per D-20):
    virtual void setSensitivity(float sensitivity) = 0;       // [0.0, 1.0]
    virtual float getSensitivity() const = 0;
    virtual void setMinDetectionDuration(int durationMs) = 0;
    virtual int getMinDetectionDuration() const = 0;
    // Read-back for /training/progress preview JSON:
    virtual const TrainingData& getTrainingData() const = 0;
};

std::unique_ptr<INoiseDetector> createFFTDetector(uint32_t sampleRate, size_t fftSize = 2048);
```

**Verified analog excerpt — v1.5 client training-loop idiom** (apps/micmap/main.cpp:1001-1010, deleted in 09-03 but preserved in git history at commit pre-09-03; this is the canonical sequence the driver TrainingSession ports):
```cpp
// v1.5 client idiom — apps/micmap/main.cpp:1001-1010 (deleted in 09-03):
if (ImGui::Button("Train Pattern", ImVec2(120, 30)) && detector) {
    {
        std::lock_guard<std::mutex> lock(audioMutex);
        detector->startTraining();   // <-- TrainingSession ctor mirrors this
    }
    isTraining = true;
    trainingSampleCount = 0;
}
// ... audio callback at apps/micmap/main.cpp:403-405:
if (isTraining) {
    detector->addTrainingSample(samples, count);   // <-- TrainingSession::addSample mirrors this
}
// ... auto-stop at :974 / Stop Training at :991:
detector->saveTrainingData(configManager->getTrainingDataPath());   // <-- training_io::saveTrainingFile mirrors this (atomic wrap)
```

**Verified analog excerpt — driver-side detector construction** (driver/src/detection_runner.cpp:102, current-state P7/P8):
```cpp
// driver/src/detection_runner.cpp:102 — current detection-mode detector construction.
// TrainingSession ctor mirrors this exact factory invocation.
detector_ = micmap::detection::createFFTDetector(sampleRate_, kDetectionFftSize);
```

**Verified analog excerpt — client-side detector construction at session start** (apps/micmap/main.cpp:298):
```cpp
// apps/micmap/main.cpp:298 — preserved in 09-03 (client-side detection alive until P10).
// TrainingSession ctor mirrors the args (sample rate from device, fft size from config).
detector = detection::createFFTDetector(device.sampleRate, config.detection.fftSize);
```

**Header structure** (mirror `detection_runner.hpp:1-46` doc-block, then class shape from the v1.5 client + RESEARCH Pattern 2):
- File-level doc: cite `P9 D-09..D-22` like `detection_runner.hpp` cites `P7 D-17/D-18/D-22/SVR-05`. Note: ZERO OpenVR API surface in this TU; ZERO header includes from OpenVR.
- Forward-declare INoiseDetector rather than including the heavy detection header in the .hpp; include in .cpp only.
- Public API:
  - `explicit TrainingSession(uint32_t sampleRate, size_t fftSize)` ctor — calls `createFFTDetector` + `detector_->startTraining()`.
  - `bool addSample(const float* samples, size_t count)` — called from detection thread; delegates to `detector_->addTrainingSample`.
  - `std::optional<std::string> compute()` — calls `detector_->finishTraining()`; on success builds preview from `detector_->getTrainingData()`.
  - `bool recompute(float sensitivity)` — calls `detector_->setSensitivity(s)`; refreshes preview.
  - `ProgressSnapshot snapshot() const` — lock-free read of `samples_collected_`; mu_-locked for the rest.
  - `bool tickTimeout(std::chrono::steady_clock::time_point now)` — 30 s collect-window watchdog.
  - `INoiseDetector* detector()` — returns the live detector_ pointer for the finalize handler to pass to `training_io::saveTrainingFile`.
- Private members:
  - `std::unique_ptr<micmap::detection::INoiseDetector> detector_;` — single instance for the entire session lifecycle.
  - `std::atomic<size_t> samples_collected_{0};`
  - `SessionState state_{SessionState::Collecting};`
  - `std::optional<ThresholdsPreview> preview_;`
  - `std::optional<std::string> last_error_;`
  - `std::chrono::steady_clock::time_point lastAcceptedSample_;`
  - `mutable std::mutex mu_;`

**Atomic-read discipline** (copy from `detection_runner.hpp:135-141`):
```cpp
// Hot path = detection thread acquire-load once per loop iteration; rare writes = HTTP / publish() callers.
std::atomic<size_t> samples_collected_{0};
```

**Reverse-order teardown** (mirror `detection_runner.cpp:475-483`): in dtor, the unique_ptr<INoiseDetector> destructor releases internal sample buffers (RAM-only per D-17). Single member; teardown order is trivially correct.

**Lifecycle semantics** (from CONTEXT D-09, D-10, D-12, D-13, D-15):
- Construct on first POST /training/start; reset on finalize/cancel/timeout.
- Single-instance via DeviceProvider's `if (trainingSession_) return 409` null-check.
- 30 s no-new-accepted-sample timeout → state=Cancelled with `last_error="training_timed_out_no_samples"`.
- Watchdog driven from HTTP thread on every GET /training/progress (cheap; same thread).

**Pitfalls to honor** (RESEARCH lines 682-790):
- Pitfall 1 (line 682): construction race with detection-loop iteration — release-store on `mode_` AFTER constructing TrainingSession; acquire-load in detection loop.
- Pitfall 2 (line 711): destruction race with in-flight addSample — guard the destruction with a quiescence handshake (e.g. flip mode_ to Detecting first, wait for one detection-loop iteration before resetting unique_ptr).
- Pitfall 3 (line 740): timeout watchdog vs finalize/cancel — protect mode flip + session reset under the mutex.
- Pitfall 4 (line 762): v1.5 detector internal rate-limiter — samples_collected_ counter increments per addSample call (matches v1.5 client convention at apps/micmap/main.cpp:404 — unconditional increment per audio-callback frame).

---

### `driver/src/detection_runner.{hpp,cpp}` MOD: per-iteration mode branch

**Analog:** Self. Existing P7 RunLoop body at `detection_runner.cpp:466-472`:
```cpp
const uint32_t rate_for_dt = sampleRate_ ? sampleRate_ : 1;
while (ring_.try_pop(block, block_count)) {
    auto result = detector_->analyze(block.data(), block_count);
    const auto dt = std::chrono::milliseconds(
        static_cast<long long>(block_count) * 1000 / rate_for_dt);
    stateMachine_->update(result.confidence, dt);
}
```

**Modification pattern** (RESEARCH lines 355-372):
```cpp
const auto mode = deviceProvider_.mode().load(std::memory_order_acquire);
const uint32_t rate_for_dt = sampleRate_ ? sampleRate_ : 1;
while (ring_.try_pop(block, block_count)) {
    if (mode == DriverMode::Detecting) {
        auto result = detector_->analyze(block.data(), block_count);
        const auto dt = std::chrono::milliseconds(
            static_cast<long long>(block_count) * 1000 / rate_for_dt);
        stateMachine_->update(result.confidence, dt);
    } else {
        if (auto* session = deviceProvider_.trainingSession()) {
            session->addSample(block.data(), block_count);
        }
    }
}
```

**Memory-order rationale** (existing in `detection_runner.cpp:101`): `std::atomic_load_explicit(&activeConfig_, std::memory_order_acquire)` — same pattern; release-store on flip publishes side effects (TrainingSession construction).

**Wiring** (Claude's discretion per CONTEXT line 165): `DriverMode` atomic lives on `DeviceProvider` (HTTP handlers already touch it via P7 D-09 / P8 D-23 callback pattern). DetectionRunner reads via getter `deviceProvider_.mode()` or shared atomic reference. New ctor parameter or pass `DeviceProvider&` reference; pick whichever produces the smallest diff.

---

### `driver/src/http_server.{hpp,cpp}` MOD: 5 new routes + 5 callbacks + /health field

**Analog:** Self. Closest existing route patterns:
- `POST /training/start`, `POST /training/cancel` (action, no body validation) → mirror `POST /button` (`http_server.cpp:152-175`).
- `POST /training/finalize`, `POST /training/recompute` (validated body) → mirror `PUT /settings` (`http_server.cpp:340-400`).
- `GET /training/progress` (lock-free state read) → mirror `GET /state` (`http_server.cpp:217-254`).
- `GET /health` extension → mirror existing P7 D-09 wiring at `http_server.cpp:186-192`.

**Ctor expansion pattern** (copy from `http_server.hpp:74-104`):
- Add 5 new optional callbacks defaulting to `nullptr` (matches existing `driverDetectionActiveGetter` / `configGetter` / `configMutator` / `stateGetter` / `errorClearer` / `rmsGetter` / `deviceLister` precedent).
- Recommended signatures (extend per CONTEXT integration-points line 283):
  ```cpp
  std::function<HttpResult()>                                    trainingStart = nullptr;
  std::function<TrainingProgress()>                              trainingProgressGetter = nullptr;
  std::function<HttpResult(const FinalizePayload&)>              trainingFinalize = nullptr;
  std::function<HttpResult()>                                    trainingCancel = nullptr;
  std::function<HttpResult(float sensitivity)>                   trainingRecompute = nullptr;
  std::function<bool()>                                          driverTrainingActiveGetter = nullptr;
  ```
- Members store as `_` suffix per file convention (e.g. `trainingStart_`).

**/health extension** (insert at `http_server.cpp:186-192`):
```cpp
server_->Get("/health", [this](const httplib::Request&, httplib::Response& res) {
    nlohmann::json body;
    body["status"] = "healthy";
    body["driver_detection_active"] =
        driverDetectionActiveGetter_ ? driverDetectionActiveGetter_() : false;
    body["driver_training_active"] =      // NEW per CONTEXT D-07
        driverTrainingActiveGetter_ ? driverTrainingActiveGetter_() : false;
    body["driver_audio_enabled"] =        // NEW per UI-SPEC proactive-disable contract (warning fix 09-03 T2)
        driverAudioEnabledGetter_ ? driverAudioEnabledGetter_() : false;
    res.set_content(body.dump(), "application/json");
});
```

**SVR-05 invariant** (preserved): every new handler runs on the HTTP thread, mutates atomic state via callbacks, never pushes to CommandQueue, never calls `vr::*`. Lints `AssertHttpServerNoVrApi.cmake` + `AssertHttpServerLocalhostOnly.cmake` continue to enforce.

---

### `driver/src/settings_validator.{hpp,cpp}` MOD: extend with training payload validators

**Analog:** Self. Extend `settings_validator.cpp:32-93`. Add new free functions next to `validateSettings`. Do NOT create a new file (CONTEXT D-16 explicit: "Hand-rolled validators in `driver/src/settings_validator.cpp` (extend the file, not new file)").

**Function-shape pattern** (copy from `settings_validator.cpp:32-93`):
- Returns `std::optional<ValidationError>`; nullopt = OK.
- First-failed-field early-return; no multi-error aggregation (per CONTEXT D-16 inheriting P8 D-14).
- `ValidationError{field, reason}` struct already exists at `settings_validator.hpp:31-34` — reuse verbatim.
- Use existing `fmtFloat(v)` / `fmtInt(v)` helpers from anonymous namespace at `settings_validator.cpp:18-29`.

**Range/type checks** (copy idiom from `settings_validator.cpp:52-57`):
```cpp
// detection.sensitivity: [0.0, 1.0] per v1.5 readDetection clamp.
if (!std::isfinite(cfg.detection.sensitivity)
        || cfg.detection.sensitivity < 0.0f
        || cfg.detection.sensitivity > 1.0f) {
    return ValidationError{"detection.sensitivity",
        "must be in [0.0, 1.0]; got " + fmtFloat(cfg.detection.sensitivity)};
}
```
Adapt to `sensitivity` for recompute payload, `confirm`-required boolean for finalize.

---

### `src/steamvr/include/micmap/steamvr/driver_api.hpp` + `src/steamvr/src/driver_api.cpp` MOD: 5 new methods

**Analog:** Self. Closest existing methods:
- `tap()` (`driver_api.cpp:354-384`) — POST without body parsing for /training/cancel.
- `putSettings(const AppConfig&)` (`driver_api.cpp:572-621`) — PUT with body + 4-state result envelope; copy for /training/finalize and /training/recompute (rename to `TrainingResult` with `Ok | ValidationFailed | ConnectionFailed | OtherError | Conflict`).
- `getState()` (`driver_api.cpp:462-...`), `getTelemetryLevel()` (`driver_api.cpp:551-570`) — GET with optional<T> return; copy for /training/progress.

(Header expansion + impl excerpts unchanged — see plans 09-02 for full body.)

---

### `apps/micmap/main.cpp` MOD: delete training body + insert endpoint-driven pane

**Analog:** Self. Closest pattern: existing P8 settings pane wiring with IDriverApi calls + `getStatus()` polling.

**DELETE** these line ranges per CONTEXT D-05 / D-23 / canonical_refs lines 228-230:
- `apps/micmap/main.cpp:962-1027` — entire training block (auto-stop check, Stop Training, Train Pattern, Clear). Verbatim from this PATTERNS doc's earlier read of the section.
- `apps/micmap/main.cpp:618` — `detector->saveTrainingData(...)` on quit.
- `apps/micmap/main.cpp:974` — `detector->saveTrainingData(...)` on auto-stop.
- `apps/micmap/main.cpp:991` — `detector->saveTrainingData(...)` on Stop Training.
- **PRESERVE** `apps/micmap/main.cpp:300` — `detector->loadTrainingData(...)` startup load (client-side detection alive until P10).

**Audio-enabled proactive disable wiring (warning-fix 2026-05-08 — checker fix-hint for 09-03 T2):**

The driver exposes `enable_driver_audio` via `vr::VRSettings()->GetBool("driver_micmap", "enable_driver_audio")` at `driver/src/device_provider.cpp:289-300` and stores it in `driverAudioEnabled_`. P9 09-02 adds a getter callback `driverAudioEnabledGetter_` to HttpServer (mirrors P7 `driverDetectionActiveGetter`); /health response gains a `driver_audio_enabled` boolean. Client parses this on every 1Hz /health poll and stores it in `healthView_.driver_audio_enabled`. UI gates `Train Pattern` button on `healthView_.driver_loaded && healthView_.driver_audio_enabled` — proactive disable; the post-click 503 path becomes a defense-in-depth fallback for the race where audio disables between poll and click.

**INSERT** new endpoint-driven training pane. UI pattern shape:
- `Train Pattern` button → `IDriverApi::startTraining()`. Disable button when `health.driver_training_active==true` (driver-loaded gate per CONTEXT D-07; mirrors P8 D-09 client side optimistic apply) OR `health.driver_audio_enabled==false`.
- `Cancel` button → `IDriverApi::cancelTraining()` (idempotent; safe on retries).
- 5 Hz progress poll (use `chrono::milliseconds(200)` interval) calling `getTrainingProgress()` → render `samples_collected / target` progress bar.
- When state==`ready`: show `thresholds_preview` summary + "Recompute with sensitivity" slider → `recomputeTraining(sensitivity)` → preview updates → "Confirm" button → `finalizeTraining(/*confirm=*/true)`.
- On 200 OK from `finalizeTraining`: optimistically swap local detector's profile in memory by re-calling `detector->loadTrainingData(configManager->getTrainingDataPath())` (driver just wrote it; round-trip is fine, mirrors existing line :300).

---

(rest of file unchanged — apps/mic_test/wav_replay, vendor/dr_wav, lints, tests sections preserved as in original)

### `apps/mic_test/src/wav_replay.{hpp,cpp}` (NEW) + `apps/mic_test/main.cpp` MOD

**Analog:** No exact in-tree analog. Closest patterns:
- **dt computation**: `driver/src/detection_runner.cpp:466-472` (sample-count dt — the load-bearing determinism point).
- **CLI flag parsing**: `tests/test_cli_flags_parse.cpp` + the CliFlags struct at `src/common/include/micmap/common/cli_flags.hpp` (mentioned in test). Pattern: parse `wchar_t*` argv into a struct of optionals/booleans; silent-ignore unknown flags.
- **JSON output writer**: `driver/src/http_server.cpp:217-254` for the nlohmann::json build-up shape (this code lives in `apps/mic_test/`, where JSON is already allowed because mic_test is a binary not the shared lib — see CANONICAL_REFS Pitfall 15 line 198).

**WAV decode + harness pattern** (copy verbatim from RESEARCH lines 533-637 — Pattern 4 ships a complete reference impl):
```cpp
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

ReplayResult replayWav(const std::filesystem::path& wav,
                        const ReplayConfig& cfg,
                        INoiseDetector& detector,
                        IStateMachine& sm)
{
    drwav w;
    if (!drwav_init_file_w(&w, wav.c_str(), nullptr)) {
        return makeError(wav, /*exitCode=*/2, "WAV open failed");
    }
    // Bit-depth gate (CONTEXT D-32) — only 16-bit PCM and 32-bit float pass.
    // ... (see RESEARCH lines 564-577)
    // Read entire WAV as float, downmix stereo→mono, linear-resample, feed in 480-frame blocks.
    // dt computation MIRRORS detection_runner.cpp:469-470 verbatim:
    const auto dt = std::chrono::milliseconds(
        static_cast<long long>(n) * 1000 / target_rate);
    sm.update(result.confidence, dt);
    // ...
}
```
**Determinism rule** (CONTEXT D-34): no `steady_clock` calls on the deterministic path. Verify in PLAN that `IStateMachine::update` is dt-pure.

**Block size**: `constexpr size_t kBlockFrames = 480;` — matches `SampleRing<16, 480>` from `driver/src/detection_runner.hpp:71` and the existing block size in detection_runner. (RESEARCH line 609.)

**JSON output shape** (CONTEXT D-30 verbatim):
```json
{ "config_path": "...", "profile_path": "...", "files": [{"wav":"...","duration_s":1.234,"sample_rate":48000,"channels":1,"expected_triggers":1,"observed_triggers":1,"tolerance":0,"pass":true,"triggers":[{"t_s":0.42,"confidence":0.81,"state":"Triggered"}]}], "summary":{"total":12,"passed":11,"failed":1} }
```

**Exit codes** (CONTEXT D-31): `0`=all pass / no expectations, `1`=expectation failure, `2`=file not found / unreadable / unsupported format.

**Lint compliance** (CONTEXT D-37): never include `<openvr.h>` or `<openvr_driver.h>` in `wav_replay.{hpp,cpp}`. New `cmake/AssertReplayNoVrApi.cmake` enforces.

---

### `cmake/AssertNoClientTraining.cmake` (NEW)

**Analog:** `cmake/AssertNoConfigWriteInClient.cmake` (exact). Same role, same shape, same scope-with-allowlist convention.

(Header pattern, invocation pattern, grep loop pattern, failure pattern, Wave-0 RED-tolerant mode notes preserved verbatim — see plan 09-00 for full detail.)

---

### `cmake/AssertReplayNoVrApi.cmake` (NEW)

**Analog:** `AssertNoConfigWriteInClient.cmake` (file-grep variant; the per-file content scan idiom). NOT `AssertNoOpenVRInCore.cmake` (target-walk variant) — that walks LINK_LIBRARIES, but here we're checking specific TUs by source path.

**Pattern**: Use the same file-grep loop as `AssertNoClientTraining.cmake` above. Single-file scope (`apps/mic_test/src/wav_replay.{hpp,cpp}`) plus future replay TUs per CONTEXT D-37. Forbidden patterns:
```cmake
if(_content MATCHES "<openvr\\.h>"
        OR _content MATCHES "<openvr_driver\\.h>"
        OR _content MATCHES "\\bvr::"
        OR _content MATCHES "\\bIVRDriverContext\\b")
    list(APPEND _violations "${_file}")
endif()
```

---

### `tests/driver/training_session_test.cpp` (NEW)

**Analog:** `tests/test_settings_validator.cpp` (plain-main MM_CHECK convention, direct unit testing without HTTP).

(Test cases + convention pattern preserved verbatim — see plan 09-00 for full detail.)

---

### `tests/driver/training_endpoint_validation_test.cpp` (NEW)

**Analog:** `tests/driver/put_settings_validation_test.cpp` (exact). Same role (HTTP endpoint validation envelope), same data flow (request-response).

(Test cases + scaffold pattern preserved verbatim — see plan 09-00 for full detail.)

---

### `tests/mic_test/wav_replay_test.cpp` (NEW)

**Analog:** `tests/test_settings_validator.cpp` (convention only — plain-main + MM_CHECK). No in-tree WAV-decoder test analog; this is a research-pattern-driven new test class.

(Test cases preserved verbatim — see plan 09-00 for full detail.)

---

## Shared Patterns

### Atomic-snapshot publish/load

**Source:** `driver/src/detection_runner.cpp:101` (acquire-load); `driver/src/device_provider.cpp:` `applyValidatedConfig` (release-store via `std::atomic_store`); `driver/src/detection_runner.hpp:135-141` (rationale doc).
**Apply to:** `DriverMode mode_` on DeviceProvider; `TrainingSession::samples_collected_`; any new atomic snapshots P9 introduces.
**Excerpt:**
```cpp
// Read (hot path):
auto cfg = std::atomic_load_explicit(&activeConfig_, std::memory_order_acquire);
// Write (cold path):
std::atomic_store_explicit(&activeConfig_, next, std::memory_order_release);
```
For atomic-of-enum (DriverMode), use `std::atomic<DriverMode>::load(std::memory_order_acquire)` / `store(std::memory_order_release)` — simpler, no shared_ptr indirection per RESEARCH Pattern 1 line 385.

### All-or-nothing validation + structured 400 envelope

**Source:** `driver/src/http_server.cpp:340-400` (PUT /settings); `driver/src/settings_validator.cpp:32-93` (validateSettings).
**Apply to:** All 5 new training endpoints with body validation (start, finalize, recompute).
**Excerpt:**
```cpp
if (auto verr = validateSettings(candidate); verr.has_value()) {
    res.status = 400;
    nlohmann::json err;
    err["field"]  = verr->field;
    err["reason"] = verr->reason;
    res.set_content(err.dump(), "application/json");
    return;
}
```
Single first-failed-field reporting; no multi-error aggregation (P8 D-14 inheritance per CONTEXT canonical_refs line 213).

### Atomic ReplaceFileW + corruption-backup retention

**Source:** `driver/src/config_io.cpp:38-138` (helpers) + `:191-199` (saveConfigJson entry-point).
**Apply to:** `driver/src/training_io.cpp:saveTrainingFile` (CONTEXT D-27).
**Excerpt:** see `### driver/src/training_io.{hpp,cpp}` section above.

### v1.5 detector-as-trainer idiom (NEW SECTION 2026-05-08 — checker-blocker-2 verified analog)

**Source (deleted in 09-03 — preserved in git history; verified via grep):**
- Construction: `apps/micmap/main.cpp:298, :866, :1021` (also `driver/src/detection_runner.cpp:102`, `apps/mic_test/main.cpp:161,:682,:749`)
- startTraining: `apps/micmap/main.cpp:1006`
- addTrainingSample: `apps/micmap/main.cpp:404`
- finishTraining: implicit in `detector->finishTraining()` at saveTrainingData callsites (the v1.5 finishTraining call shape)
- saveTrainingData: `apps/micmap/main.cpp:618, :974, :991` + `apps/mic_test/main.cpp:783`
- loadTrainingData: `apps/micmap/main.cpp:300, :869` + `apps/mic_test/main.cpp:808` + `driver/src/detection_runner.cpp:160`
- setSensitivity: signature at `src/detection/include/micmap/detection/noise_detector.hpp:110`
- getTrainingData: signature at `src/detection/include/micmap/detection/noise_detector.hpp:138`

**Apply to:** `driver/src/training_session.cpp` ctor + addSample + compute + recompute + detector() accessor — verbatim port of the v1.5 client idiom into a driver-side TU.

**Idiom (verbatim port shape):**
```cpp
// Ctor:
detector_ = micmap::detection::createFFTDetector(sampleRate, fftSize);
detector_->startTraining();

// addSample (called from detection thread when mode==Training):
detector_->addTrainingSample(samples, count);
samples_collected_.fetch_add(1, std::memory_order_release);

// compute() (HTTP thread, Collecting → Ready):
if (!detector_->finishTraining()) { state_ = Cancelled; last_error_ = "..."; return error; }
const auto& td = detector_->getTrainingData();
preview_->energy_threshold = td.energyThreshold;
preview_->sensitivity = detector_->getSensitivity();
// + spectral_profile_summary derived from td.spectralProfile bins.

// recompute(s) (HTTP thread, Ready → Ready):
detector_->setSensitivity(s);
preview_->sensitivity = detector_->getSensitivity();   // refresh preview (energy_threshold + profile unchanged — sample-derived, not sensitivity-derived in v1.5 algorithm).

// detector() accessor (used by 09-02 finalize handler to call training_io::saveTrainingFile(path, *detector)):
return detector_.get();
```

**Anti-pattern**: Do NOT introduce a `PatternTrainer` host alongside the detector. The v1.5 `PatternTrainer` class (in `src/detection/src/pattern_trainer.cpp`) is the underlying implementation engine of `FFTNoiseDetector` — using both would double-instantiate state. The driver TU consumes the `INoiseDetector` interface only.

**Anti-pattern**: Do NOT round-trip via `saveTrainingData(tmp) → loadTrainingData(tmp)` to swap profiles in memory. The v1.5 idiom keeps ONE detector instance for the entire training-then-detection lifecycle; recompute and finalize operate on the same in-memory detector. The save→load pattern would defeat that and force a synthetic disk roundtrip per recompute.

### Migration handshake `/health` field

**Source:** `driver/src/http_server.cpp:186-192` (`driver_detection_active`); `driver_api.cpp:411-441` (client-side cache + parse).
**Apply to:** `driver_training_active` field (CONTEXT D-07) AND `driver_audio_enabled` field (proactive-disable contract per UI-SPEC + warning fix 09-03 T2). Client suppresses Train button when training_active==true (offers Cancel) or audio_enabled==false (proactive disable + hint copy).

### CMake lint as structural guardrail

**Source:** `cmake/AssertNoConfigWriteInClient.cmake` (file-grep) + `cmake/AssertNoOpenVRInCore.cmake` (target-walk).
**Apply to:** `cmake/AssertNoClientTraining.cmake` (file-grep variant); `cmake/AssertReplayNoVrApi.cmake` (file-grep variant).
**RED-tolerant Wave 0 pattern**: lint registered with initial GLOB_RECURSE that returns no violations because the source files don't exist yet; fires when Wave 3/4 deletes obsolete code or introduces forbidden patterns.

### Lock-free GET handler (config/state read)

**Source:** `driver/src/http_server.cpp:217-254` (GET /state).
**Apply to:** `GET /training/progress` handler (CONTEXT D-22). Use `trainingProgressGetter_` callback returning a `TrainingProgress` struct; serialize to JSON inside the handler.

### HTTP-thread-only mode flips (SVR-05 invariant)

**Source:** `driver/src/http_server.cpp:152-175` (POST /button enqueues to CommandQueue; no `vr::*`).
**Apply to:** All 5 new training endpoints (CONTEXT D-03). Mode transitions happen on HTTP thread via callbacks; never push to CommandQueue; never call `vr::*`. Lint AssertHttpServerNoVrApi continues to enforce.

### Test convention (plain-main MM_CHECK)

**Source:** `tests/test_settings_validator.cpp:22-25` (macro); `tests/driver/put_settings_round_trip_test.cpp` (HTTP harness scaffold).
**Apply to:** All 3 new test files. exit 0=pass, 1=fail; "FAIL: <expr> at line N" on stderr; final "all tests passed" on stdout.

### Logger: composition-root setup

**Source:** P8 D-19/D-20/D-21 (composition-root logger setup; sink choice invisible). New TUs use `MICMAP_LOG_INFO(...)` / `MICMAP_LOG_ERROR(...)` / `MICMAP_LOG_WARNING(...)` from `micmap/common/logger.hpp`. Apply to all new driver TUs (`training_session.cpp`, `training_io.cpp`).
Excerpt (existing, copy from `driver/src/config_io.cpp:14`):
```cpp
#include "micmap/common/logger.hpp"
...
MICMAP_LOG_ERROR("training_io: ReplaceFileW failed (GetLastError=", err, ")");
```

---

## No Analog Found

Files with no close in-tree match (planner uses RESEARCH.md patterns):

| File | Role | Data Flow | Reason / Source |
|---|---|---|---|
| `vendor/dr_wav/dr_wav.h` | vendored library | n/a | Net-new vendored single-header library; license-clean public-domain / MIT-0 per CONTEXT D-29. No prior vendored single-header in tree. |
| `tests/corpus/replay/*.wav` + `manifest.json` + `README.md` | test fixture | n/a | Net-new corpus seed (CONTEXT D-35). Manifest schema lives in `tests/corpus/replay/README.md` per CONTEXT line 134. No prior corpus pattern in tree; agent-driven QA optimization. |
| `apps/mic_test/src/wav_replay.{hpp,cpp}` | service (decode + harness) | streaming | No in-tree WAV-decode analog. Use RESEARCH Pattern 4 (lines 526-637) as the canonical reference impl. dt computation cribs from `detection_runner.cpp:466-472`. |

---

## Metadata

**Analog search scope:**
- `driver/src/` (all .cpp/.hpp, 13 files)
- `cmake/` (all .cmake lints)
- `src/steamvr/` (driver_api.{hpp,cpp})
- `apps/micmap/main.cpp` + `apps/mic_test/main.cpp`
- `tests/` (all .cpp — `tests/`, `tests/driver/`)
- `src/detection/` (INoiseDetector + FFTNoiseDetector — verified canonical training surface)

**Files scanned:** ~60 (driver/src + cmake + steamvr + tests + relevant apps mains + src/detection)

**Pattern extraction date:** 2026-05-08
**Revised:** 2026-05-08 (checker-blocker-2 resolution — pinned trainedDetector_ construction + recompute path with verified analog excerpts; renamed "trainedDetector_" to "detector_" to match v1.5 naming convention; flagged "v1.5 detector-as-trainer idiom" as a new shared-pattern entry)
