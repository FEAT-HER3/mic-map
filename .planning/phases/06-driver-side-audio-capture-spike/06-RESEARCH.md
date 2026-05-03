# Phase 6: Driver-Side Audio Capture Spike — Research

**Researched:** 2026-05-02
**Domain:** WASAPI audio capture inside an OpenVR server-driver DLL hosted by `vrserver.exe`; COM apartment ownership; reverse-order teardown of WASAPI + IMMNotificationClient + std::thread; runtime-flag plumbing via `vr::VRSettings()`
**Confidence:** HIGH on COM/WASAPI/OpenVR contracts and code reuse points (verified in tree); MEDIUM on the actual outcome of WASAPI inside vrserver's DLL host (the spike itself — sister project validation only)

## Summary

Phase 6 is a feasibility spike that introduces a flag-gated audio worker thread inside `driver_micmap.dll` to prove WASAPI capture works inside the `vrserver.exe` DLL host on real Bigscreen Beyond + Win11 Pro hardware. The CONTEXT.md is locked across 22 decisions: reuse the existing `WASAPIAudioCapture` class unmodified, construct it on a new `AudioWorker` thread (so its existing `CoInitializeEx(MTA)` lands on a dedicated apartment), guard the entire path behind `enable_driver_audio` in `default.vrsettings` (default `false`), surface `RPC_E_CHANGED_MODE` distinctly in driver logs, and tear down in strict reverse order with a 2 s watchdog. No FFT, no SPSC ring, no detection, no `CommandQueue` push from audio — those are P7. SC4 demands byte-identical Phase 5 behavior when the flag is OFF.

The research questions are not "should we do this" (CONTEXT decided) but "how do we implement these decisions correctly without violating Pitfalls 1, 3, 4, 11, 13, 14 or the v1.5 SVR-05 invariant?" The answers concentrate on three load-bearing technical contracts: COM apartment per-thread rules with `RPC_E_CHANGED_MODE` semantics; WASAPI device handle lifetime across `Cleanup`→`Init` cycles inside a single vrserver process; and `IMMNotificationClient` callback survival of driver unload, mitigated by the `shared_ptr<State>` + `atomic<bool> alive` pattern.

**Primary recommendation:** Build `driver/src/audio_worker.{hpp,cpp}` as a self-contained owner of the entire COM/WASAPI lifecycle. The thread function is the only place `CoInitializeEx` is called; `CoInitializeEx`'s return value is checked against three buckets — `S_OK`/`S_FALSE` proceed, `RPC_E_CHANGED_MODE` log-and-bail, anything else log-and-bail. The existing `WASAPIAudioCapture` constructor is invoked **after** the worker-side `CoInitializeEx` succeeds, ensuring its inner `CoInitializeEx` returns `S_FALSE` (already-init same apartment) which the existing code already accepts. Teardown is the construction sequence reversed: `state->alive = false` → signal shutdown CV → `WASAPIAudioCapture::stopCapture()` → destruct the capture (runs `UnregisterEndpointNotificationCallback` + `CoUninitialize` on the same thread) → `CoUninitialize` for the worker's own init → thread exits → `AudioWorker::~AudioWorker()` joins. Bound the join with a 2 s watchdog mirroring the v1.5 `VREvent_Quit` precedent.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|--------------|----------------|-----------|
| Runtime flag read (`enable_driver_audio`) | Driver — `DeviceProvider::Init` | — | `vr::VRSettings()` is a driver-only API; reading once at Init avoids hot-reload complexity in P6 (D-01). |
| COM apartment ownership for WASAPI | Driver — `AudioWorker` thread | — | Pitfall 1 mandates the worker thread own its own `CoInitializeEx(MTA)`; `DeviceProvider::Init` and RunFrame must never touch COM (D-04, D-06, D-07). |
| WASAPI capture lifecycle | Shared lib — `WASAPIAudioCapture` (existing) | Driver — `AudioWorker` constructs/destructs on worker thread | D-04 reuses the class unmodified; placement of construction/destruction sites is a driver concern. |
| `IMMNotificationClient` registration | Shared lib — `WASAPIAudioCapture` ctor (existing) | Driver — `AudioWorker` provides alive-flag callback wrapper | Register/unregister code stays in shared lib; alive-flag mitigation is driver-only state (D-15, D-16). |
| Audio callback / RMS log | Driver — `AudioWorker` callback wired into `IAudioCapture::setAudioCallback` | — | RMS computation and DriverLog calls are driver-side spike code; shared lib stays generic (D-08, D-10). |
| Trigger emission to OpenVR | **Not in P6** | — | `CommandQueue` producer for audio is P7 (D-07); v1.5 SVR-05 invariant survives — only HTTP thread pushes in P6. |
| Cleanup ordering | Driver — `DeviceProvider::Cleanup` | — | `audioWorker_.reset()` first, then existing v1.5 sequence (HTTP, CommandQueue, HMD-handle, `VR_CLEANUP_SERVER_DRIVER_CONTEXT`) (D-13). |

## User Constraints (from CONTEXT.md)

### Locked Decisions (D-01 through D-22)

**Flag plumbing:**
- **D-01:** Flag at `driver/resources/settings/default.vrsettings`, key `driver_micmap.enable_driver_audio` (bool, default `false`). Read once in `DeviceProvider::Init` via `vr::VRSettings()->GetBool("driver_micmap", "enable_driver_audio", &err)`. Stored on `DeviceProvider` as `bool driverAudioEnabled_`. No hot-reload.
- **D-02:** Flag read does NOT drag `ConfigManager`/`nlohmann::json`/`AppConfig` into the driver. P8 owns that.
- **D-03:** Flag-OFF: `AudioWorker` is **never constructed**. No `std::thread`, no COM, no WASAPI calls. Byte-identical to Phase 5.

**Code reuse:**
- **D-04:** Reuse `IAudioCapture` factory unchanged. `WASAPIAudioCapture` ctor at `audio_capture.cpp:186-220` already calls `CoInitializeEx(MTA)`; the trick is constructing it on the audio worker thread.
- **D-05:** New driver-only file `driver/src/audio_worker.{hpp,cpp}`. `AudioWorker` owns: (1) `std::thread` whose entry constructs `IAudioCapture`, calls `start()`, drains callbacks, and on shutdown calls `stop()` + lets dtor run on the worker thread; (2) `std::shared_ptr<State>` with `atomic<bool> alive` + counters; (3) `condition_variable` + `atomic<bool> shutdown_`.
- **D-06:** `RPC_E_CHANGED_MODE` (`0x80010106`) handled in driver-side worker entry, NOT in shared lib. Worker calls `CoInitializeEx(MTA)` first; on `RPC_E_CHANGED_MODE` logs distinctly and exits the thread. Shared lib's inner `CoInitializeEx` then returns `S_FALSE` (already-init same apartment), which existing code accepts.
- **D-07:** No `vr::*` API call from worker. Worker writes only to `DriverLog` (thread-safe per OpenVR convention) and its own atomics. **No CommandQueue push from audio in P6.**

**Spike scope:**
- **D-08:** Audio callback computes RMS per buffer; logs first ~1 second of values to `vrserver.txt` (target ~100 lines at 10 ms WASAPI period). After budget met, callback continues draining frames but skips DriverLog writes.
- **D-09:** No SampleRing, no FFT, no state machine, no TapCommand push. Frames after RMS budget dropped on the floor.
- **D-10:** WASAPI capture thread (internal to `WASAPIAudioCapture`) and `AudioWorker` thread are distinct. Worker owns lifecycle; capture thread is internal.

**Device selection:**
- **D-11:** Open system default capture device (`eMultimedia` role) via existing `IAudioCapture` API. No `config.json` reading, no pinning, no enumeration UI. Pitfall 14 deferred — existing `OnDefaultDeviceChanged` is `S_OK` no-op.
- **D-12:** Device pinning is a Phase 8 concern.

**Lifecycle:**
- **D-13:** Cleanup order: (1) `audioWorker_.reset()` first — sets `state->alive = false`, signals shutdown, joins thread, with 2 s watchdog matching v1.5 `VREvent_Quit` precedent; (2) existing v1.5 sequence (`httpServer_->Stop()`, `commandQueue_.reset()`, HMD-handle reset, `VR_CLEANUP_SERVER_DRIVER_CONTEXT()`).
- **D-14:** Init order: bindings patch (existing) → CommandQueue → HttpServer (existing) → if flag set, construct `AudioWorker` last.

**IMMNotificationClient (Pitfall 13 / SC5):**
- **D-15:** Register on worker thread automatic via D-04 (existing ctor does the register call). Unregister in dtor (existing) runs on worker thread per D-13.
- **D-16:** Pitfall 13 alive-flag mitigation in driver-only code (`AudioWorker::State`). `AudioWorker` installs an `onDeviceRemoved` callback into the capture that captures `weak_ptr<State>` + checks `alive` before doing anything. Spike-grade; full ComPtr migration of `DeviceNotificationClient` deferred to P7.

**UAT (D-17):**
1. Flag-ON capture run on Bigscreen Beyond + Win11 Pro: confirm `vrserver.txt` shows worker thread constructed, `CoInitializeEx` outcome (S_OK or S_FALSE pass; `RPC_E_CHANGED_MODE` is bail-out path), WASAPI device opened, ~100 RMS lines covering ~1 s, capture stays alive ≥ 30 s.
2. HMD wake/sleep × 2 with run from (1) still active: worker survives, no leaked handles in Process Explorer, no crash.
3. SteamVR-restart-without-quit single cycle: `Cleanup` → `Init` in-process; second `Init` starts fresh worker + WASAPI session (no `AUDCLNT_E_DEVICE_IN_USE`); single cycle only — 50-cycle stress test is P7 SC4.
4. Flag-OFF regression: confirm `hmd_button_test.exe` against v1.5 trigger path (POST /button → CommandQueue → `/input/system/click`) toggles dashboard; byte-identical to Phase 5.

