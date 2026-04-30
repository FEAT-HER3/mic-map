# Technology Stack — v1.6 Feature Migration

**Project:** MicMap
**Milestone:** v1.6 Feature Migration (relocate audio/detection/state-machine/config/trigger from `micmap.exe` into `driver_micmap.dll`)
**Researched:** 2026-04-30
**Overall confidence:** HIGH

This document covers ONLY stack additions/changes the migration needs. The locked v1.5 stack (C++17, CMake, ImGui+D3D11, WASAPI, KissFFT, nlohmann/json, cpp-httplib, OpenVR SDK, Inno Setup) stays as-is. Where v1.6 introduces a NEW capability or changes a target's library set, it is called out below.

---

## TL;DR — Picks

| Question | Pick | Add to repo? |
|---|---|---|
| Q1. WASAPI from inside the driver DLL | Same WASAPI stack already in `src/audio/` — vrserver is a normal user-process, no session-0 issue, no API change | No new dep |
| Q2. Shared `libmicmap_core` build pattern | Add an INTERFACE facade `micmap::core_runtime` over the existing STATIC libs (`micmap_audio` + `micmap_detection` + `micmap_core` + `micmap_common`); enforce no-OpenVR-symbol rule via a CMake dependency-graph guard | No new dep |
| Q3. IPC reshape | Keep cpp-httplib for client→driver settings/training push and client←driver health pull. Trigger HTTP path is deleted (becomes in-process callback). | No new dep |
| Q4. Driver-side file logger | Tiny in-house `FileLogSink` (~80 LoC) writing to `%APPDATA%\MicMap\micmap-driver.log` with size-cap rotation; bridges to existing `common::Logger` interface and to OpenVR `VRDriverLog()` simultaneously. | No new dep (reject spdlog) |
| Q5. Health/status surface | Same HTTP server, new `GET /health` endpoint returning JSON; client polls every 1–2 s with short timeout; HTTP connection-refused IS the "driver down" signal — no separate liveness scheme needed | No new dep |

---

## Question 1 — WASAPI inside the driver DLL

### Answer