**D-18:** UAT artifacts → `06-UAT.md` with `vrserver.txt` excerpt covering RMS window. Process Explorer screenshot optional.

**Merge / hygiene:**
- **D-19/D-20:** Land on `main` branch with flag default OFF. P7 flips default ON when its SC are met.
- **D-21:** No `#ifdef MICMAP_DRIVER_BUILD` in shared lib (P5 D-12 invariant).
- **D-22:** No `__declspec(dllexport)/dllimport` (P5 D-13). `dumpbin /exports driver_micmap.dll` must continue to show only `HmdDriverFactory`.

### Claude's Discretion
- Exact RMS budget accounting (sample-count threshold vs steady-clock cutoff).
- DriverLog line format for RMS values.
- 2 s Cleanup watchdog implementation: `condition_variable::wait_for` vs detach-as-last-resort. Match v1.5 watchdog precedent.
- File layout under `driver/src/` for `AudioWorker` (single hpp/cpp pair vs split state struct).
- Whether to log resolved device's friendly name + sample rate at Init.

### Deferred Ideas (OUT OF SCOPE)
- SPSC SampleRing + audio→detection plumbing → P7.
- `OnDefaultDeviceChanged` follow-the-default + device pinning UI → P7+/P8.
- 50-cycle Init/Cleanup stress test → P7 SC4.
- `DeviceNotificationClient` ComPtr migration → P7.
- `config.json.audio.deviceId` reading from driver → P8.
- LIB-04 logger sink injection → P8.
- `RPC_E_CHANGED_MODE` shared-lib handling → P7+ if needed.
- `hmd_button_test.exe` retirement → at earliest P10.
- cpp-httplib v0.14.3 → v0.20.1 (CVE-2025-46728) → P8.

## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MIG-01 (partial) | "Driver hosts a dedicated audio worker thread that owns `CoInitializeEx(COINIT_MULTITHREADED)`, opens the configured WASAPI capture device, and pushes captured frames into a single-producer/single-consumer ring buffer." | P6 owns the **capture-only** half: dedicated audio worker thread, MTA `CoInitializeEx` ownership, WASAPI default-capture device opened. The SPSC ring + push portion is P7 (D-09). The Standard Stack, Architecture Patterns, and Common Pitfalls sections below provide the implementation contract for this partial coverage. |

## Project Constraints (from CLAUDE.md)

- Stack is locked this milestone: C++17, CMake, ImGui + D3D11, WASAPI, KissFFT, cpp-httplib, nlohmann/json, OpenVR SDK. **No framework changes in P6.**
- Bash via Git Bash; Unix-style paths in shell commands.
- Visual validation on real HMD is mandatory for VR-input exit criteria; build success does not substitute. Bigscreen Beyond + Win11 Pro is the canonical UAT rig.
- Phase artifacts (CONTEXT, RESEARCH, PLAN, UAT) are project memory across context resets — do not skip them.
- GSD config: YOLO mode, standard granularity, parallel execution, research/plan-check/verifier all enabled, Opus model profile.
- Windows-only; non-Windows audio stubs remain. P6 does not touch them.

## Standard Stack

### Core (already in tree — no new deps)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| WASAPI (`<mmdeviceapi.h>` + `<audioclient.h>`) | Windows 10/11 SDK | Audio capture endpoint, IMMNotificationClient, IAudioClient/IAudioCaptureClient | Native Windows audio API; existing `WASAPIAudioCapture` already wraps it. [VERIFIED: `src/audio/src/audio_capture.cpp:1-30` includes confirmed.] |
| `Microsoft::WRL::ComPtr` | Win10/11 SDK (`<wrl/client.h>`) | RAII for COM interface pointers | Project's preferred COM smart pointer per `.planning/codebase/ARCHITECTURE.md` and `audio_capture.cpp` usage. [VERIFIED] |
| OpenVR SDK | Same as v1.5 (vendored at `external/openvr` per build config) | `vr::VRSettings()`, `vr::VRDriverLog()`, lifecycle hooks | Driver context must use these exclusively for driver-host integration. [CITED: openvr/Driver_API_Documentation.md] |
| C++17 stdlib (`<thread>`, `<atomic>`, `<condition_variable>`, `<mutex>`, `<memory>`, `<chrono>`) | C++17 | Worker thread, shutdown signalling, alive flag, watchdog timing | Locked stack from PROJECT.md. No new threading lib needed. |
| `micmap::core_runtime` (INTERFACE target from P5) | Project-internal, P5 | Already linked PRIVATE into `driver_micmap` per P5 D-10; provides `IAudioCapture` interface + factory | Phase 5 explicitly preserved this link without driver TUs consuming it; P6 is the first driver TU consumer. [VERIFIED: `driver/CMakeLists.txt:80`] |

### Supporting

| Library | Purpose | When to Use |
|---------|---------|-------------|
| `vr::VRSettings()` (OpenVR) | Read driver config from `default.vrsettings` | Once at `Init` per D-01. **Do not** poll, do not call from RunFrame, do not read on the worker thread. |
| `vr::VRDriverLog()` / `DriverLog(...)` | Thread-safe driver logging to `vrserver.txt` | From any thread per OpenVR convention. **Only logging mechanism allowed from the audio worker thread per D-07.** |

### Alternatives Considered

| Instead of | Could Use | Why Not (in P6) |
|------------|-----------|-----------------|
| Reusing `WASAPIAudioCapture` constructor placement trick | Refactor to extract a `CoInitializeEx`-free constructor + explicit `init()` step | D-04 explicitly preserves the class unchanged. P5 SC5 byte-identical guarantee at the shared-lib layer is preserved by **not** touching shared lib. Refactor belongs to P7+ if it ever becomes load-bearing. |
| `vr::VRSettings()->GetBool` for the flag | Reading a JSON config file directly from `DeviceProvider::Init` | D-02 explicitly defers `nlohmann::json` + `ConfigManager` from the driver until P8. `VRSettings()` is driver-native and zero-dep. |
| Logging via shared lib `MICMAP_LOG_*` macros | `DriverLog(...)` directly | LIB-04 logger sink injection is deferred to P8. P6 audio worker uses raw `DriverLog` per D-07 / `code_context` note. The shared lib's `MICMAP_LOG_*` will route to whatever sink the binary configured at static-init time, which is unwired in the driver and therefore meaningless to call from driver code in P6. |
| Boost.Lockfree / atomic-shared_ptr for state | `std::shared_ptr<State>` + `std::atomic<bool>` + `weak_ptr` capture in callback lambda | Matches Pitfall 13 mitigation prescription verbatim; no new deps; C++17 sufficient. |

**Installation:** Nothing new. P5 already linked `micmap::core_runtime` PRIVATE into the driver target; P6 is the first phase to actually consume an `IAudioCapture` symbol from a driver TU. P5 D-10's "link-only, no `#include` from driver TUs" restriction is explicitly lifted in P6/P7.

**Version verification:** All libraries are in-tree at versions locked by P5; no `npm view` equivalent applies. The OpenVR SDK at `external/openvr` and KissFFT/cpp-httplib/nlohmann_json under `external/` are pinned by the project's CMake configuration. [VERIFIED: `.planning/codebase/STACK.md` and `driver/CMakeLists.txt` both confirm vendored deps; no version bumps in P6 per D-15 (cpp-httplib bump deferred to P8).]

## Architecture Patterns

### System Architecture Diagram

```
                       SteamVR start (vrserver.exe spawns)
                                     │
                                     ▼
                  ┌──────────────────────────────────┐
                  │  driver_micmap.dll loaded        │
                  │  HmdDriverFactory → DeviceProvider│
                  └──────────────┬───────────────────┘
                                 │
                                 ▼  vrserver calls IServerTrackedDeviceProvider::Init()
                  ┌──────────────────────────────────────────────────────────────────┐
                  │  DeviceProvider::Init (vrserver thread, apartment unknown)       │
                  │   1. VR_INIT_SERVER_DRIVER_CONTEXT                               │
                  │   2. PatchGenericHmdBindings                                     │
                  │   3. commandQueue_ = make_unique<CommandQueue>     [v1.5]        │
                  │   4. httpServer_ = make_unique<HttpServer>(...)    [v1.5]        │
                  │      httpServer_->Start()                                        │
                  │   5. driverAudioEnabled_ = VRSettings()->GetBool(                │
                  │           "driver_micmap","enable_driver_audio",&err)            │
                  │   6. if (driverAudioEnabled_)                                    │
                  │         audioWorker_ = make_unique<AudioWorker>(...)             │
                  │         audioWorker_->Start()    ← spawns std::thread            │
                  └──────────────┬───────────────────────────────────────────────────┘
                                 │
                  flag=false ────┼──────── flag=true
                  (SC4 path)     │
                  Phase-5         ▼
                  byte-identical  ┌─────────────────────────────────────────────┐
                  end             │  AudioWorker::ThreadEntry  (NEW thread)     │
                                  │  ─────────────────────────────────          │
                                  │  hr = CoInitializeEx(nullptr,               │
                                  │                  COINIT_MULTITHREADED)      │
                                  │   ├─ S_OK / S_FALSE   → proceed             │
                                  │   ├─ RPC_E_CHANGED_MODE                     │
                                  │   │     → DriverLog("...RPC_E_CHANGED_MODE  │
                                  │   │              — bailing out");           │
                                  │   │       state->last_error = ...;          │
                                  │   │       return; (thread exits)            │
                                  │   └─ other failure → DriverLog + bail       │
                                  │                                              │
                                  │  capture_ = createWASAPICapture()            │
                                  │       (existing ctor calls CoInitializeEx   │
                                  │        again, gets S_FALSE same apartment)  │
                                  │       (existing ctor RegisterEndpoint-      │
                                  │        NotificationCallback ← Pitfall 13)   │
                                  │                                              │
                                  │  capture_->setAudioCallback([state]         │
                                  │      (const float* s, size_t n) {           │
                                  │        if (auto sp = state.lock()) {        │
                                  │          if (!sp->alive) return;            │
                                  │          float rms = computeRMS(s,n);       │
                                  │          if (sp->rms_logs_emitted < BUDGET) │
                                  │            DriverLog("MicMap audio: rms=%f",│
                                  │                      rms);                  │
                                  │            ++sp->rms_logs_emitted;          │
                                  │        }                                    │
                                  │      })                                     │
                                  │                                              │
                                  │  capture_->selectDeviceById(<default>)      │
                                  │  capture_->startCapture()                   │
                                  │       └─ spawns INNER captureThread_        │
                                  │          which does its own                 │
                                  │          CoInitializeEx(MTA) per :521       │
                                  │                                              │
                                  │  // Wait for shutdown                        │
                                  │  std::unique_lock lk(mu_);                  │
                                  │  cv_.wait(lk, [&]{ return shutdown_; });    │
                                  │                                              │
                                  │  capture_->stopCapture()  (joins inner)     │
                                  │  capture_.reset()         (dtor on worker)  │
                                  │       └─ UnregisterEndpointNotification…    │
                                  │       └─ Release(notificationClient_)       │
                                  │       └─ enumerator_.Reset()                │
                                  │       └─ CoUninitialize()  (capture's own)  │
                                  │                                              │
                                  │  CoUninitialize()           (worker's own)  │
                                  │  return; (thread exits)                     │
                                  └─────────────────────────────────────────────┘

                  ─────── steady state ───────
                  vrserver RunFrame (~100 Hz)        v1.5 trigger path unchanged
                       │                              POST /button → HttpServer
                       │  drains CommandQueue         → CommandQueue.push(TapCommand)
                       │  → UpdateBooleanComponent    → drained by RunFrame
                       │     /input/system/click

                  ─────── shutdown (vrserver Cleanup or VREvent_Quit) ───────
                  DeviceProvider::Cleanup
                    1. audioWorker_.reset()  ← FIRST (new in P6)
                         └─ AudioWorker::~AudioWorker()
                              ├─ state->alive = false
                              ├─ shutdown_ = true; cv_.notify_one();
                              ├─ thread_.join() with 2 s watchdog
                              │    (worker thread runs the teardown
                              │     described in inner box above)
                              └─ if watchdog fires → log + thread_.detach() (last resort)
                    2. httpServer_->Stop(); httpServer_.reset();   [v1.5]
                    3. commandQueue_.reset();                      [v1.5]
                    4. HMD handle / state reset                    [v1.5]
                    5. VR_CLEANUP_SERVER_DRIVER_CONTEXT()          [v1.5]
```

### Recommended Project Structure

```
driver/
├── CMakeLists.txt                      # MODIFIED: add audio_worker.cpp to driver_micmap target
├── resources/
│   └── settings/
│       └── default.vrsettings          # MODIFIED: add "enable_driver_audio": false
├── src/
│   ├── driver_main.cpp                 # UNCHANGED: HmdDriverFactory glue
│   ├── device_provider.{hpp,cpp}       # MODIFIED: add bool driverAudioEnabled_,
│   │                                   #   unique_ptr<AudioWorker> audioWorker_;
│   │                                   #   Init reads flag + optionally constructs worker last;
│   │                                   #   Cleanup resets worker first
│   ├── audio_worker.hpp                # NEW: AudioWorker class + State struct +
│   │                                   #   Start/Stop interface
│   ├── audio_worker.cpp                # NEW: thread entry, CoInitializeEx outcome handling,
│   │                                   #   capture construction on worker thread,
│   │                                   #   RMS callback wiring, reverse-order teardown
│   ├── command_queue.hpp               # UNCHANGED: read-only context for P6 (P7 adds producer)
│   ├── http_server.{hpp,cpp}           # UNCHANGED in P6
│   ├── driver_log.hpp                  # UNCHANGED
│   └── ...
```

### Pattern 1: Construction-on-the-target-thread (COM apartment trick)

**What:** Move the construction site of `WASAPIAudioCapture` into the audio worker thread's entry function, instead of into `DeviceProvider::Init`. The class's existing constructor calls `CoInitializeEx(MTA)` — that call now lands on the worker thread, not on the vrserver-owned thread. The class is untouched; only the construction *site* changes.

**When to use:** Any time a class's constructor takes ownership of thread-affine resources (COM, OpenGL, GDI+) and the original design assumed an EXE host but the new host is a DLL with unknown thread apartment state. CONTEXT D-04 + spike rationale.

**Example:**

```cpp
// driver/src/audio_worker.cpp (new file — illustrative skeleton, not final code)

#include "audio_worker.hpp"
#include "driver_log.hpp"
#include <micmap/audio/audio_capture.hpp>   // P5 link-only restriction lifted in P6
#include <Objbase.h>                         // CoInitializeEx, RPC_E_CHANGED_MODE
#include <cmath>

namespace micmap::driver {

void AudioWorker::ThreadEntry() {
    // Worker-owned COM init. CONTEXT D-06: surface RPC_E_CHANGED_MODE distinctly.
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        DriverLog("MicMap: audio worker thread already in another COM apartment "
                  "(RPC_E_CHANGED_MODE = 0x80010106) — bailing out\n");
        state_->last_error = AudioWorkerError::ApartmentMismatch;
        return;
    }
    if (FAILED(hr) && hr != S_FALSE) {
        DriverLog("MicMap: audio worker CoInitializeEx failed hr=0x%08X\n",
                  static_cast<unsigned>(hr));
        state_->last_error = AudioWorkerError::ComInitFailed;
        return;
    }
    // hr is S_OK or S_FALSE — proceed.
    DriverLog("MicMap: audio worker thread COM apartment = MTA (hr=0x%08X)\n",
              static_cast<unsigned>(hr));

    // Construct WASAPIAudioCapture HERE — its constructor's own CoInitializeEx
    // returns S_FALSE (already-init same apartment) and the existing code accepts.
    auto capture = micmap::audio::createWASAPICapture();
    if (!capture) {
        DriverLog("MicMap: createWASAPICapture returned null\n");
        ::CoUninitialize();
        return;
    }

    // Wire RMS callback with weak_ptr<State> alive-flag check (Pitfall 13).
    std::weak_ptr<State> weak = state_;
    capture->setAudioCallback(
        [weak](const float* samples, size_t count) {
            auto sp = weak.lock();
            if (!sp || !sp->alive.load(std::memory_order_acquire)) return;
            // Compute RMS over this buffer
            double sumSq = 0.0;
            for (size_t i = 0; i < count; ++i) sumSq += samples[i] * samples[i];
            float rms = static_cast<float>(std::sqrt(sumSq / std::max<size_t>(count, 1)));
            // RMS budget: ~1 second worth of log lines (D-08)
            uint32_t emitted = sp->rms_logs_emitted.fetch_add(1, std::memory_order_relaxed);
            if (emitted < kRmsBudget) {
                DriverLog("MicMap audio: rms=%.6f (sample %u)\n", rms, emitted);
            }
        });

    // Open default capture device (D-11). Existing API: enumerate, find default, select.
    // (Exact call shape depends on selectDevice/selectDeviceById API on IAudioCapture.)
    if (!OpenDefaultCaptureDevice(*capture)) {
        DriverLog("MicMap: failed to open default capture device\n");
        capture.reset();         // dtor unregisters notification client + CoUninitialize
        ::CoUninitialize();      // worker's own CoInitializeEx
        return;
    }

    if (!capture->startCapture()) {
        DriverLog("MicMap: startCapture failed\n");
        capture.reset();
        ::CoUninitialize();
        return;
    }

    DriverLog("MicMap: audio capture started inside vrserver DLL host\n");

    // Wait for shutdown signal.
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this]{ return shutdown_.load(); });
    }

    DriverLog("MicMap: audio worker shutting down\n");

    // Reverse-order teardown — all on this same worker thread (Pitfall 4).
    capture->stopCapture();
    capture.reset();              // ~WASAPIAudioCapture: Unregister + Release + CoUninitialize
    ::CoUninitialize();           // matches the worker's own CoInitializeEx above

    DriverLog("MicMap: audio worker thread exiting cleanly\n");
}

} // namespace micmap::driver
```

### Pattern 2: Three-bucket COM apartment outcome handling

**What:** `CoInitializeEx`'s return must be handled in three buckets, not two. `S_OK` and `S_FALSE` both mean "you may use COM on this thread"; only `RPC_E_CHANGED_MODE` and other failures mean "stop." Treating `S_FALSE` as failure (because `FAILED(hr)` is false but `SUCCEEDED(hr) || hr == S_FALSE` is sometimes coded as a different branch) is a common bug. The existing `WASAPIAudioCapture::WASAPIAudioCapture` already gets this right at `audio_capture.cpp:197` (`comInitialized_ = SUCCEEDED(hr) || hr == S_FALSE`), but it does **not** distinguish `RPC_E_CHANGED_MODE` from other failures — that's the gap D-06 fills in driver-side code.

**When to use:** Every `CoInitializeEx` call site, especially in DLL host contexts where the calling thread's apartment is determined by an external host. [CITED: learn.microsoft.com — `CoInitializeEx` documents `S_OK`, `S_FALSE`, `RPC_E_CHANGED_MODE`, `E_INVALIDARG`, `E_OUTOFMEMORY`, `E_UNEXPECTED` as defined return values.]

**Example:** See Pattern 1 code block above — three explicit branches.

### Pattern 3: `shared_ptr<State>` + `atomic<bool> alive` for COM callbacks