**Use the existing `src/audio/` WASAPI implementation unchanged.** It already calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` on the capture thread (`audio_capture.cpp:196, :521`, `device_enumerator.cpp:32`), which is the correct pattern for any in-process consumer of WASAPI — including a DLL hosted by `vrserver.exe`.

### Why this is safe

`vrserver.exe` is **not a Windows service and does not run in session 0.** It launches as a regular user-process from the Steam client when SteamVR starts, signed by Valve, residing under `C:\Program Files (x86)\Steam\vr\runtime\bin\` ([file.net analysis](https://www.file.net/process/vrserver.exe.html), [SpyShelter](https://www.spyshelter.com/exe/valve-corp-vrserver-exe/)). A driver DLL loaded into vrserver inherits the user's session, user token, and audio endpoint permissions. WASAPI shared-mode capture works identically to how it does inside `micmap.exe` today.

The session-0 / loopback-from-service caveats from MS docs ([Loopback Recording](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording), [WASAPI overview](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi)) **do not apply here**. We are doing input capture (not loopback) from a user-context process.

### Concrete integration rules

1. **Do NOT call `CoInitializeEx` on the OpenVR `RunFrame` thread.** RunFrame is the vrserver main loop — OpenVR docs explicitly forbid blocking work there ([Driver_API_Documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md)). Spin a dedicated audio capture thread inside `IServerTrackedDeviceProvider::Init()`, mirroring how `micmap.exe` does it today. Tear down in `Cleanup()`.
2. **COM apartment:** `COINIT_MULTITHREADED` on the capture thread (already done). No change.
3. **MMCSS thread priority:** Optional — `AvSetMmThreadCharacteristics(L"Pro Audio", ...)` from `avrt.lib` if we observe glitches. Not required for v1.6 baseline; we have plenty of slack at our buffer sizes. Leave as a Phase-N follow-up if jitter shows up.
4. **Trigger pipeline becomes in-process:** the audio thread's state-machine callback enqueues a `TapCommand` directly onto the existing `CommandQueue` that `RunFrame` already drains. The HTTP `POST /button` path goes away — the driver only reads its own state machine.
5. **Lifecycle alignment:** open WASAPI in `Init()`, close in `Cleanup()`. SteamVR can call `Cleanup()` and re-`Init()` on HMD reactivation cycles — existing `IAudioCapture` is already idempotent on stop/start (reused by client UI today when device picker changes).

### What to avoid

- **Don't add PortAudio, RtAudio, miniaudio.** WASAPI is already wired and works. Wrappers add a dependency, not a capability.
- **Don't add WASAPI loopback** (we're doing capture, not system-mix recording). Loopback is the path with audiodg.exe APO deadlock pitfalls ([MS docs](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)).
- **Don't initialize COM in `DllMain`.** OS rules forbid it, and OpenVR doesn't need it — do it on the capture thread we own.

### Sources

- [vrserver.exe runs as user process](https://www.file.net/process/vrserver.exe.html) — HIGH (multiple corroborating sources)
- [OpenVR Driver_API_Documentation: RunFrame is non-blocking](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md) — HIGH (official)
- [MS WASAPI overview](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi) — HIGH (official)
- Existing `src/audio/src/audio_capture.cpp` already correct — HIGH (verified in repo)

**Confidence: HIGH.** Same API, same calls, same threading discipline. No new code paths.

---

## Question 2 — Shared `libmicmap_core` CMake structure

### Answer

The codebase **already has** the right shape: `micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common`, `micmap_bindings` are already STATIC libraries with no OpenVR/SteamVR symbols. The v1.6 work is to:

1. Add an INTERFACE facade target that aggregates them.
2. Wire that facade into `driver_micmap` (DLL), `micmap` (EXE), and `mic_test` (EXE).
3. **Enforce** the no-SteamVR-symbol rule with a CMake-level guard.

### Pick: keep STATIC libs, add an INTERFACE facade

```cmake
# src/CMakeLists.txt — add alongside existing micmap_lib (which mixes in
# micmap_steamvr and is fine for the EXE). The new facade is the driver-safe
# subset.