**What:** Wrap any COM callback whose lifetime is not under your control (here: `IMMNotificationClient` callbacks invoked on a system-managed MMDevice notifier thread) so it captures a `weak_ptr<State>` instead of `this`. The callback locks the weak_ptr; if it converts, it then checks `alive`; only if `alive` does it touch any state. On teardown, set `alive = false` BEFORE calling `UnregisterEndpointNotificationCallback`, then drop the shared_ptr. Even if a callback is mid-flight at the moment of unregister, it sees `alive == false` and bails before touching destroyed members. Pitfall 13 verbatim.

**When to use:** Every COM callback registered with a system service whose unregister contract does not synchronously wait for in-flight callbacks. `IMMNotificationClient` is the canonical case in P6.

**Example:**

```cpp
// driver/src/audio_worker.hpp (new)

namespace micmap::driver {

enum class AudioWorkerError { None, ApartmentMismatch, ComInitFailed,
                              EnumeratorFailed, DeviceOpenFailed, CaptureStartFailed };

struct AudioWorkerState {
    std::atomic<bool> alive{true};
    std::atomic<uint32_t> rms_logs_emitted{0};
    AudioWorkerError last_error{AudioWorkerError::None};
};

class AudioWorker {
public:
    explicit AudioWorker();
    ~AudioWorker();   // signals shutdown, joins thread with 2 s watchdog

    void Start();     // launches std::thread running ThreadEntry()
    // No public Stop() — destructor owns shutdown semantics.

private:
    void ThreadEntry();

    std::shared_ptr<AudioWorkerState> state_;
    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_{false};

    static constexpr uint32_t kRmsBudget = 100;             // D-08 ~1 s @ 10 ms WASAPI period
    static constexpr auto kJoinWatchdog = std::chrono::seconds(2);   // D-13 v1.5 precedent
};

} // namespace micmap::driver
```

The `state_->alive` flag is captured by `weak_ptr` into the audio callback (Pattern 1 example). The same pattern should be installed into the `onDeviceRemoved` callback site if Phase 6 chooses to surface device removal — although per D-16, "spike-grade alive-flag is enough for P6," meaning the audio callback alive-flag alone is sufficient; the `IMMNotificationClient` register/unregister timing inside `WASAPIAudioCapture::~WASAPIAudioCapture` is correct provided the dtor runs on the worker thread (D-13).

### Pattern 4: Reverse-order teardown bounded by a watchdog

**What:** Construction order: bindings patch → CommandQueue → HttpServer → AudioWorker. Destruction order: AudioWorker → HttpServer → CommandQueue → HMD-handle reset → `VR_CLEANUP_SERVER_DRIVER_CONTEXT`. The **AudioWorker destructor itself** runs the audio-side teardown in reverse: signal `state->alive = false` → notify shutdown CV → `WASAPIAudioCapture::stopCapture()` (which joins inner WASAPI thread + Stops audio client) → `WASAPIAudioCapture::~WASAPIAudioCapture()` (which `UnregisterEndpointNotificationCallback`s, releases ComPtrs, calls `CoUninitialize` on this same thread) → worker's own `CoUninitialize` → thread exits → `AudioWorker::~AudioWorker` joins. Bounded by 2 s; on timeout, log + detach the thread (last resort).

**When to use:** Any place a thread owns COM + system-callback registrations + a thread-affine resource. CONTEXT D-13. v1.5 precedent: the `VREvent_Quit` shutdown watchdog at the SteamVR-shutdown boundary.

**Example:**

```cpp
// driver/src/audio_worker.cpp

AudioWorker::~AudioWorker() {
    if (!thread_.joinable()) return;

    // 1. Tell callbacks to stop touching our state.
    if (state_) state_->alive.store(false, std::memory_order_release);

    // 2. Signal the worker thread to exit its wait.
    {
        std::lock_guard<std::mutex> lk(mu_);
        shutdown_.store(true);
    }
    cv_.notify_one();

    // 3. Bounded join. v1.5 VREvent_Quit precedent uses a 2 s watchdog.
    auto start = std::chrono::steady_clock::now();
    auto deadline = start + kJoinWatchdog;

    // std::thread::join() is unbounded; we approximate with a polled wait.
    // A cleaner approach: run the teardown on a dedicated detached signaller
    // thread that times out. For P6 spike, polling join is acceptable.
    std::thread joiner([this]{ thread_.join(); });
    if (joiner.joinable()) {
        // Best-effort bounded wait (no native timed_join in C++17; this is
        // a pattern, not a guarantee — see Common Pitfalls §"Watchdog polling").
        // Detach as last resort.
        joiner.detach();
    }
    // For full v1.5-shape watchdog: spawn a one-shot timer thread that
    // signals a sentinel; if the worker thread hasn't joined when the timer
    // fires, log "WARNING: audio worker did not join within 2s — detaching"
    // and continue. vrserver process exit will reap the detached thread.
}
```

### Anti-Patterns to Avoid

- **`CoInitializeEx` from `DeviceProvider::Init`:** vrserver's calling thread apartment is undocumented and version-dependent. Pitfall 1. Always defer to the worker thread.
- **`vr::*` API calls from the audio worker thread:** Pitfall 3. Even reading `VRSettings()` from the worker is forbidden by D-07 (and unnecessary — flag is read once at Init). The only OpenVR symbol allowed from the worker is `DriverLog`.
- **Construct `WASAPIAudioCapture` on `Init` thread, then move to worker:** This places `CoInitializeEx` on the wrong thread. ComPtr-held interfaces are apartment-affine; subsequent calls from a different apartment marshal incorrectly or fail.
- **Push to `CommandQueue` from the audio worker thread in P6:** D-09 explicitly defers this to P7. The v1.5 SVR-05 invariant must survive P6 unchanged: HTTP thread is the only producer.
- **Treat `S_FALSE` as failure:** Common bug. `S_FALSE` from `CoInitializeEx` means "already initialized in same apartment, no-op succeeded" — proceed normally.
- **Skip the alive-flag because "we always join the worker":** `IMMNotificationClient` callbacks run on a system thread the driver does not own, and `UnregisterEndpointNotificationCallback` does not synchronously wait for in-flight callbacks. Even with a clean join of the worker thread, the unregister can race with an in-flight notification.
- **Hot-reload the flag:** D-01 explicitly forbids this. Future phases may add it; P6 reads once at Init.
- **Add `MICMAP_DRIVER_BUILD` ifdef anywhere in `src/`:** P5 D-12 invariant. P6 lives entirely in `driver/src/`. CI grep at `grep -rn 'MICMAP_DRIVER_BUILD' src/audio/ src/detection/ src/core/ src/common/` must continue to return zero.
- **Add `__declspec(dllexport)` to `IAudioCapture` or any shared header:** P5 D-13 invariant. `dumpbin /exports driver_micmap.dll` must continue to show only `HmdDriverFactory`.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WASAPI capture event loop | A new event-driven WASAPI loop | Existing `WASAPIAudioCapture::captureLoop` at `audio_capture.cpp:519-532` | Already proven in `mic_test.exe`; correct `WaitForSingleObject` pattern; correct `CoInitializeEx` on the inner capture thread (D-04 reuses unchanged). |
| `IMMNotificationClient` register/unregister | A new notification client | Existing `DeviceNotificationClient` in `audio_capture.cpp:116-179` | Already register/unregister-paired in ctor/dtor (`audio_capture.cpp:217` and `:225-228`). Manual `InterlockedIncrement/Decrement` is flagged in CONCERNS.md but explicitly **not refactored in P6** (Pitfall 13 mitigation is the alive-flag, not the ref-count rework — D-16). |
| Bounded thread-safe queue between threads | A new mutex+deque or SPSC ring | Existing `CommandQueue` (`driver/src/command_queue.hpp`) for HTTP→RunFrame; **no new queue in P6** | P6 adds zero new producers. SPSC ring for audio→detection is P7's problem (D-09). |
| Logger sink for `vrserver.txt` | A new sink | `DriverLog(...)` macro from `driver/src/driver_log.hpp` (already null-safe pre-context per `:33`) | Thread-safe per OpenVR convention. LIB-04 sink injection is P8's job. |
| Configuration parser for the flag | A new config reader | `vr::VRSettings()->GetBool` | Driver-native; one call; no new dep; D-01 locks this. |
| RMS computation | A new DSP utility | Inline `std::sqrt(sum_of_squares / count)` in the callback | RMS over a small buffer is ~10 lines; pulling a DSP lib for it is overkill. The existing `apps/micmap/main.cpp:353-444` (to be deleted at P10) has the prior-art shape. |
| Watchdog timer for thread join | A new `timed_join` primitive | `condition_variable::wait_for` with a deadline OR a sentinel-thread-detach pattern matching the v1.5 `VREvent_Quit` precedent | C++17 `std::thread` lacks native `timed_join`; `wait_for` on a CV that the worker signals on completion is the canonical workaround. |

**Key insight:** Phase 6 deliberately reuses every shared-lib mechanism rather than introducing parallel implementations in driver code. The only new code in `driver/src/` is `AudioWorker` itself — a thread-and-state owner that orchestrates the existing pieces in the correct order. Spike discipline: minimal new surface, maximum reuse.

## Runtime State Inventory

Phase 6 is a feasibility spike that adds new files and modifies a vrsettings file. It is not a rename / refactor / migration of existing keys, IDs, or stored state. Each category is checked explicitly per the protocol.

| Category | Items Found | Action Required |
|----------|-------------|------------------|
| Stored data | None — verified by grepping `enable_driver_audio` and `driver_audio` across the tree (zero hits before P6); no databases, no persisted config keys are renamed. The new flag key is being **added**, not migrated. | None. |
| Live service config | None — no Datadog/Tailscale/Cloudflare equivalents. The driver's own `default.vrsettings` is the live-service-config equivalent and is git-tracked under `driver/resources/settings/default.vrsettings` (D-01 modifies this file in tree). | None — file change committed via the same install pipeline as v1.5. |
| OS-registered state | None — no Windows Task Scheduler, pm2, launchd, or systemd registrations involve audio worker state. The driver itself is registered via `vrpathreg adddriver` (handled by Phase 4 installer / P5 carryover); P6 does not change driver registration. | None. |
| Secrets and env vars | None — P6 introduces no secrets, no env vars. `vr::VRSettings()` reads from the in-tree `default.vrsettings` plus the user's per-machine `steamvr.vrsettings` overlay (managed by SteamVR itself). | None. |
| Build artifacts / installed packages | The installer (Phase 4 carryover) copies `default.vrsettings` to `{SteamVR}\drivers\micmap\resources\settings\default.vrsettings`. Existing user installs from Phase 5 will have the **old** vrsettings file without the new key — `vr::VRSettings()->GetBool` with a missing key returns the default (`false` per D-01), so legacy installs are forward-compatible by construction. | None for user-side; verify `driver/CMakeLists.txt` POST_BUILD copies the updated vrsettings into the staging tree (it already does, lines 147-149) and the installer pulls from the staging tree (Phase 4 D-01 layout). |

**The canonical question:** *After every file in the repo is updated, what runtime systems still have the old string cached, stored, or registered?* Answer: nothing — P6 adds new state, does not rename old state. The only forward-compat hazard is "user installs new driver DLL but `default.vrsettings` is not updated," which is mitigated by the missing-key default (`false`) returning the SC4 byte-identical-Phase-5 path.

## Common Pitfalls

### Pitfall 1: `CoInitializeEx` called on the wrong thread

**What goes wrong:** Calling `CoInitializeEx` on `DeviceProvider::Init`'s vrserver-owned thread returns `RPC_E_CHANGED_MODE` (vrserver may have already initialized that thread as STA). Code that then proceeds with `IMMDeviceEnumerator` calls either fails outright (`CO_E_NOTINITIALIZED`) or — worse — succeeds and hands back interface pointers that get marshaled wrong because the apartment doesn't match. Crash 30 seconds later in unrelated stack frame.

**Why it happens:** COM apartment rules are per-thread, not per-process. SteamVR does not document its threads' apartment state; it varies by SteamVR version.

**How to avoid:** D-04 + D-06 + D-07. `CoInitializeEx` lives in three places: (a) the worker's own thread entry (D-06 explicit), (b) the inner `WASAPIAudioCapture` constructor (existing, D-04 unchanged — runs on the worker thread because of where it's constructed), (c) the inner `WASAPIAudioCapture::captureLoop` at `audio_capture.cpp:521` (existing — runs on the inner WASAPI capture thread). All three are MTA. **No** `CoInitializeEx` on the `Init` thread, on the RunFrame thread, or on the HTTP thread.

**Warning signs:** `RPC_E_CHANGED_MODE` (`0x80010106`) in driver logs at startup; `CO_E_NOTINITIALIZED` (`0x800401F0`) on `IMMDeviceEnumerator::EnumAudioEndpoints` calls; random crashes in `combase.dll` minutes after capture starts; audio worker thread exits silently with no log line.

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 1 + `audio_capture.cpp:196` + Microsoft `CoInitializeEx` documentation]

### Pitfall 3: Audio thread calls `UpdateBooleanComponent` directly

**What goes wrong:** "Easy" implementation: detection thread sees a confidence spike → calls `VRDriverInput()->UpdateBooleanComponent` directly. Compiles, fires once, ships. Then HMD reactivates, the v1.5 reactivation handler in `RunFrame` flips state to `Invalidated`, but the audio thread doesn't see that flip and keeps trying to update the dead handle. Symptom A: silent failure. Symptom B: SEGV in `vrserver.exe`. Symptom C: deadlock at SteamVR shutdown.

**Why it happens:** OpenVR's `IVRDriverInput` is documented as "expected from the same thread as `RunFrame`." Cross-thread use is not API-stable.

**How to avoid:** D-07 (no `vr::*` from worker) + the explicit "P6 does not introduce a new CommandQueue producer" wording. P6 doesn't even *try* to push from audio — that's deferred to P7 by design. The only OpenVR symbol the worker touches is `DriverLog`.

**Warning signs:** A grep of `driver/src/` finds `VRDriverInput`/`VRProperties`/`VRServerDriverHost`/`VRSettings` outside `device_provider.cpp` and `manifest_registrar.cpp`. (P5 already has this implicitly; P6 adds `audio_worker.cpp` which must NOT match this grep — see Validation Architecture §"Static check.")

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 3 + `driver/src/device_provider.cpp:113-204` (RunFrame is the only call site for OpenVR input writes) + v1.5 SVR-05 invariant from `.planning/milestones/v1.5-ROADMAP.md`]

### Pitfall 4: WASAPI device handle leak across `Cleanup`/`Init` cycles

**What goes wrong:** SteamVR can call `Cleanup()` then `Init()` again within the same vrserver process (HMD reactivation cycles, "Restart SteamVR" without quitting vrserver). If `Cleanup()` doesn't (1) stop the capture thread (clean join, not detach), (2) release `IAudioCaptureClient`/`IAudioClient`/`IMMDevice`, (3) `UnregisterEndpointNotificationCallback`, (4) `CoUninitialize` on the same thread that called `CoInitializeEx`, then `Init()` re-creates a parallel session. `IAudioClient::Initialize` returns `AUDCLNT_E_DEVICE_IN_USE` because the old session still holds the device. Audio appears dead in the new session.

**Why it happens:** WASAPI ownership is asymmetric. `IAudioClient::Stop` is synchronous, but the underlying audio-engine reference releases via COM `Release()` which may defer cleanup. COM notifiers run on system-managed threads that don't synchronize with destructors.

**How to avoid:** D-13 reverse-order teardown. Worker thread's exit path runs the four steps inline before the thread returns: `capture->stopCapture()` (joins inner thread, stops `IAudioClient`), `capture.reset()` (dtor runs `UnregisterEndpointNotificationCallback`, releases ComPtrs, calls `CoUninitialize` for the capture's own init), then worker's own `CoUninitialize`, then thread exits. `AudioWorker::~AudioWorker` joins the worker thread bounded by 2 s. UAT D-17(3) is the single-cycle Init/Cleanup spot-check; full 50-cycle stress test is P7 SC4.

**Warning signs:** Second SteamVR session after "Restart SteamVR" produces no audio in driver, but driver log shows capture started; Process Explorer shows `driver_micmap.dll` keeping a handle on the audio device after `Cleanup()`; `AUDCLNT_E_DEVICE_IN_USE` in `vrserver.txt` on the second `Init`.

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 4 + `audio_capture.cpp:222-234` (existing dtor sequence) + `audio_capture.cpp:441-470` (existing `stopCapture` joins inner thread)]

### Pitfall 11: v1.5 priors recur with new shapes

**What goes wrong:** v1.5 paid for several lessons (atomic config write, `VR_Init` reentry, `IsApplicationInstalled` poll guard, `AcknowledgeQuit_Exiting`). Each can recur in v1.6 in a new place if not actively defended.

**Why it happens:** Institutional memory degrades. New code paths don't naturally cite v1.5 commit history.

**How to avoid:** P6 specifically: `vr::VRSettings()->GetBool` is a safe single-read pattern (matches v1.5 atomic-config-read shape). `DriverLog` is already null-safe pre-context per `driver_log.hpp:33`. The new `AudioWorker` lifecycle integrates into the existing `DeviceProvider::Init`/`Cleanup` shape that already survived v1.5's `VREvent_Quit` ack-first discipline.

**Specific check for P6:** The `default.vrsettings` modification (D-01 adds `enable_driver_audio: false`) does not introduce any new `MoveFileExW` or `ReplaceFileW` callsite — it is a static file copied via the existing CMake POST_BUILD command. No new atomic-write path; no Pitfall 11(4) recurrence.

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 11 + `driver/src/driver_log.hpp:33` + `driver/CMakeLists.txt:147-149`]

### Pitfall 13: `IMMNotificationClient` callback fires after driver is unloaded

**What goes wrong:** `DeviceNotificationClient` (registered with `IMMDeviceEnumerator::RegisterEndpointNotificationCallback` at `audio_capture.cpp:217`) is called on a system-managed thread. If `Cleanup()` returns before that callback's `OnDeviceStateChanged` finishes — or if the callback is mid-flight at the moment of unregister — it dereferences freed memory. Use-after-free across threads.

**Why it happens:** `UnregisterEndpointNotificationCallback` is documented to "no longer call the callback after returning" but does NOT synchronously wait for in-flight callbacks to complete. COM notifier-thread cleanup is best-effort.

**How to avoid:** D-15 + D-16. `AudioWorker` owns a `shared_ptr<State>` with `atomic<bool> alive`. The audio callback (and any device-removed forwarding) captures `weak_ptr<State>` and checks `alive` before doing anything. On teardown: set `alive = false` BEFORE signalling the worker shutdown, so any in-flight notification reads `alive == false` and bails before touching destroyed members.

**Warning signs:** Crash during SteamVR shutdown when an audio device was just unplugged; dump backtrace ends in `mmdevapi.dll` calling our callback; "Restart SteamVR" while unplugging the mic crashes vrserver.

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 13 + `audio_capture.cpp:116-179` + `audio_capture.cpp:217` + `audio_capture.cpp:225-228`]

### Pitfall 14: `OnDefaultDeviceChanged` makes the default mic move under the driver

**What goes wrong:** User has headset mic as default; trains MicMap; later plugs in USB mic; Windows auto-changes default capture device; MicMap silently captures from the wrong device. Trained thresholds are device-specific; detection fires randomly or not at all.

**Why it happens:** `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eCapture, eMultimedia)` is dynamic. The existing `DeviceNotificationClient::OnDefaultDeviceChanged` at `audio_capture.cpp:166-169` is a `S_OK` no-op.

**How to avoid (in P6):** **Explicitly defer.** D-11 + D-12 lock P6 to "open whatever the system default capture device is at Init time" with no follow-the-default behavior. The existing `S_OK` no-op is fine for a single-cycle spike. P7+/P8 own the device-pinning UI/IPC work. Do **not** write any `OnDefaultDeviceChanged` follow-the-default logic in P6 — that's scope creep.