add_library(micmap_core_runtime INTERFACE)
target_link_libraries(micmap_core_runtime INTERFACE
    micmap_common
    micmap_audio
    micmap_detection
    micmap_core      # state_machine + config_manager
    # Deliberately NOT: micmap_steamvr (depends on OpenVR client API)
)
add_library(micmap::core_runtime ALIAS micmap_core_runtime)
```

Each consumer just does `target_link_libraries(<consumer> PRIVATE micmap::core_runtime)`:

| Consumer | Currently links | After v1.6 |
|---|---|---|
| `driver_micmap` (DLL) | `OpenVR::openvr_api`, `httplib::httplib`, `nlohmann_json`, `micmap::bindings`, `ws2_32` | + `micmap::core_runtime` |
| `micmap` (EXE) | `micmap::lib` (everything), `imgui`, `d3d11`... | `micmap::core_runtime` + `micmap::steamvr` (driver client only, no detection) + `imgui` |
| `mic_test` (EXE) | `micmap::audio`, `micmap::detection`, `micmap::core` | `micmap::core_runtime` |

### STATIC vs OBJECT — pick STATIC

OBJECT libraries compile once and embed objects directly into each consumer ([CMake add_library docs](https://cmake.org/cmake/help/latest/command/add_library.html)). STATIC libraries also compile once; the linker pulls only what each consumer references. **For three consumers (DLL + 2 EXEs) of a non-trivial codebase, STATIC + LTO gives the best build-cache behavior** — OBJECT libs lose archive-level dead-code elimination on MSVC unless you also pass `/OPT:REF /OPT:ICF`.

The repo is **already STATIC across the board**. No change needed; just don't accidentally convert anything to OBJECT during the rewire. ([CMake Discourse on INTERFACE vs OBJECT](https://discourse.cmake.org/t/how-can-i-combine-interface-libraries-with-shared-libraries/3027))

### Symbol-visibility guard (the "no SteamVR symbol leak" rule)

Two enforcement layers:

1. **Dependency-graph rule.** `micmap_audio`, `micmap_detection`, `micmap_core`, `micmap_common` MUST NOT `target_link_libraries(... OpenVR::openvr_api)` and MUST NOT include `<openvr.h>` or `<openvr_driver.h>`. This is true today — preserve it. Add a CI grep:
   ```cmake
   # cmake/AssertNoOpenVRInCore.cmake
   foreach(lib micmap_audio micmap_detection micmap_core micmap_common)
       get_target_property(_links ${lib} LINK_LIBRARIES)
       if(_links MATCHES "openvr|OpenVR")
           message(FATAL_ERROR "${lib} must not link OpenVR (core_runtime invariant)")
       endif()
   endforeach()
   ```
2. **MSVC-specific:** `driver_micmap.dll` builds with `/WX /W4` already. Add `target_compile_definitions(micmap_core_runtime INTERFACE MICMAP_CORE_RUNTIME=1)` and put a `static_assert` in one core header that fires if `<openvr_driver.h>` was included alongside it — catches accidental driver-only header inclusion in shared sources at compile time.

### What stays where

| Module | Lives in | DLL? | EXE? | Why |
|---|---|---|---|---|
| WASAPI capture | `src/audio` | YES | YES | Migrating from EXE-only to both |
| FFT detection | `src/detection` | YES | YES | Migrating from EXE-only to both |
| State machine | `src/core/state_machine` | YES | YES | Migrating from EXE-only to both |
| Config (read+write) | `src/core/config_manager` | YES (read) | YES (read+write) | Driver reads at boot; client owns writes |
| Bindings patcher | `src/bindings` | YES | YES | Already shared in v1.5 |
| ImGui + D3D11 UI | `apps/micmap` | NO | YES | UI never enters driver |
| Tray icon, Win32 windowing | `apps/micmap` | NO | YES | UI never enters driver |
| OpenVR client API (`vr::IVRApplications`, etc.) | `src/steamvr` | NO | YES | Client uses for manifest registration; driver uses driver-side API only |
| OpenVR driver API (`vr::IServerTrackedDeviceProvider`, `IVRDriverInput`) | `driver/src` | YES | NO | Never crosses into shared layer |

### What to avoid

- **Don't make `micmap_core_runtime` a SHARED lib.** Adds a third DLL to ship, gains nothing. The DLL boundary already exists at `driver_micmap.dll` and `micmap.exe`.
- **Don't pull `micmap_steamvr` into the shared facade.** It links the OpenVR client API; that imports `openvr_api.dll` which is fine for the EXE but unwanted symbol surface in the driver DLL.
- **Don't add a package manager (vcpkg, conan).** FetchContent works; we already have lock-step versions pinned in `external/CMakeLists.txt`.

### Sources

- [CMake add_library — STATIC/SHARED/OBJECT/INTERFACE semantics](https://cmake.org/cmake/help/latest/command/add_library.html) — HIGH (official)
- [target_link_libraries propagation](https://cmake.org/cmake/help/latest/command/target_link_libraries.html) — HIGH (official)
- [Benefits of CMake Object Libraries](https://www.scivision.dev/cmake-object-libraries/) — MEDIUM (corroborates the STATIC choice for our 3-consumer count)
- Existing `src/CMakeLists.txt`, `src/audio/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/detection/CMakeLists.txt`, `driver/CMakeLists.txt` — HIGH (verified in repo)

**Confidence: HIGH.** The structure already exists; v1.6 is a facade + rewire + guard, not a new build system.

---

## Question 3 — IPC reshape (settings + training push, health pull)

### Answer

**Keep cpp-httplib. Same library, same port, new endpoint set. Trigger path goes away.**

### Why not switch

Three concrete options were on the table:

| Option | Verdict |
|---|---|
| **Localhost HTTP (status quo)** | **PICK.** Already wired, already tested under UAT, already has a binding-port retry path. The migration removes one endpoint (`POST /button`); the other endpoints are cheap to add. |
| Named pipes (`CreateNamedPipeW` on `\\.\pipe\micmap`) | Lower latency (kernel-mode `npfs.sys`, no TCP), but we're not latency-bound — nothing in the new IPC contract is in the audio hot path. Net cost: rewrite client + driver endpoints, maintain a custom message framer. ([Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/ipc/pipes), [csandker.io named pipes deep-dive](https://csandker.io/2021/01/10/Offensive-Windows-IPC-1-NamedPipes.html)) |
| Memory-mapped file (`CreateFileMappingW`) | Faster again, but we don't share large datasets. Training samples are at most ~150 frames × FFT-size floats ≈ under 1 MB. HTTP body is fine. ([sashadu MMF IPC](https://sashadu.wordpress.com/2015/03/19/ipc-using-memory-mapped-files/)) |

The trigger path was the latency-sensitive one and **it's being deleted entirely** — trigger becomes a direct callback into the same process. Settings push happens on save (user action, ~hundreds of ms is fine). Training-sample push happens on training-complete (one-shot, body up to ~MB). Health pull is a 1–2 Hz poll. None of these benefit from sub-millisecond IPC.

### New endpoint contract (driver = server)

| Endpoint | Direction | Body | Purpose |
|---|---|---|---|
| `GET /health` | client → driver | — | Returns `{"state":"detecting|training|idle","driver_version":"0.2.0","uptime_ms":N,"last_trigger_ms_ago":N,"audio_device":"..."}` |
| `POST /settings` | client → driver | full `AppConfig` JSON | Driver applies new sensitivity / detection-time / device-id |
| `POST /training/sample` | client → driver | `{"samples":[...]}` | Push collected samples to driver detector |
| `POST /training/finalize` | client → driver | — | Driver finalizes profile, writes `training_data.bin`, returns thresholds |
| `POST /training/abort` | client → driver | — | Cancel in-progress training |
| ~~`POST /button`~~ | — | — | **DELETED.** Trigger no longer crosses wire. |

### Health-check semantics

- **Driver-down detection:** client `GET /health` with a 250 ms connect timeout. ECONNREFUSED → driver not up. No separate heartbeat needed; HTTP's own connection refusal IS the liveness signal.
- **Stale state detection:** `uptime_ms` reset implies driver restart (HMD re-activation cycle). Client treats reset > 5 s ago as "ok, propagate fresh settings".

### Concurrency model inside driver (already established v1.5 pattern)

HTTP handler thread → pushes a typed command onto `CommandQueue` → drained in `RunFrame`. Same pattern works for `/settings` and `/training/*`. **No OpenVR API call ever happens off the RunFrame thread** — this is the load-bearing v1.5 invariant we preserve.

For pure-data endpoints (settings push, training sample push) that don't touch OpenVR, the HTTP handler thread can write directly into a `std::mutex`-protected detector/config struct — no need to bounce through CommandQueue. CommandQueue is only for OpenVR-touching commands (the trigger, which is going away anyway).

### Localhost binding + security

Already correct: `httplib::Server` binds to `127.0.0.1` only. Port discovery via fallback range (already implemented in `http_server.cpp` per Phase 1). For v1.6:

- Driver writes its bound port to `%APPDATA%\MicMap\driver_port` (atomic `ReplaceFileW` — reuse existing helper from `config_manager.cpp` v1.5 work).
- Client reads that file on startup; falls back to scanning the configured range if absent.

### cpp-httplib version

Currently pinned at `v0.14.3`. Latest as of 2026-04 is `v0.20.x` ([yhirose/cpp-httplib releases](https://github.com/yhirose/cpp-httplib/releases)). **Recommend bump to v0.20.1** — covers CVE-2025-46728 and unblocks `Server::set_keep_alive_max_count` tuning if we add it later. Defer the bump to a dedicated "deps refresh" plan inside Phase 1; it's low-risk but not zero-risk on the wire format we already validated.

### What to avoid

- **Don't add gRPC, Cap'n Proto, MessagePack-RPC, ZeroMQ.** Single machine, one client, one server. The framing is already JSON-over-HTTP and works.
- **Don't add WebSockets.** Pull-poll is fine at 1–2 Hz for health; push subscriptions are a complexity we don't need.
- **Don't introduce a service mesh, Mailslot, Window-message IPC.** All worse fits.
- **Don't add OpenSSL.** Already disabled in the v1.5 driver build (`CPPHTTPLIB_NO_EXCEPTIONS` only). Localhost-only binding is the security boundary.

### Sources

- [cpp-httplib releases — current 0.20.x](https://github.com/yhirose/cpp-httplib/releases) — HIGH (official)
- [CVE-2025-46728 / 0.20.1 fix](https://vcpkg.io/en/package/cpp-httplib) — MEDIUM (vcpkg notes)
- [Microsoft Learn — named pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/pipes) — HIGH (official, alternative-not-chosen rationale)
- [csandker.io named pipe internals](https://csandker.io/2021/01/10/Offensive-Windows-IPC-1-NamedPipes.html) — MEDIUM (corroborates: kernel-mode `npfs.sys`, faster than localhost TCP, but unnecessary at our message rate)
- Existing `driver/src/http_server.cpp`, `src/steamvr/src/driver_client.cpp` — HIGH (verified in repo)

**Confidence: HIGH.** Status quo wins on cost-vs-benefit; the only reason to switch IPC was trigger latency, and we're removing that path.

---

## Question 4 — Driver-side logging

### Answer

**Write a tiny in-house `FileLogSink` (~80 LoC). Reject spdlog.**

### Why not spdlog

spdlog is excellent but oversized for our actual needs:

- spdlog 1.15.x ships ~30 KLOC of code + bundled fmt 11.x ([spdlog releases](https://github.com/gabime/spdlog/releases)).
- Driver DLLs loaded into vrserver should keep their import surface and binary size small. Adding spdlog grows `driver_micmap.dll` materially and adds a header-only dep that compiles slowly.
- spdlog's real wins (async logging, multi-sink, formatter library, log levels with compile-time stripping) are features we don't use today and aren't in the v1.6 acceptance criteria.
- Multi-DLL spdlog usage requires registry sharing dance ([How-to-use-spdlog-in-DLLs](https://github.com/gabime/spdlog/wiki/How-to-use-spdlog-in-DLLs)) — one more thing to get wrong.

### Pick: extend `common::Logger` with a file sink

Existing `src/common/include/micmap/common/logger.hpp` already has the right interface. Add three things:

1. **`FileLogSink` implementation.** Opens `%APPDATA%\MicMap\micmap-driver.log` (or `micmap-client.log` from the client) for append; size-cap at e.g. 5 MB → rename to `micmap-driver.log.1`, drop `.1` → `.2`, etc. Atomic via `ReplaceFileW` (same pattern as `config_manager` rotation in v1.5). UTF-8 only; wstring-to-UTF-8 at the boundary like the config code does.
2. **`OpenVRDriverLogSink` adapter.** Calls `vr::VRDriverLog()->Log(...)` so messages also land in `vrserver.txt`. This is the existing v1.5 path; preserve it.
3. **`Logger::addSink()`.** Composite sink — writes to all attached sinks.

Driver wires both sinks at `Init()`. Client wires file sink + console (when `/SUBSYSTEM:CONSOLE` test build) or file sink only (when `/SUBSYSTEM:WINDOWS` release build).

### File location

- Driver writes: `%APPDATA%\MicMap\micmap-driver.log`
- Client writes: `%APPDATA%\MicMap\micmap-client.log`

Two separate files because two processes write concurrently — cross-process file locking is a tar pit we shouldn't enter. The `%APPDATA%` resolution path is already in `config_manager.cpp` for the same directory; reuse the helper.

### Driver-process specifics

- **No console.** Driver DLL has no stdout. `printf`/`std::cout` go nowhere. `OutputDebugStringA` works but only useful with a debugger attached.
- **No stdout-only sink in the driver.** The current logger is stdout-only under `/SUBSYSTEM:WINDOWS` (per UAT C3 follow-up notes) and that's the bug v1.6 fixes for the driver path.
- **CrashHandler interaction:** driver should call `Logger::flushAll()` from `Cleanup()` and from a `SetUnhandledExceptionFilter` callback if we add one. Out of v1.6 scope.

### What to avoid

- **Don't add spdlog, glog, Boost.Log, plog.** Net negative.
- **Don't add fmt directly.** `snprintf` + a `std::format`-shaped helper (we're C++17, no `std::format`) is fine. Or just printf-style as today.
- **Don't use Windows Event Log (`ReportEvent`).** Wrong UX surface for end-user troubleshooting; manifest registration adds installer complexity.
- **Don't use ETW.** Heavyweight tooling for end-users to read.
- **Don't share one log file between driver and client.** File-locking interleave problem; just use two files.

### Sources

- [spdlog releases (1.15.3 current)](https://github.com/gabime/spdlog/releases) — HIGH (official, confirms size/feature scope)
- [spdlog DLL usage caveats](https://github.com/gabime/spdlog/wiki/How-to-use-spdlog-in-DLLs) — HIGH (official wiki, justifies rejection)
- [OpenVR DriverLog wrapper in samples](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md) — HIGH (official)
- Existing `common::Logger` and `driver/src/driver_log.hpp` — HIGH (verified in repo)

**Confidence: HIGH.** Build-vs-buy clearly favors build at this scope.

---

## Question 5 — Health/status surface (driver lifecycle survives client lifecycle)

### Answer

**`GET /health` on the same cpp-httplib server, polled by the client every 1–2 s with a 250 ms timeout. ECONNREFUSED is the "driver down" signal. No separate liveness mechanism.**

This collapses Q3 and Q5 onto the same HTTP server — one connection model, one set of failure modes, one IPC library to maintain.

### Why poll, not subscribe

| Option | Trade-off |
|---|---|
| **Client polls `GET /health` (PICK)** | Stateless on driver side. Client always gets fresh state. Reconnection is trivial: keep polling. Driver up/down independence is built-in: failed connect = down. |
| Long-poll / WebSocket / SSE | Adds session state on driver side. Requires a different cpp-httplib mode. Reconnection logic lives in client AND driver. Net cost > benefit for 1–2 Hz update rate. |
| Named-pipe duplex | Same drawback as Q3 named pipes — new framing protocol, no upside. |
| `CreateMutexW` + shared memory | Lockless ring with a generation counter. Faster, but client polls anyway and the driver state object is small. Adds a new IPC channel for one feature. |

### State surface

```jsonc
GET /health 200 OK
{
  "ok": true,
  "driver_version": "0.2.0",
  "uptime_ms": 1234567,
  "state": "detecting",          // idle | training | detecting | triggered | cooldown
  "audio_device_id": "{0.0.1.00000000}.{...}",
  "audio_device_friendly": "Microphone (Beyond)",
  "audio_capturing": true,
  "training_progress": null,      // {samples_collected:N, samples_needed:M} when training
  "last_trigger_ms_ago": 4200,
  "config_loaded_from": "%APPDATA%\\MicMap\\config.json",
  "log_path": "%APPDATA%\\MicMap\\micmap-driver.log"
}
```

Client UI surfaces:
- "Driver: Connected" (green) when poll succeeds, "Driver: Not running" (red) when ECONNREFUSED
- Live state badge (Idle / Detecting / etc.)
- Last-trigger-ago counter
- Audio-device-active LED

### Lifecycle independence

The contract handles all four cross-product cases:

| Driver | Client | Behavior |
|---|---|---|
| Up | Up | Polls succeed; full UI live |
| Up | Down | Driver runs end-to-end (the v1.6 win); no observer |
| Down | Up | Polls fail with ECONNREFUSED; client shows "Not running"; reconnects when driver returns |
| Down | Down | — |

There is no "driver crashes mid-session" recovery requirement beyond "client UI reflects reality on next poll." SteamVR will restart the driver on its own lifecycle (HMD reactivation cycle).

### Polling cost

- 1 Hz poll × ~500-byte response × localhost = imperceptible (sub-1 KB/s, sub-microsecond CPU).
- Don't poll while client window is minimized + tray-only; freeze polls then. Cuts background cost to zero.

### What to avoid

- **Don't add a Windows Service Control Manager-style heartbeat.** Driver isn't a service.
- **Don't add an OpenVR overlay-based status surface.** Massive overkill, and OpenVR overlays are explicitly out of v1.6 scope.
- **Don't use registry keys, files, or `WaitForSingleObject` on a named event for liveness.** All work, all worse fits than "is the HTTP socket open?"
- **Don't add Prometheus/StatsD/OpenTelemetry.** Single-user desktop app.

### Sources

- [cpp-httplib server docs](https://github.com/yhirose/cpp-httplib) — HIGH (official)
- Existing `driver/src/http_server.cpp` — HIGH (verified pattern in repo)

**Confidence: HIGH.** Smallest possible delta on top of v1.5 IPC; no new technology.

---

## What NOT to add

| Category | Don't add | Reason |
|---|---|---|
| Audio | PortAudio, RtAudio, miniaudio | WASAPI already wired; wrappers add deps not capabilities |
| Audio | WASAPI loopback mode | We capture from a microphone, not the system mix; loopback has audiodg deadlock pitfalls |
| FFT | FFTW, Intel MKL, KFR | KissFFT is fast enough for our 2048-point FFT @ ~50 Hz; adds license / size cost |
| IPC | gRPC, Cap'n Proto, MsgPack-RPC, ZeroMQ, ROS | One process pair, one machine, JSON-over-HTTP works |
| IPC | Named pipes, memory-mapped files | Faster than HTTP but we're not latency-bound (trigger goes in-process) |
| IPC | WebSockets, SSE, long-polling | Pull-poll handles a 1–2 Hz health check trivially |
| IPC | Window-message IPC, Mailslots, DCOM | All inferior fits |
| Logging | spdlog, glog, plog, Boost.Log | ~30 KLOC + bundled fmt for features we don't use; multi-DLL registry dance |
| Logging | fmt as a standalone dep | C++17 + snprintf covers our format needs |
| Logging | Windows Event Log, ETW | Wrong UX surface for end-users |
| Build | vcpkg, Conan, Hunter | FetchContent works, versions are pinned |
| Build | Convert `micmap_audio` etc. to OBJECT libs | STATIC + LTO is better for our 3-consumer count |
| Build | Convert shared layer to SHARED (DLL) | Adds a third DLL to ship, no upside |
| OpenVR | Overlay-based status UI | Explicitly out of v1.6 scope (deferred to UX-02) |
| Telemetry | Prometheus, StatsD, OpenTelemetry, Sentry | Single-user desktop app |
| Crash | breakpad, crashpad, SetUnhandledExceptionFilter framework | Not in v1.6 acceptance criteria; driver-side `__try`/`__except` around RunFrame is enough if we hit a crash bug |
| Threading | Intel TBB, OpenMP, oneTBB, libdispatch | Two threads (audio capture + RunFrame) is straightforward `std::thread` |
| Wait primitives | Boost.Asio, libuv | cpp-httplib has its own loop; we don't need a general-purpose async runtime |

---

## Summary table — all stack additions for v1.6

| Item | Action | Version | Confidence |
|---|---|---|---|
| WASAPI capture in driver | Reuse `src/audio/` unchanged | — | HIGH |
| `micmap::core_runtime` INTERFACE facade | Add to `src/CMakeLists.txt` | — | HIGH |
| Symbol-leak guard | New `cmake/AssertNoOpenVRInCore.cmake` | — | HIGH |
| `FileLogSink` | New ~80 LoC in `src/common/src/file_log_sink.cpp` | — | HIGH |
| `OpenVRDriverLogSink` adapter | New ~30 LoC in `driver/src/openvr_driver_log_sink.cpp` | — | HIGH |
| `GET /health` endpoint | Extend `driver/src/http_server.cpp` | — | HIGH |
| `POST /settings`, `POST /training/*` | Extend `driver/src/http_server.cpp`; remove `POST /button` | — | HIGH |
| `driver_port` discovery file | Atomic write in `driver/src/http_server.cpp`, read in `src/steamvr/src/driver_client.cpp` | — | HIGH |
| cpp-httplib version bump | `v0.14.3` → `v0.20.1` (deferred to dedicated plan inside Phase 1) | 0.20.1 | MEDIUM — defer if it surfaces wire-format regressions |
| spdlog | **DO NOT ADD** | — | HIGH |
| Named pipes / shared memory | **DO NOT ADD** | — | HIGH |

**No new third-party libraries are required for v1.6.** The migration is structural: existing static libs get a new facade, the driver consumes them, the client→driver IPC shrinks (trigger gone) and grows (settings + health), and a tiny file-sink fills the "driver has no stdout" gap.

---

## Sources (consolidated)

- [vrserver.exe runs as user-process — file.net](https://www.file.net/process/vrserver.exe.html)
- [vrserver.exe Valve component — SpyShelter](https://www.spyshelter.com/exe/valve-corp-vrserver-exe/)
- [About WASAPI — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
- [Loopback Recording — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
- [OpenVR Driver_API_Documentation](https://github.com/ValveSoftware/openvr/blob/master/docs/Driver_API_Documentation.md)
- [SimpleHMD driver sample](https://github.com/ValveSoftware/openvr/blob/master/samples/drivers/drivers/simplehmd/src/hmd_device_driver.cpp)
- [CMake add_library reference](https://cmake.org/cmake/help/latest/command/add_library.html)
- [CMake target_link_libraries reference](https://cmake.org/cmake/help/latest/command/target_link_libraries.html)
- [Benefits of CMake Object Libraries — scivision](https://www.scivision.dev/cmake-object-libraries/)
- [INTERFACE vs SHARED libraries discussion — CMake Discourse](https://discourse.cmake.org/t/how-can-i-combine-interface-libraries-with-shared-libraries/3027)
- [cpp-httplib releases](https://github.com/yhirose/cpp-httplib/releases)
- [cpp-httplib repo — yhirose](https://github.com/yhirose/cpp-httplib)
- [spdlog releases](https://github.com/gabime/spdlog/releases)
- [spdlog DLL usage caveats](https://github.com/gabime/spdlog/wiki/How-to-use-spdlog-in-DLLs)
- [Windows IPC named pipes — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/ipc/pipes)
- [Offensive Windows IPC: named pipes — csandker](https://csandker.io/2021/01/10/Offensive-Windows-IPC-1-NamedPipes.html)
- [IPC using memory-mapped files — sashadu](https://sashadu.wordpress.com/2015/03/19/ipc-using-memory-mapped-files/)

---

*STACK research complete — 2026-04-30. No new third-party deps required for v1.6. All architectural deltas live inside `src/CMakeLists.txt`, `driver/CMakeLists.txt`, `driver/src/http_server.cpp`, and `src/common/`.*