**Warning signs in P6 UAT:** If during D-17(2) HMD wake/sleep cycle the user plugs in a different USB mic, the spike will silently follow Windows' default-device decision (because we use `eMultimedia` role). This is **expected P6 behavior** and not a bug — it just means the RMS readings might switch to a different device's signal. UAT instructions should call out: *do not change the default audio device during D-17 testing; if you do, the RMS readings may shift and that is not a P6 defect.*

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 14 + `audio_capture.cpp:166-169` (existing no-op confirmed)]

### Pitfall (new for P6 spike): `vr::VRSettings()->GetBool` reentrancy / thread-safety

**What goes wrong:** `vr::VRSettings()` is one of the OpenVR driver-side service interfaces. There is no documented thread-safety guarantee at the level of "you may call this concurrently with `RunFrame`" but it is documented as callable from `Init` (which is the canonical pattern).

**Why it happens:** `vr::VRSettings()` is rarely exercised in this codebase — `default.vrsettings` keys (`enable`, `http_port`, `http_host`) exist in the file (`driver/resources/settings/default.vrsettings:1-7`) but are not yet read by any code. P6 introduces the **first** read.

**How to avoid:** D-01: read once at `DeviceProvider::Init`, on the same thread vrserver invoked us on. Do not call from RunFrame. Do not call from the HTTP thread. Do not call from the audio worker thread (D-07). Store the bool result on `DeviceProvider::driverAudioEnabled_` and read that flag thereafter.

**Warning signs:** None expected if discipline is maintained. If a future phase needs hot-reload, that's a P7+ concern with its own threading review.

[ASSUMED: OpenVR `IVRSettings::GetBool` thread-safety contract is not explicitly documented as thread-safe; D-01 reads once at Init which sidesteps the question entirely. The conservative default — call it on the same thread that received the `Init` callback — is the safe choice.]

### Pitfall (new for P6 spike): `DriverLog` rate-limit / vrserver.txt flooding

**What goes wrong:** WASAPI shared-mode default period is ~10 ms. If the audio callback writes a `DriverLog` line per buffer indefinitely, after a 30-second UAT run the log holds ~3,000 RMS lines plus everything else; after an extended session, it's tens of thousands. `vrserver.txt` is shared with all other drivers and SteamVR's own logging; flooding it makes it unreadable.

**Why it happens:** Spike-grade code prioritizes "we can see the data" over rate limiting.

**How to avoid:** D-08: only log the **first ~1 second** of RMS values (a budget of ~100 lines at 10 ms period). After the budget is met, the audio callback continues to drain WASAPI buffers (so the audio engine doesn't overflow its internal ring) but skips DriverLog writes. Implementation: an `atomic<uint32_t> rms_logs_emitted` on the State struct + a `kRmsBudget = 100` constant; `fetch_add` and compare each call.

**Warning signs:** `vrserver.txt` exceeds 100 MB after a single SteamVR session; SteamVR's own logging is delayed because of disk I/O contention; UAT becomes unparseable.

[VERIFIED: `.planning/research/PITFALLS.md` Pitfall 12 (RunFrame budget) is the parallel concern on the OpenVR side; P6 doesn't push to CommandQueue so RunFrame isn't affected. The vrserver.txt flood is a P6-specific spike concern addressed by the budget.]

## Code Examples

### `default.vrsettings` modification (D-01)

```jsonc
// driver/resources/settings/default.vrsettings — MODIFIED
{
    "driver_micmap": {
        "enable": true,
        "http_port": 27015,
        "http_host": "127.0.0.1",
        "enable_driver_audio": false   // P6 D-01: feature flag, default OFF
    }
}
```

[VERIFIED: existing file at `driver/resources/settings/default.vrsettings:1-7`; addition is a single key in the existing `driver_micmap` section.]

### `DeviceProvider::Init` flag-read insertion (D-01, D-14)

```cpp
// driver/src/device_provider.cpp — MODIFIED
EVRInitError DeviceProvider::Init(IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    DriverLog("MicMap driver initializing (sidecar mode)\n");

    (void)micmap::bindings::PatchGenericHmdBindings(driverLogSink);   // existing

    commandQueue_ = std::make_unique<CommandQueue>();                  // existing
    httpServer_   = std::make_unique<HttpServer>(*commandQueue_);      // existing
    if (!httpServer_->Start()) {
        DriverLog("MicMap: failed to start HTTP server\n");
        return VRInitError_Driver_Failed;
    }
    DriverLog("MicMap: HTTP server listening on port %d\n", httpServer_->GetPort());

    // ── P6 NEW: read enable_driver_audio flag once (D-01) ──
    EVRSettingsError settingsErr = VRSettingsError_None;
    driverAudioEnabled_ = vr::VRSettings()->GetBool(
        "driver_micmap", "enable_driver_audio", &settingsErr);
    if (settingsErr != VRSettingsError_None) {
        // Key missing in user's default.vrsettings — accept default (false).
        DriverLog("MicMap: enable_driver_audio not set, defaulting to false "
                  "(VRSettingsError=%d)\n", static_cast<int>(settingsErr));
        driverAudioEnabled_ = false;
    }
    DriverLog("MicMap: enable_driver_audio = %s\n",
              driverAudioEnabled_ ? "true" : "false");

    // ── P6 NEW: optionally construct AudioWorker last (D-14) ──
    if (driverAudioEnabled_) {
        audioWorker_ = std::make_unique<AudioWorker>();
        audioWorker_->Start();
    }

    initialized_ = true;
    return VRInitError_None;
}
```

[CITED: existing `DeviceProvider::Init` at `driver/src/device_provider.cpp:58-79` is the splice point.]

### `DeviceProvider::Cleanup` reverse-order modification (D-13)

```cpp
// driver/src/device_provider.cpp — MODIFIED
void DeviceProvider::Cleanup() {
    if (!initialized_) {
        return;
    }

    DriverLog("MicMap driver cleaning up...\n");

    // ── P6 NEW: tear down audio FIRST (D-13) ──
    if (audioWorker_) {
        audioWorker_.reset();   // dtor: alive=false, signal shutdown,
                                // join thread w/ 2s watchdog
    }

    // ── existing v1.5 sequence, unchanged ──
    if (httpServer_) {
        httpServer_->Stop();
        httpServer_.reset();
    }
    commandQueue_.reset();

    hSystemClick_ = k_ulInvalidInputComponentHandle;
    state_ = HmdComponentState::NotReady;
    pendingReleaseAt_.reset();
    isPressed_ = false;
    lastWrittenValue_ = false;
    initLogged_ = false;
    loggedAwaitingHmd_ = false;
    profilePropsWritten_ = false;
    initialized_ = false;

    VR_CLEANUP_SERVER_DRIVER_CONTEXT();

    DriverLog("MicMap driver cleanup complete\n");
}
```

[CITED: existing `DeviceProvider::Cleanup` at `driver/src/device_provider.cpp:81-107`.]

### `device_provider.hpp` member additions

```cpp
// driver/src/device_provider.hpp — MODIFIED (additions only)
namespace micmap::driver {

class AudioWorker;   // forward decl (new)

class DeviceProvider : public vr::IServerTrackedDeviceProvider {
    // ... existing public/private members ...

private:
    // ... existing members ...

    // ── P6 NEW (D-01, D-14) ──
    bool driverAudioEnabled_{false};
    std::unique_ptr<AudioWorker> audioWorker_;
};

}
```

### `driver/CMakeLists.txt` modification (TU registration)

```cmake
# driver/CMakeLists.txt — MODIFIED line 23-27
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
    src/audio_worker.cpp        # P6 NEW
)
```

No new link dependencies — `IAudioCapture` is already pulled via `micmap::core_runtime` PRIVATE link from P5 D-10 (`driver/CMakeLists.txt:80`).

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| v1.5: audio capture lives in `micmap.exe`; trigger crosses HTTP via `POST /button` | v1.6: audio capture migrating into `driver_micmap.dll`; Phase 6 is the feasibility gate before Phase 7 commits to in-process detection | v1.6 milestone start (this milestone) | If P6 fails, the architecture migration needs reassessment; if it succeeds, P7 can proceed with confidence. |
| `DeviceProvider::Init` does no `vr::VRSettings()` reads (none of `enable`, `http_port`, `http_host` are code-consumed) | `DeviceProvider::Init` reads `enable_driver_audio` via `vr::VRSettings()->GetBool` | P6 D-01 | Establishes the read pattern for future driver-side flag work (P7+ may extend). |
| `WASAPIAudioCapture` constructor runs on the EXE's main thread (in `mic_test.exe` and v1.5 `micmap.exe`) | `WASAPIAudioCapture` constructor runs on a dedicated audio worker thread spawned inside the DLL host | P6 D-04 + D-05 | Same code, different construction context. The class is unmodified; only the call site changes. |
| `IMMNotificationClient` callback safety: bare `this`-capturing lambda | `weak_ptr<State>` capture with `atomic<bool> alive` check | P6 D-15 + D-16 (driver-side wrapper only; shared lib unchanged) | Pitfall 13 mitigation in driver-only code. Spike-grade; full ComPtr migration of `DeviceNotificationClient` deferred to P7. |

**Deprecated/outdated for P6:**
- The "POST `/button` from app to driver" trigger path remains the **only** v1.5 trigger path during P6. SC4 demands flag-OFF byte-identical-to-Phase-5 behavior — `hmd_button_test.exe` against this path is the regression harness in D-17(4). Trigger-path collapse is P10's job (per `MIG-05` in REQUIREMENTS.md).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `vr::VRSettings()->GetBool` is safe to call from the same thread that received the `Init` callback (i.e., the thread vrserver uses to invoke `IServerTrackedDeviceProvider::Init`). | Common Pitfalls §"VRSettings reentrancy" + Code Examples §`Init` insertion | Low. If somehow VRSettings throws / is null, the missing-key default path returns `false` and SC4 still holds. The error path (`VRSettingsError != None`) is logged. |
| A2 | The 2 s shutdown watchdog precedent from v1.5 (`VREvent_Quit` shutdown) is the right magnitude for the audio worker join. | Pattern 4 + D-13 | Low. If the worker can't tear down WASAPI in 2 s, something is badly wrong (network audio devices, driver bug); detach-as-last-resort lets vrserver exit reap the thread. |
| A3 | WASAPI shared-mode default period at 48 kHz is ~10 ms, so a budget of 100 RMS log lines covers ~1 second per D-08. | Common Pitfalls §"DriverLog flooding" + Pattern 1 example | Low. The actual buffer period varies by device and Windows version. The "approximate" wording in D-08 already grants discretion to switch to a steady-clock cutoff if 100 lines turns out to be too few or too many. |
| A4 | `WASAPIAudioCapture::~WASAPIAudioCapture()` runs on the same thread as the constructor (i.e., the worker thread in P6's design), because `unique_ptr<IAudioCapture>` is destroyed at the worker thread's stack-frame exit (D-13 wording: "lets the `IAudioCapture` destructor run on the worker thread"). | Pattern 4 (teardown) | Low. C++ `unique_ptr` destruction is deterministic at scope exit. The only risk is if `AudioWorker` accidentally moves the unique_ptr to a different thread before reset — the design keeps the unique_ptr local to `ThreadEntry()` precisely to avoid this. |
| A5 | Adding `audio_worker.cpp` to the driver target does not change `dumpbin /exports driver_micmap.dll`'s output (only `HmdDriverFactory` exported). | D-22 + Anti-Patterns | Very low. The new file uses no `__declspec(dllexport)`. As long as P5 D-13 invariant holds, the dumpbin assertion remains green. Verify in CI / build verification step. |
| A6 | `vr::VRDriverLog()` and the `DriverLog(...)` macro is thread-safe across the audio worker, RunFrame, HTTP, and inner WASAPI capture threads (so RMS log writes from the audio callback do not corrupt the v1.5 driver-side log lines from RunFrame). | D-07 + Pattern 1 example | Low. OpenVR convention treats `DriverLog` as thread-safe (driver-host serializes writes). If contention causes interleaving, lines may garble but the process won't crash. |
| A7 | The `IAudioCapture` API exposes a method to open the system default capture device without manually calling `IMMDeviceEnumerator::GetDefaultAudioEndpoint`. The existing class has `selectDevice(namePattern)` and `selectDeviceById(deviceId)` (per `audio_capture.hpp:42-51`). Opening "the default" likely means: call `enumerateDevices()`, find the entry with `isDefault == true`, then `selectDeviceById(that.id)`. | D-11 + Pattern 1 example | Low. If the public API doesn't expose a one-call "open default" helper, the worker entry can do the enumerate-and-find-default dance inline. The existing `enumerateDevices()` at `audio_capture.cpp:237-279` already populates `isDefault` flags via `GetDefaultAudioEndpoint(eCapture, eConsole, ...)` — note `eConsole` not `eMultimedia`; D-11 says `eMultimedia`, so the planner should confirm whether the existing class's "default" flag matches D-11's intent or whether the worker needs to enumerate + pick `eMultimedia` itself. |

**Risk-of-being-wrong assessment:** Every assumption above is low-risk. The spike's success hinges on Pitfall 1 / Pitfall 4 mitigations being correctly implemented in `audio_worker.cpp`, not on any of A1-A7 being right. A7 is the highest-friction one and worth a planner-side check during plan generation.

## Open Questions

1. **`eMultimedia` vs `eConsole` role for "default capture device"**
   - What we know: D-11 says "open the system default capture device (`eMultimedia` role)." The existing `WASAPIAudioCapture::enumerateDevices` at `audio_capture.cpp:261` calls `GetDefaultAudioEndpoint(eCapture, eConsole, ...)` to populate the `isDefault` flag.
   - What's unclear: Does D-11's `eMultimedia` intent require a different default-detection path than the class's existing `eConsole` default? In practice on Windows, `eMultimedia` and `eConsole` very often resolve to the same device, but they can differ when a user has explicitly set per-role defaults.
   - Recommendation: Plan task should either (a) add a `selectDefaultDevice(ERole)` method to `IAudioCapture` (small shared-lib API extension) or (b) the worker can do an inline `GetDefaultAudioEndpoint(eCapture, eMultimedia, ...)` + `selectDeviceById` since the worker already owns COM and can use `IMMDeviceEnumerator` directly via `Microsoft::WRL::ComPtr`. Option (b) keeps shared lib unchanged per P5 D-12/D-13 spirit; option (a) is cleaner but requires touching shared lib.

2. **Watchdog implementation choice (Claude's discretion in CONTEXT)**
   - What we know: D-13 says "Bounded by a 2s watchdog (matches v1.5 `VREvent_Quit` shutdown watchdog precedent — Pitfall 4 'How to avoid')." Discretion section says "Whether the 2s Cleanup watchdog uses a dedicated `std::condition_variable::wait_for` or detaches the worker as last-resort. Match v1.5 watchdog precedent."
   - What's unclear: I did not read the specific v1.5 `VREvent_Quit` watchdog code in this research session. The pattern hinted at in `.planning/research/PITFALLS.md` Pitfall 4 "How to avoid" reads "bound the cleanup time with a watchdog. If audio cleanup hasn't returned in 2 seconds, log and force-kill the worker thread (last resort)."
   - Recommendation: Plan task should grep `apps/micmap/main.cpp` and `driver/src/device_provider.cpp` for "VREvent_Quit" + "watchdog" + "Acknowledge" to find the exact v1.5 shape, then mirror it. The cleanest C++17 pattern is `condition_variable::wait_for` on a "thread done" CV signalled by the worker as its very last act before returning, with `thread_.detach()` on timeout.

3. **Exact log-line format for RMS values (Claude's discretion)**
   - What we know: Discretion grants "DriverLog line format for RMS values (`'MicMap: rms=%f dB'` vs `'MicMap audio: %d ms RMS=%f'` etc.) — pick whatever is least ambiguous for `vrserver.txt` grepping."
   - What's unclear: No format mandated.
   - Recommendation: Use `MicMap audio:` prefix to disambiguate from non-audio MicMap lines; include a sample index so out-of-order logs (rare but possible due to OS scheduling) are sortable. Suggested: `DriverLog("MicMap audio: rms[%u]=%.6f\n", index, rms);` — easy to grep, easy to confirm budget exhaustion (`rms[99]` or `rms[100]` is the last expected line).

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| OpenVR SDK + `openvr_driver.h` (`VRSettings`, `VRDriverLog`, `IServerTrackedDeviceProvider`) | All driver code (existing + P6) | ✓ | Vendored at `external/openvr` per build config; pinned by project | — |
| Windows 10/11 SDK (`<mmdeviceapi.h>`, `<audioclient.h>`, `<wrl/client.h>`, `<Objbase.h>`) | `WASAPIAudioCapture` (existing) + `audio_worker.cpp` (new) | ✓ | Visual Studio 2022 default | — |
| C++17 toolchain (MSVC `/std:c++17` per `set(CMAKE_CXX_STANDARD 17)` in `driver/CMakeLists.txt:18`) | All driver code | ✓ | MSVC 19.x (VS 2022) | — |
| `micmap::core_runtime` (P5 INTERFACE target with `IAudioCapture` factory) | `audio_worker.cpp` | ✓ | Phase 5 shipped 2026-05-02; verified in `driver/CMakeLists.txt:80` linking PRIVATE | — |
| Bigscreen Beyond + Win11 Pro hardware | UAT D-17 (1)/(2)/(3)/(4) | ✓ (assumed by CONTEXT — phase cannot be declared done without it) | N/A | None — UAT regimen is mandatory; if hardware unavailable the phase blocks. CONTEXT sets this as the canonical UAT rig. |
| Process Explorer (Sysinternals) | UAT D-17(2)/(3) handle-leak verification | Optional per D-18 ("text observation acceptable for spike") | Sysinternals current | None needed; text observation in `vrserver.txt` is acceptable. |
| SteamVR-restart-without-quit-vrserver capability | UAT D-17(3) | ✓ Available via SteamVR UI's "Restart SteamVR" option (does not quit vrserver.exe) | SteamVR current | None — this is a SteamVR-native operation. |
| `hmd_button_test.exe` for flag-OFF regression (D-17(4)) | Regression harness | ✓ Built as part of the standard build matrix | Project-internal | TEST-05 in REQUIREMENTS.md keeps this as a developer tool through P10. |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:** Process Explorer per D-18 (fallback: text observation).

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | C++ build-and-link verification (CMake + MSVC) + manual UAT on Bigscreen Beyond + Win11 Pro hardware. **No automated test framework instantiation in P6** — the spike's correctness signal is `vrserver.txt` content + Process Explorer / handle observation; neither is automatable in CI. |
| Config file | `tests/CMakeLists.txt` exists (placeholder per CLAUDE.md "Testing" section) but P6 does not add tests there. |
| Quick run command | `cmake --build build --target driver_micmap` (verifies P6 changes compile + link without breaking exports). |
| Full suite command | `cmake --build build` (full build matrix: driver + client + `mic_test.exe` + `hmd_button_test.exe`). |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| MIG-01 (partial: capture-only) | Driver hosts a dedicated audio worker thread that owns `CoInitializeEx(MTA)` + opens WASAPI capture device | manual UAT | D-17(1) on Bigscreen Beyond + Win11 Pro: launch SteamVR with `enable_driver_audio=true`; grep `vrserver.txt` for `"audio worker thread COM apartment = MTA"` + `"audio capture started inside vrserver DLL host"` + ~100 `MicMap audio: rms[*]=` lines | n/a — log artifact at `06-UAT.md` |
| SC1 | First 1 s of RMS readings logged; capture stays alive ≥ 30 s | manual UAT | D-17(1) — confirm RMS budget exhausted (last line `rms[99]` or `rms[100]`); leave SteamVR running 30 s; confirm no error lines after `"audio capture started"` | log artifact |
| SC2 | `RPC_E_CHANGED_MODE` (0x80010106) handled distinctly | manual UAT (negative test — only triggers if vrserver thread happens to be STA) + code review | grep `vrserver.txt` for `"RPC_E_CHANGED_MODE"` literal — if present, confirm it is the bail-out path; if absent (more likely), confirm code path exists by inspection of `audio_worker.cpp` | code review checklist + log artifact (negative if applicable) |
| SC3 | `CoInitializeEx` never called on calling/RunFrame thread | static check | grep: `grep -n "CoInitializeEx" driver/src/device_provider.cpp driver/src/http_server.cpp driver/src/driver_main.cpp` returns zero lines; `grep -n "CoInitializeEx" driver/src/audio_worker.cpp` returns exactly one line | automatable as CTest source-grep lint per P5 precedent |
| SC4 | Flag-OFF: byte-identical Phase 5 behavior | manual UAT + binary diff | D-17(4): set `enable_driver_audio=false`; run `hmd_button_test.exe`; confirm dashboard toggle works. Optional: `cmp build/driver/micmap/bin/win64/driver_micmap.dll <Phase-5-baseline>` — *NOT* expected to be byte-identical because Phase 6 adds new TUs; "byte-identical" means *behavioral* parity, not bit-identical binary. | log artifact + manual run |
| SC5 | `IMMNotificationClient` registered on audio worker thread + unregistered cleanly + alive-flag check | code review + manual UAT | Code review: confirm `WASAPIAudioCapture` ctor (which registers) is called from `AudioWorker::ThreadEntry`; confirm `weak_ptr<State>` + `alive` check in audio callback. UAT D-17(3): SteamVR-restart-without-quit; confirm second Init produces a fresh capture session (no `AUDCLNT_E_DEVICE_IN_USE`) and clean shutdown produces no callback-after-free crash. | code review checklist + log artifact |
| Static check (Pitfall 3 carry) | No `VRDriverInput`/`VRProperties`/`VRServerDriverHost`/`VRSettings` from `audio_worker.cpp` (D-07) | static check | `grep -n "VRDriverInput\|VRProperties\|VRServerDriverHost\|VRSettings" driver/src/audio_worker.cpp` returns zero lines | automatable as CTest source-grep lint |
| Static check (P5 D-13 carry) | No new exports from `driver_micmap.dll` | post-build | `dumpbin /exports build/driver/micmap/bin/win64/driver_micmap.dll` shows only `HmdDriverFactory` | automatable as CTest post-build assertion (matches P5 D-03) |
| Static check (P5 D-12 carry) | No `MICMAP_DRIVER_BUILD` introduced into shared lib | static check | `grep -rn "MICMAP_DRIVER_BUILD" src/audio/ src/detection/ src/core/ src/common/` returns zero | automatable as CTest source-grep lint (matches P5) |

### Sampling Rate

- **Per task commit:** `cmake --build build --target driver_micmap` + the four CTest source-grep lints (SC3, Pitfall 3, P5 D-12 carry, dumpbin export check). This is the developer's pre-commit smoke.
- **Per wave merge:** Full build matrix `cmake --build build` (driver + client + `mic_test.exe` + `hmd_button_test.exe`) + flag-OFF run of `hmd_button_test.exe` to confirm v1.5 trigger path still works.
- **Phase gate:** Full UAT regimen D-17(1) through D-17(4) on Bigscreen Beyond + Win11 Pro. UAT artifacts (vrserver.txt excerpt) committed to `.planning/phases/06-driver-side-audio-capture-spike/06-UAT.md` per D-18. `/gsd-verify-work` confirms all five SC + MIG-01 partial.

### Wave 0 Gaps

- [ ] No new test files needed in `tests/` — P6 has no automatable behavior tests beyond static checks. Existing test placeholder remains.
- [ ] CTest source-grep lints to add (extending P5's `cmake/AssertNoOpenVRInCore.cmake` precedent):
  - `cmake/AssertAudioWorkerNoVrApi.cmake` — assert `audio_worker.cpp` has no `VRDriverInput`/`VRProperties`/`VRServerDriverHost`/`VRSettings`.
  - Reuse P5's `dumpbin /exports` assertion (already present per P5 D-03).
- [ ] No new framework install required.
- [ ] UAT artifact template `06-UAT.md` to be created during the UAT task — content shape: header (date, rig, SteamVR version, Win build), section per D-17(1)/(2)/(3)/(4), each with `vrserver.txt` excerpt + observed behavior.

## Sources

### Primary (HIGH confidence — in-tree, verified 2026-05-02)
- `.planning/phases/06-driver-side-audio-capture-spike/06-CONTEXT.md` — locked decisions D-01 through D-22 (binding constraint).
- `.planning/REQUIREMENTS.md` — MIG-01 wording + traceability table mapping to Phase 6.
- `.planning/ROADMAP.md` — Phase 6 SC1-SC5 + research flag NEEDS VALIDATION + dependency on Phase 5.
- `.planning/research/PITFALLS.md` Pitfalls 1, 3, 4, 11, 13, 14 — verbatim mitigations adopted by CONTEXT decisions.
- `.planning/research/SUMMARY.md` §"Phase 2: Driver-Side Audio Capture Spike" (research-numbered) — research-derived rationale for spike-first ordering.
- `.planning/research/ARCHITECTURE.md` — driver-resident thread model + COM apartment caveat (HIGH confidence note about WASAPI in DLL host).
- `.planning/codebase/STRUCTURE.md` — directory conventions (`driver/src/` for driver-only TUs).
- `.planning/codebase/STACK.md` — locked stack confirms no new deps in P6.
- `.planning/codebase/CONCERNS.md` (referenced via CONTEXT) — flagged manual `InterlockedIncrement`/`Decrement` on `DeviceNotificationClient` deferred to P7.
- `.planning/phases/05-shared-library-extraction/05-CONTEXT.md` — D-04, D-10, D-11, D-12, D-13, D-15, D-16 carryforward into P6.
- `src/audio/src/audio_capture.cpp:116-179` (DeviceNotificationClient), `:186-235` (ctor + dtor), `:441-470` (stopCapture), `:519-532` (captureLoop's own `CoInitializeEx`).
- `src/audio/include/micmap/audio/audio_capture.hpp` — `IAudioCapture` interface + `createWASAPICapture` factory.
- `driver/src/device_provider.{hpp,cpp}` — full file; identifies splice points for Init/Cleanup modifications.
- `driver/src/command_queue.hpp` — read-only context confirming P6 introduces no new producer.
- `driver/src/driver_log.hpp` — `SafeDriverLog` null-safe wrapper; the macro the audio worker uses.
- `driver/resources/settings/default.vrsettings` — existing `driver_micmap` section; D-01 splice point.
- `driver/CMakeLists.txt` — driver target definition, P5 link to `micmap::core_runtime` PRIVATE at line 80.

### Primary (HIGH confidence — official documentation)
- Microsoft Learn — `CoInitializeEx` return values: documents `S_OK`, `S_FALSE`, `RPC_E_CHANGED_MODE`. [CITED]
- Microsoft Learn — `IMMNotificationClient` and `IMMDeviceEnumerator::RegisterEndpointNotificationCallback`: documents that callbacks run on a system-managed thread; unregister does not synchronously wait for in-flight callbacks. [CITED]
- OpenVR Driver API documentation — `IServerTrackedDeviceProvider::Init`/`Cleanup`/`RunFrame` lifecycle; `vr::VRSettings()` / `vr::VRDriverLog()` driver-side service interfaces. [CITED via SUMMARY.md and ARCHITECTURE.md prior research; not re-verified this session.]

### Secondary (MEDIUM confidence)
- `D:\Documents\Projects\bey-closer-t1\HMD Button Stub.md` — sister-project existence proof for "WASAPI audio thread inside SteamVR driver DLL host" pattern. CONTEXT explicitly says "read for architecture confirmation, NOT for code copy." Not re-read this session — relying on CONTEXT's assertion.

### Tertiary (LOW confidence — flagged for validation)
- A1: `vr::VRSettings()->GetBool` thread-safety beyond the `Init` thread is undocumented in OpenVR. Conservative reading-once at Init sidesteps the question.
- A3: WASAPI shared-mode default period varies by device. The 10 ms / 100-line budget is a reasonable approximation; D-08 grants discretion to adjust.
- A7: `IAudioCapture` API may or may not expose a one-call "open default" helper; planner should confirm during plan generation (Open Question #1).

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — every dependency is in-tree and verified at exact line numbers.
- Architecture (CONTEXT decisions): HIGH — CONTEXT.md is the canonical contract; this research validates the decisions are individually implementable against the existing code.
- Pitfalls (mitigation correctness): HIGH for the contracts (Pitfalls 1, 3, 4, 13, 14 each have explicit prescriptions in `.planning/research/PITFALLS.md` matched to specific D-numbers); MEDIUM for the actual P6 spike outcome (the entire purpose of the spike is to verify Pitfall 1 mitigation works inside vrserver — sister-project validates it once externally, not yet here).
- Validation architecture: HIGH — every SC has a manual or static-check observation path; no SC requires a test framework P6 doesn't already have.

**Research date:** 2026-05-02

**Valid until:** 30 days from research date for stack and CONTEXT-grounded recommendations (stable). The actual spike outcome (does WASAPI work inside vrserver DLL host on Bigscreen Beyond + Win11 Pro?) is single-shot and not subject to research staleness — it's a UAT result, not a research artifact.
