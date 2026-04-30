# Pitfalls Research — v1.6 Feature Migration

**Domain:** Relocating audio capture + FFT detection + state machine + config + trigger pipeline from a Windows desktop EXE (`micmap.exe`) into a SteamVR/OpenVR driver DLL (`driver_micmap.dll`); extracting a shared static library (`libmicmap_core`) consumed by driver, client, and `mic_test.exe`.
**Researched:** 2026-04-30
**Confidence:** HIGH — anchored in (a) v1.5 shipped pitfalls catalog (lessons paid for in commits + UAT), (b) sister-project `bey-closer-t1` HMD Button Stub.md, (c) actual v1.5 driver source (`driver/src/device_provider.cpp`, `command_queue.hpp`, `http_server.cpp`) and codebase concerns (`.planning/codebase/CONCERNS.md`).

## Orientation

v1.5 made the driver real but kept it dumb: an HTTP receiver that drains a `CommandQueue` from RunFrame and toggles `/input/system/click`. v1.6 makes the driver smart — it now hosts WASAPI capture, FFT, state machine, config, and the trigger pipeline. The HTTP boundary survives but flows the other way: client pushes settings + training samples; client pulls health.

This shifts the failure surface in non-obvious ways:

- **vrserver.exe is now an audio app.** WASAPI + COM + IMMDevice live inside Valve's process, on Valve's threads, with Valve's lifecycle.
- **The CommandQueue boundary that v1.5 invented for HTTP→RunFrame must now also catch audio-thread→RunFrame.** Audio callbacks are a much higher-frequency producer than HTTP.
- **The shared-lib carries platform-Windows code (WASAPI) into a DLL.** Symbol visibility, COM apartments, ODR — all must survive.
- **Two processes now own the same JSON.** v1.5 had only the client writing config. v1.6 has the client writing and the driver reading on a notification.
- **`mic_test.exe` is the "is this representative?" canary.** It exercises the shared lib without OpenVR — easy to drift away from the driver's reality.

The eleven v1.5 critical pitfalls (HMD handle invalidation, vrpathreg double-register, manifest paths, `VR_Init` reentry, `ReplaceFileW` atomicity, `IsApplicationInstalled` poll guard, etc.) all still apply. Several of them recur with new symptoms in v1.6 — those are explicitly cited under each pitfall below where relevant.

`bey-closer-t1/HMD Button Stub.md` documents the cross-driver input-component permission shape (`VRInputError_WrongType` on cross-driver Update, `VRInputError_InvalidParam` on early Create). v1.5 already absorbed those lessons; they remain unchanged in v1.6.

---

## Critical Pitfalls

### Pitfall 1: COM apartment incompatibility between vrserver, WASAPI, and the driver DLL

**What goes wrong:**
The driver calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` from `DeviceProvider::Init` (or worse, lazily from a worker thread). vrserver may have already called `CoInitializeEx` with `COINIT_APARTMENTTHREADED` on that thread (it does on its rendering threads), and the second call returns `RPC_E_CHANGED_MODE`. Code that assumed COM is initialized proceeds anyway. `IMMDeviceEnumerator::EnumAudioEndpoints` either fails outright or — worse — succeeds, hands back interface pointers, and they get marshaled wrong because the apartment doesn't match. Crash 30 seconds later in a totally unrelated stack frame.

Conversely: developer assumes vrserver already initialized COM and skips `CoInitializeEx`. On a thread vrserver did NOT initialize (a freshly-spawned `std::thread` for audio capture), `CoCreateInstance(MMDeviceEnumerator)` fails with `CO_E_NOTINITIALIZED`.

**Why it happens:**
- COM apartment rules are per-thread, not per-process.
- The existing `WASAPIAudioCapture` (at `src/audio/src/audio_capture.cpp:196`) already calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` in its constructor and stores `comInitialized_`. Inside an EXE that's fine. Inside a DLL loaded by vrserver, the same call may fail with `RPC_E_CHANGED_MODE` if the calling thread is already STA — and `S_FALSE` from a different prior call shape is treated identically to `S_OK` here.
- `RPC_E_CHANGED_MODE` (`0x80010106`) is **not** an error to fail-out on — it means COM is already initialized on this thread in a different apartment. The right behavior is to use the existing apartment for as much as possible, and spin a dedicated MTA thread for WASAPI work that needs it.

**How to avoid:**
- In the shared lib's WASAPI initialization, **never** call `CoInitializeEx` on a thread you don't own. Create a dedicated `audio worker thread` inside `IAudioCapture::start()`. That thread calls `CoInitializeEx(nullptr, COINIT_MULTITHREADED)`, sets up `IMMDeviceEnumerator`/`IAudioClient`/`IAudioCaptureClient`, runs the capture event loop, and `CoUninitialize()` on its way out.
- Treat `S_OK`, `S_FALSE` (already initialized same apartment), and `RPC_E_CHANGED_MODE` distinctly. Only `S_OK` and `S_FALSE` are safe to proceed; `RPC_E_CHANGED_MODE` means "I'm not allowed to use COM here — bail out and start my own thread."
- The driver DLL's `DeviceProvider::Init` must NOT call `CoInitializeEx` on the calling thread. SteamVR does not document its threads' apartment state and it varies by SteamVR version. Defer all COM work to the audio worker thread.
- Audit: existing audio capture code uses `Microsoft::WRL::ComPtr` (good); however the `DeviceNotificationClient` does manual `InterlockedIncrement`/`InterlockedDecrement` (CONCERNS.md item). Wrap in `ComPtr` during the migration to avoid leaks across module boundaries.

**Warning signs:**
- `CoInitializeEx` returns `0x80010106 RPC_E_CHANGED_MODE` in driver logs at startup.
- WASAPI calls succeed in `mic_test.exe` but fail with `CO_E_NOTINITIALIZED` (`0x800401F0`) inside the driver.
- Random crashes in `wmilib.dll` or `combase.dll` minutes after startup, dump backtrace ends in COM marshaling code.
- Audio worker thread exits silently with no log line — symptom of an unhandled C++ exception thrown from a COM call on a misconfigured apartment.

**Phase to address:**
The shared-lib extraction phase (extract audio + WASAPI; Phase 1 of v1.6 in current sketch). Bake apartment ownership into `WASAPIAudioCapture::start()` from day one — retrofitting after the driver is already calling it is much harder.

---

### Pitfall 2: vrserver auto-launched at user login lacks microphone-access permission

**What goes wrong:**
User installs MicMap. `app.vrmanifest` registers MicMap as auto-launch. SteamVR is configured to auto-start (or the user has SteamVR pinned). On Windows 11 with mic-privacy enforcement enabled (the default since 2021), the Microphone access prompt is gated on the *first audio session start* — but in a service-or-background-spawned process at user login, the prompt either (a) never appears because no foreground window exists to host it, or (b) appears behind everything and gets dismissed on its own. `IAudioClient::Initialize` returns `AUDCLNT_E_DEVICE_IN_USE` or the WASAPI session opens with all-zero samples. Detection appears completely dead.

In contrast: when the user runs `mic_test.exe` interactively, the prompt shows correctly, they grant access, and audio works. So "works on dev rig" is the default state and the bug is only seen by clean-install users at first auto-launch.

Additionally: Windows 10/11 distinguishes "default capture device" from "default communication device" (the comm device is what apps like Discord use; the capture device is what generic apps see). If the user has them set to different devices, the driver may capture the wrong one. The legacy code uses `eMultimedia` role; users who set their headset mic as `eCommunications`-only end up with the wrong device.

**Why it happens:**
- Windows 11's `appsUseLocation` / `appsUseMicrophone` privacy gates apply per-package. A driver DLL has no AppUserModelID and inherits vrserver's. vrserver's package identity is `Valve.SteamVR` (when installed via Steam) — most users have already granted mic access to "Steam apps" but the gate enforces this per-process *executable path*, not per-app, so a fresh SteamVR auto-launch may still hit the prompt the first time.
- Auto-launched processes at login run before the user has a foreground session. UAC/access prompts don't have a parent HWND.
- `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eCapture, eMultimedia, ...)` and `GetDefaultAudioEndpoint(eCapture, eCommunications, ...)` can return different devices. The existing audio code likely hardcodes one role.

**How to avoid:**
- Document the first-run permission flow in README + installer finish page: "After install, please launch SteamVR once with MicMap. If Windows asks for microphone access, accept."
- Detect mic-access denial: `IAudioClient::Initialize` returning `AUDCLNT_E_DEVICE_IN_USE` or the resulting session producing all-zero buffers for >1 second is the heuristic. When detected, surface an error to the client UI (via the new IPC) so the user knows to grant access.
- Probe both `eMultimedia` and `eCommunications` roles on initial enumeration. Display both in the client's device picker. Default to the device the user previously selected; if absent, fall back to `eMultimedia`.
- Emit an audible-or-visible health signal early: log RMS dB of incoming audio for the first 10 seconds after capture starts. If RMS is exactly -inf (i.e., all-zero buffer), log "MIC ACCESS LIKELY DENIED" with a link to Windows Settings > Privacy > Microphone.
- Test the auto-launched-at-login case explicitly. v1.5 caught Pitfall 5 (manifest paths) only because real-hardware UAT was disciplined; v1.6 needs the same discipline for "fresh-OS-install + auto-launch" UAT.

**Warning signs:**
- Driver log shows audio capture started, but RMS dB readings are -∞ continuously.
- Detection never fires regardless of training/sensitivity (no signal to detect against).
- Client health UI shows "audio capturing" but waveform is flat.
- Works after user manually runs `mic_test.exe` once.

**Phase to address:**
Audio-capture-in-driver phase (the phase that lifts WASAPI into the driver). Audit also touches the installer phase (DOC entry on finish page).

---

### Pitfall 3: Audio thread (WASAPI render thread / capture thread) calls `UpdateBooleanComponent` directly

**What goes wrong:**
The "easy" v1.6 implementation collapses the IPC hop: detection thread sees a confidence spike, calls `m_dashboardManager->trigger()` which used to POST HTTP to the driver. Now that they're in the same process, why not call `VRDriverInput()->UpdateBooleanComponent` directly from the detection thread? The code compiles, the trigger fires, the dashboard opens. Ships. Then:

- Symptom A — silent failure: `UpdateBooleanComponent` returns `VRInputError_None` from the wrong thread for a while, then starts returning `VRInputError_InvalidHandle` after the HMD reactivates. The Pitfall-1 v1.5 reactivation handler in `RunFrame` flips state to `Invalidated`, but the audio thread doesn't see that flip and keeps trying to update the dead handle. Triggers stop working until SteamVR restart.

- Symptom B — race-then-crash: HMD deactivation event arrives in `RunFrame` (Plan 01-03 Pitfall 1 handler), `RunFrame` sets `hSystemClick_ = k_ulInvalidInputComponentHandle` and sets state to Invalidated. Concurrently, audio thread reads `hSystemClick_` (still seeing the old value due to no synchronization), calls `UpdateBooleanComponent(<dead handle>, true, 0)`. Best case: error logged. Worst case: SteamVR's input layer holds an internal lock + uses the invalid handle index past its allocation array → SEGV in `vrserver.exe`. Drops the user's VR session.

- Symptom C — deadlock: `VRDriverInput()` is implemented inside the OpenVR driver context bound by `VR_INIT_SERVER_DRIVER_CONTEXT` in `RunFrame`'s host thread. Some implementation versions take an internal mutex around component updates that's also held during driver shutdown. Audio-thread call holding that mutex while RunFrame thread holds the audio thread's join() → classic deadlock at SteamVR shutdown.

**Why it happens:**
The existing v1.5 architecture already correctly enforces "HTTP thread enqueues TapCommand → RunFrame drains → RunFrame calls OpenVR" (`driver/src/http_server.cpp:124-152` POST handler comment: "Never touches OpenVR API"). This is the load-bearing v1.5 lesson. v1.6 brings a *second* producer (the audio/detection thread inside the driver) that's just as forbidden. The temptation to "skip the queue, we're already in the driver" is high because (a) it's local code, (b) it removes a layer of indirection, (c) it works on the dev rig where the HMD never deactivates.

OpenVR's threading model is documented thinly: `IVRDriverInput` calls are expected from the same thread as `RunFrame` (vrserver's main pump). Valve owes no API-stability guarantee on cross-thread use.

**How to avoid:**
- **Hard rule, lifted from v1.5:** Only `RunFrame` calls OpenVR API. Period. Audio thread, detection thread, HTTP thread — all of them feed the existing `CommandQueue`. Adding new commands (e.g. `SetSettingsCommand`) is fine; calling OpenVR from new threads is not.
- Code-review checklist: any new file in `driver/src/` that includes `<openvr_driver.h>` and is not `device_provider.cpp` is a red flag. Any `VRDriverInput()` / `VRProperties()` / `VRServerDriverHost()` call from a callstack rooted in something other than `DeviceProvider::RunFrame()` is a defect.
- Static check: add a CMake target `driver_threading_check` that runs `grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost\|VRSettings' driver/src/` and asserts the only file matched is `device_provider.cpp`. Run on every PR.
- For commands the audio thread needs to send (e.g. "trigger fired", "state changed to detecting", "config-reload requested"), extend `command_queue.hpp` with new `struct` types and a `std::variant<TapCommand, ReloadConfigCommand, ...>`-shaped queue. Keep the queue lock-free-ish via `std::lock_guard` and bounded depth (already done at `kMaxDepth = 8`).
- Detection thread's trigger path becomes: detection→state machine→`commandQueue_->push(TapCommand{})`→return. Identical pattern to today's HTTP-thread path.

**Warning signs:**
- Triggers fire reliably for a while, then stop after the user sleeps/wakes the HMD.
- vrserver.exe crashes with access violation in `vrserver_lib.dll` after extended sessions.
- Stress test (trigger-spam during HMD sleep cycle) deadlocks at shutdown.
- A grep of the driver source finds `VRDriverInput()` outside `device_provider.cpp`.

**Phase to address:**
The phase that introduces the in-driver detection+trigger pipeline (likely Phase 2 or 3 of v1.6). The lint/grep check goes in the same PR.

---

### Pitfall 4: Driver lifecycle vs WASAPI device lifecycle — leaked capture session on `Cleanup()`/`Init()` cycles

**What goes wrong:**
SteamVR can call `IServerTrackedDeviceProvider::Cleanup()` and then `Init()` again within the same vrserver process lifetime. This happens during HMD reactivation cycles, driver hot-reload (rare but possible), or "Restart SteamVR" without quitting vrserver. v1.5 currently handles this — `Cleanup()` resets `hSystemClick_`, kills the HTTP server, drops the CommandQueue. v1.6 adds the audio capture path. If `Cleanup()` doesn't:

1. Stop the audio capture thread (cleanly join, not detach).
2. Release `IAudioCaptureClient` / `IAudioClient` / `IMMDevice` references.
3. Unregister the `IMMNotificationClient` (currently created in `WASAPIAudioCapture` at `src/audio/src/audio_capture.cpp:116`).
4. Wait for the WASAPI buffer service to release its event handle.

…then `Init()` re-creates a parallel audio session. Now there are two `IMMNotificationClient`s registered on the system (or worse, two `IAudioClient`s holding leases on the same endpoint in `Shared` mode — the second `IAudioClient::Initialize` returns `AUDCLNT_E_DEVICE_IN_USE`). Audio capture appears dead in the new session because the old one still holds the device.

Also — the existing code's `DeviceNotificationClient::onDeviceRemoved` fires on the *MMDevice notifier thread*, not a thread the driver controls. If that callback dereferences any object owned by the driver (state machine, command queue) after `Cleanup()` has destroyed those objects, that's a use-after-free across threads.

**Why it happens:**
- WASAPI ownership is asymmetric: `IAudioClient::Stop` is synchronous, but the underlying audio-engine reference is released via COM `Release()` which may defer cleanup to a system thread.
- COM notifiers (`IMMNotificationClient::OnDeviceStateChanged`) run on a system-managed thread that doesn't synchronize with your destructors.
- The current `audio_capture.cpp` constructor calls `CoInitializeEx` and the destructor relies on RAII via `ComPtr`, but `CoUninitialize` is not visible in the snippet — if it's missing, the WASAPI subsystem leaks across `Init`/`Cleanup` cycles.
- v1.5's `device_provider.cpp` Cleanup clears HTTP server, command queue, and HMD handle state, but doesn't yet have audio. Adding audio means adding teardown ordering: audio first (stop capture → notify queue → drain pending → release), THEN command queue, THEN HTTP server.

**How to avoid:**
- Tighten `Cleanup()` ordering. Tear down in reverse of construction:
  1. **Stop audio capture first.** `IAudioCapture::stop()` must (a) signal the capture thread to exit, (b) join it, (c) unregister `IMMNotificationClient` via `IMMDeviceEnumerator::UnregisterEndpointNotificationCallback`, (d) `Release()` all `ComPtr`s, (e) `CoUninitialize()` on the worker thread.
  2. Stop the detection state machine (drain pending events).
  3. Drain CommandQueue and stop HTTP server (existing v1.5 sequence).
  4. Reset HMD handle state, set state to NotReady.
  5. `VR_CLEANUP_SERVER_DRIVER_CONTEXT()`.
- Make `IAudioCapture::stop()` strictly idempotent and synchronous. After it returns, no callbacks may fire. Use a `std::shared_ptr<State>` captured by the notifier callback and check `state->shuttingDown` before dereferencing anything else.
- Test: write a stress test that calls `Init()` → wait 500ms → `Cleanup()` → repeat 50 times. WASAPI should still work on iteration 50. Currently v1.5 does this for HTTP + CommandQueue (implicit via the 5-cycle SteamVR restart UAT) — v1.6 must extend it to audio.
- Handle `OnDefaultDeviceChanged` and `OnDeviceStateChanged` (currently DEVICE_STATE_NOTPRESENT triggers `onDeviceRemoved_` callback). Currently the existing code maps both `DEVICE_STATE_UNPLUGGED` and `DEVICE_STATE_DISABLED` to "removed" (`audio_capture.cpp:147-153`) — confirm `DEVICE_STATE_NOTPRESENT` is also covered when used inside the driver. Also handle `OnDefaultDeviceChanged` so when the user changes their default mic in Sound settings, the driver follows (or at least surfaces a notification).
- For ergonomic shutdown: bound the cleanup time with a watchdog. If audio cleanup hasn't returned in 2 seconds, log and force-kill the worker thread (last resort). v1.5 used a 2s watchdog for `VREvent_Quit` shutdown — same pattern applies.

**Warning signs:**
- Second SteamVR session after a "Restart SteamVR" produces no audio in the driver, but driver log shows capture started.
- Process Explorer shows `driver_micmap.dll` keeping a handle on the audio device after `Cleanup()`.
- Random crashes during `Cleanup()` with stack frame in `wbemcomn.dll` or `combase.dll`.
- 5-cycle stress test (Init/Cleanup loop) leaks GDI handles or accumulates audio sessions visible in Sound mixer.

**Phase to address:**
Audio-capture-in-driver phase. Specifically the `IAudioCapture` lifecycle hardening — write the stress test before claiming the phase done.

---

### Pitfall 5: Config file read-write race between client (writer) and driver (reader)

**What goes wrong:**
Client writes `%APPDATA%/MicMap/config.json` via the v1.5-shipped atomic `ReplaceFileW` path (CFG-04). Driver reads the same file at `Init()` and on a "config changed" notification. Two concrete failure modes:

1. **Read-during-rename:** Client calls `ReplaceFileW` to atomically swap `config.json.tmp` over `config.json`. Driver opens `config.json` for read 50µs later. On Windows, `ReplaceFileW` is atomic for the filename mapping but the underlying NTFS transaction may briefly leave the path returning ERROR_SHARING_VIOLATION (32) if an open handle existed during the swap. Read fails. Driver falls back to defaults. Settings lost.

2. **Stale read:** Driver reads config at `Init()`, caches `AppConfig` in memory. Client changes a setting and calls Save. Driver never finds out. Client→driver IPC sends "settings changed, please reload" — but if that IPC drops a packet (HTTP timeout, port collision per existing concerns), driver runs forever with stale settings. UI appears to update; behavior doesn't.

3. **`training_data.bin` amplification:** Same problem with a much larger payload (~150 samples × N bytes). Atomic rewrite is feasible but the read path is more expensive — if the driver re-loads training data on every "config change" notification, you've created a spike of disk I/O blocking either the audio thread or RunFrame.

**Why it happens:**
- Two-process JSON ownership without IPC for change events is a textbook race. v1.5 was single-writer (client-only); v1.6 introduces dual ownership.
- Windows file-watching APIs (`ReadDirectoryChangesW`, `FindFirstChangeNotificationW`) work but require a watcher thread inside the driver — yet another thread to manage in `Cleanup()`.
- The v1.5 `ReplaceFileW` pattern at `src/core/src/config_manager.cpp` is atomic for *the file content* but does not synchronize with concurrent reads. Standard fix is the writer holds the file briefly (`FILE_SHARE_READ` only, no `FILE_SHARE_WRITE`); existing code likely uses `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE` defaults.

**How to avoid:**
- **Drop file-watching as the change-detection mechanism.** Use the existing HTTP IPC: client `POST /config` pushes the new `AppConfig` JSON; driver validates, swaps in-memory state under a mutex, persists to disk via the same atomic-write path. Single producer (client) per write; driver is reader-only on disk after Init.
- For the **boot read** (driver Init reads `config.json` fresh — client may not be running yet): wrap the read in a 3-attempt retry with 50ms backoff. If `ERROR_SHARING_VIOLATION` (32) is returned, sleep + retry. If all retries fail, fall back to defaults and log (existing v1.5 behavior already handles corrupt-file fallback at Phase 2 / CFG-02 — extend it to handle locked-file).
- For `training_data.bin`: route the same way. Client `POST /training-data` streams the binary blob (multipart or raw body), driver validates, writes atomically, swaps in-memory pattern. Driver does NOT reload from disk on signals — disk is the persistence layer, RAM is the source of truth.
- Use `MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING` flags on `MoveFileExW` (or `REPLACEFILE_WRITE_THROUGH` on `ReplaceFileW`) when persisting. This forces NTFS to flush before returning.
- For atomicity gotchas: v1.5 already documented this as solved (Pitfall 9 from v1.5 catalog: defensive nlohmann/json read with try/catch + backup-and-rotate). Extend to v1.6 by ensuring the driver-side read uses the **same** read function (it lives in the shared lib `libmicmap_core` — single-source-of-truth principle).

**Warning signs:**
- Driver and client disagree on settings: client UI shows sensitivity 0.7, driver behaves like 0.5.
- After a save, intermittent driver log entries showing parse fallback to defaults.
- Race-only-on-slow-disk: a tester with HDD reports settings sometimes don't take effect; SSD users never see it.

**Phase to address:**
IPC-reshape phase. Decide "client pushes config, driver does not file-watch" early in the milestone — it changes the requirements (no file-watcher) and reduces the threading surface.

---

### Pitfall 6: Static lib linked into both DLL (driver) and EXE (client) — symbol-visibility ODR violations

**What goes wrong:**
The shared lib `libmicmap_core` is built once and linked statically into both `driver_micmap.dll` and `micmap.exe`. The same translation units get compiled twice with possibly different macros (e.g., `MICMAP_DRIVER_BUILD` defined for the driver target, undefined for the client). Concrete failure modes:

1. **Different inline-template instantiations:** `nlohmann::json` is heavily templated. If `driver_micmap.dll` and `micmap.exe` both link `libmicmap_core` and both use `nlohmann::json`, each ends up with its own private template instantiations. Link-time bloat (40-100 KB extra per binary), potentially different vtables for the same `json` shape, occasional crashes when objects are passed across module boundaries (don't do this — but we won't, the boundary is JSON-over-HTTP). Also: if you ever link a *third* TU that exports a `json` parameter, you've got an ODR violation that newer linkers (`/WX` + LTCG) flag as error LNK2005 / LNK4006.

2. **`__declspec(dllexport)` on the wrong side:** `libmicmap_core` declares `IAudioCapture::stop()` with no visibility annotation. Compiles fine for both. At driver-DLL link time, `stop()` is internal to the DLL, fine. At driver→client interaction time… we don't share C++ objects across the boundary, so this is OK. But: if any header in the shared lib *also* defines `extern "C"` exports (e.g., a logging shim), and that header is included from both consumers, the symbol may end up exported from both binaries. `mic_test.exe` linking the same lib + that header could expose the same symbol, leading to runtime ambiguity if both DLLs are loaded into the same process (not currently a scenario, but easy to drift into).

3. **COM apartment leakage:** Discussed in Pitfall 1 — but the shared-lib version: `WASAPIAudioCapture::WASAPIAudioCapture()` calls `CoInitializeEx`. If the constructor runs in `mic_test.exe` once, it initializes COM for `main` thread MTA. In the driver, the same constructor runs but now on a thread vrserver picked, possibly STA. The shared-lib code is identical but its environmental assumption is different. Bug looks like "shared lib is buggy" when really the construction context is wrong.

4. **Static globals duplicated across modules:** `libmicmap_core` has a logger singleton (`Logger::instance()`). Built once, linked twice: driver gets its own logger instance, client gets its own. If both are running, both write to `%APPDATA%\MicMap\micmap.log` — interleaved, with two writers and no synchronization. File corruption. (See Pitfall 7 too.)

**Why it happens:**
- C++ ODR is *strict* across translation units — same symbol, same definition, full stop. Link-time deduplication works for inline functions and templates but not always; the "differently compiled with different macros" case explicitly violates ODR.
- Static libraries have no concept of "exports" — every symbol is potentially visible to whatever links it. CMake's `target_link_libraries(... PRIVATE)` controls inclusion but not visibility.
- Windows-specifically: `__declspec(dllexport)` on a class member exports the *member* from the linking DLL. If the same source is linked into two DLLs, both DLLs export the symbol with the same decorated name → loader ambiguity.

**How to avoid:**
- **Strict TU hygiene:** No `__declspec(dllexport)` / `__declspec(dllimport)` annotations in the shared lib at all. The shared lib is `STATIC` only (CMake `add_library(micmap_core STATIC ...)`). It's an internal implementation detail of each consumer.
- **Single macro frontier:** Avoid `#ifdef MICMAP_DRIVER_BUILD` in the shared lib. Where divergent behavior is needed (e.g., logger sink), inject it via constructor / dependency injection — `IAudioCapture::start(LogSink sink)` rather than compile-time switching. The existing `driverLogSink` pattern in `device_provider.cpp:28-30` is the right shape; extend it to all shared-lib seams.
- **Don't pass STL/template objects across the HTTP boundary.** JSON is a string. Settings come back as a `std::string` of JSON, parsed inside the consumer's TU. No `nlohmann::json` objects cross processes; therefore template instantiations don't need to match between driver and client.
- **Pin the nlohmann/json version** at the project level. It's already vendored (`external/`). The same vendored copy is used by both targets — same instantiations.
- Use CMake to enforce: `add_library(micmap_core STATIC ...)`, `target_compile_options(micmap_core PRIVATE /WX /W4)` (warnings clean — already the v1.5 standard), and **never** `target_link_libraries(driver_micmap PUBLIC micmap_core)` (PRIVATE only).
- Build-time guard against "shared lib leaks symbols" by running `dumpbin /exports driver_micmap.dll` and asserting only `HmdDriverFactory` is exported. Run on every CI build.
- For the logger duplication: Logger should accept a sink at construction time (driver passes `DriverLog`; client passes the desktop file logger; `mic_test.exe` passes `printf`). No global singletons in the shared lib.

**Warning signs:**
- Linker warnings: `LNK4006 (symbol already defined; second definition ignored)`, `LNK4042 (object specified more than once)`.
- Driver DLL size unexpectedly large (>5MB without justification).
- `dumpbin /exports driver_micmap.dll` shows symbols other than `HmdDriverFactory`.
- Two processes writing to `micmap.log` produces interleaved/corrupted lines.
- Behavior diverges between `mic_test.exe` and the driver despite identical source — TU compiled with different effective macros.

**Phase to address:**
Shared-lib extraction phase (Phase 1 of v1.6). Set the boundaries before *any* code moves into the lib — establish the rules (STATIC, no dllexport, no static singletons, dependency-injected sinks) and enforce in CMake.

---

### Pitfall 7: IPC reshape — dead trigger code paths and broken localhost-only binding

**What goes wrong:**
v1.5 has `POST /button {"kind":"tap"}` as the trigger path (client→driver). v1.6 removes that path because trigger is now in-process. Easy to leave residue:

1. **Dead client code:** `IDriverClient::tap()` in `src/steamvr/src/vr_input.cpp` may still exist, no callers, unbuilt warnings suppressed. Refactor PR misses it. Three months later someone wonders why there's a tap method.

2. **Dead driver route:** `http_server.cpp:129` POST `/button` handler still registered. Any process can POST to it and force a dashboard toggle without going through detection. Localhost-only mitigates this, but leaving the attack surface open is sloppy.

3. **Settings push semantics drift:** New `POST /settings` from client. Should it accept partial updates (`{"sensitivity": 0.7}`) or full `AppConfig`? Validation order: schema first, range-clamp next, then commit? What if the client sends a sensitivity value of -1? What if it sends a device GUID for a device that no longer exists?

4. **Training-sample stream:** Client streams ~150 samples (the existing `MIN_TRAINING_SAMPLES * 3`). If the client crashes mid-stream, driver has half a training set. If driver restarts mid-stream, client gets a connection drop and can't tell whether the partial data was committed.

5. **localhost-only regression:** v1.5 binds `127.0.0.1` (or attempts to). New routes added for v1.6 settings/training may inherit a different binding by mistake — `0.0.0.0:port` would let any LAN host trigger settings changes. v1.5 catalog Pitfall (Security #1) is worth re-checking against v1.6 routes.

**Why it happens:**
- "Remove old code" is rarely a clean diff when the code is wired through interfaces (factory pattern + IDriverClient).
- HTTP libraries (cpp-httplib) bind based on the host string passed to `bind_to_port`. Misnaming `host_` to `"0.0.0.0"` for any reason silently opens the network.
- Validation order matters: range-clamp before schema is wrong (an out-of-range value with the right type passes schema, gets clamped, and you never log the original). Schema-then-range is the only sane order.

**How to avoid:**
- Apply the v1.5 SVR-04 rip-out discipline: removal checklist enumerating every file referencing the trigger path. Specifically: `IDriverClient::tap()`, `IDriverClient::click()`, `dashboard_manager.cpp` (any remaining stubs), driver-side `/button` POST handler in `http_server.cpp:129-152`. Compile with `-Werror`/`/WX` and `-Wunused-function`/`-Wunused-variable`.
- v1.5 v1.5-PITFALLS.md introduced a "forbidden-string sweep" (Pitfall 7). Repeat for v1.6: after the migration, `grep -rn 'IDriverClient::tap\|/button\|TapCommand' src/ apps/` should return zero hits in client code (TapCommand is fine inside `driver/`).
- For settings push: send full `AppConfig` JSON; partial updates introduce more bugs than they save. Validation pipeline (in shared lib, used by both consumers): parse → schema-check (required fields, types) → range-clamp (with WARN log on each clamp) → commit-or-reject. v1.5 Phase 2 already shipped the clamp/pow2-snap logic — reuse it.
- For training-sample stream: chunked POST with a transaction ID. Client begins with `POST /training-session/begin` → gets ID → streams samples → `POST /training-session/{id}/finalize` to commit, or the driver auto-aborts after 30s without a finalize. Driver writes new `training_data.bin` only on finalize, atomically (existing v1.5 atomic-write pattern).
- For localhost-only: extract host binding into a single `kHttpBindAddress = "127.0.0.1"` constant in `http_server.cpp` (or shared lib); add a unit test that `HttpServer::Start()` invoked with default args binds only to `127.0.0.1`. Add a smoke test in `mic_test.exe` (or a new `driver_smoke_test`) that asserts `netstat -an` does not show `0.0.0.0:<port>` for the driver.
- Consider adding a per-boot shared secret in headers as defense-in-depth (already noted as deferred in v1.5 catalog — same status v1.6).

**Warning signs:**
- Tap behavior continues working even after detection is stopped — means the `/button` route still exists and *something* is hitting it (or there's a leaked path).
- Linker warns about unused `tap()` symbol but build still succeeds.
- Settings change has no effect, or has unexpected effect (validation skipped a field).
- LAN coworker can trigger a teammate's dashboard.

**Phase to address:**
IPC reshape phase. The migration phase that introduces in-process detection will be tightly coupled to the dead-code removal — bundle them.

---

### Pitfall 8: Driver-side logger opens log file before SteamVR working dir / AppData are usable

**What goes wrong:**
The shared lib's logger writes to a file. The driver instantiates the logger early (TU static initializer or `DeviceProvider::Init()`'s prologue). At that moment the driver is loaded by vrserver but the working directory of the process is `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\` — not the driver's directory, not the AppData directory. If the logger uses a relative path, the log file lands in vrserver's CWD (write may fail under UAC if SteamVR was installed under Program Files). If it uses `SHGetKnownFolderPath(FOLDERID_RoamingAppData, ...)` from a service-spawned process at user login, the call may resolve to the SYSTEM account's AppData, not the user's — silent misroute.

Additionally: client and driver both open the same log file (`%APPDATA%\MicMap\micmap.log`) in append mode. Two writers, no coordination, line-level interleaving at best, file corruption at worst.

**Why it happens:**
- Driver DLLs run in vrserver's process context. CWD is vrserver's CWD; not the DLL's own location.
- `SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, NULL, &path)` with `NULL` token returns the *current process token's* AppData. If vrserver was started from a Windows service or scheduled task, that's not the interactive user's AppData. Edge case but real on systems with non-default Steam-launcher configs.
- vrserver has its own log file (`%LOCALAPPDATA%\openvr\logs\vrserver.txt`) that captures driver `DriverLog` output. Adding a second file (driver's own logger writing to AppData) doubles the surface.

**How to avoid:**
- **Path resolution:** Use `SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_NO_ALIAS | KF_FLAG_DONT_VERIFY, NULL, &path)` and cache once at first use. Fall back to `GetEnvironmentVariableW(L"APPDATA", ...)` if the call fails. Log the resolved path on first use. v1.5 codebase uses `SHGetFolderPathW` (older API) at `src/core/src/config_manager.cpp:25-34` — upgrade to `SHGetKnownFolderPath` for consistency and KF flags.
- **Driver-side: defer log-file open** to `DeviceProvider::Init` (after `VR_INIT_SERVER_DRIVER_CONTEXT`). Until then, all logging goes through `DriverLog` only (which lands in vrserver.txt and is always available). This matches v1.5's existing `driverLogSink` shim shape.
- **Don't write to `%APPDATA%\MicMap\micmap.log` from the driver.** The driver's log file should be `%APPDATA%\MicMap\driver.log` (separate file). Single-writer per file. v1.5 deferred file-sink logger to backlog; if v1.6 ships it, ship it as two files.
- **Race resilience:** Open files with `FILE_SHARE_READ | FILE_SHARE_WRITE` and append mode (`O_APPEND` semantics → `dwShareMode=...`, `dwCreationDisposition=OPEN_ALWAYS`, then `SetFilePointer(... FILE_END)` is unnecessary if you use the system call that handles it).
- Test: launch SteamVR via different mechanisms (Steam library shortcut, command-line, scheduled task, Windows service) and verify driver logs land in the *interactive user's* AppData each time.

**Warning signs:**
- Driver appears to be running but no file appears at expected log path.
- Log file appears in `C:\Windows\System32\config\systemprofile\AppData\` (system AppData) instead of user's.
- Client and driver logs interleave; log lines are partial.
- File-permission denied errors in vrserver.txt for the driver's log path.

**Phase to address:**
Logger setup occurs in the shared-lib extraction phase if a file-sink is added; otherwise audio-capture-in-driver phase (when the first need to log audio events from inside the driver arises).

---

### Pitfall 9: `mic_test.exe` (headless harness) drifts away from driver's reality

**What goes wrong:**
`mic_test.exe` continues to use `libmicmap_core` directly with no SteamVR. Easy mistake: a feature-flag added in the driver path (e.g., "if running inside driver, use a different audio buffer size") is not exercised in headless path. `mic_test.exe` passes; production fails. Or the reverse: a bug fix made for `mic_test.exe`'s test rig (e.g., disabling a noise filter) doesn't get into the driver build.

Concretely: `mic_test.exe` uses a different `LogSink` (printf), runs a different audio thread (no driver `RunFrame` to gate it), and is started on the dev's terminal (interactive session, all permissions granted, foreground window). The driver has none of these. Regressions slip through.

**Why it happens:**
- The whole point of extracting the shared lib is that the same code paths are exercised. Drift defeats the purpose.
- Build-time `#ifdef`s creep in to handle "differences" — usually for things that should be runtime-injected instead.
- `mic_test.exe` lacks the IPC layer (no HTTP), so the IPC-pushed-settings flow is not exercised at all from the test harness — but the driver depends on it heavily.

**How to avoid:**
- **Zero `#ifdef MICMAP_DRIVER_BUILD` in the shared lib.** Differences are runtime-injected via interfaces (e.g., `IConfigSource` — driver implements via "wait for HTTP push", `mic_test` implements via "load default + apply CLI flags").
- Add a `mic_test.exe` smoke step that exercises the *full* detection pipeline including config-load, training-load, audio-capture, FFT, state-machine, and emit-trigger — same paths the driver runs. The only thing it can't exercise is the OpenVR boundary; that's fine, the CommandQueue→OpenVR step is small and visually verifiable.
- Add a hot-path lint: a CMake target `headless_canary` that compiles `mic_test.exe` with `-Werror` against the shared lib and asserts at runtime that it can replicate the driver's startup sequence (load config → load training → start capture → enter detection state → emit trigger callback once on stub input). If this canary fails, the driver build will too.
- For things `mic_test.exe` *cannot* exercise: write driver-specific integration tests using `hmd_button_test.exe` analog (call the driver's HTTP routes directly with curl-equivalent). v1.5 already validates this pattern.
- For divergent threading models (driver: RunFrame at 90Hz; mic_test: free-running): the audio→state-machine→trigger callback chain must be RunFrame-agnostic. The CommandQueue is the only place that knows about RunFrame. Verify by ensuring the shared lib has no `RunFrame` reference at all.

**Warning signs:**
- A bug fix is "tested" only via `mic_test.exe` and ships broken in the driver.
- A bug found in the driver cannot be reproduced in `mic_test.exe` despite identical inputs — divergent code paths.
- `git grep MICMAP_DRIVER_BUILD src/` returns hits in non-driver TUs.

**Phase to address:**
Shared-lib extraction phase. Establish "no driver-only macros in shared lib" as a hard rule at the start; enforce on every PR.

---

### Pitfall 10: Migration boundary states — double-trigger and stale-callsite hazards

**What goes wrong:**
v1.6 is multi-phase. Mid-milestone the codebase has, e.g., audio capture lifted to driver but trigger still going through HTTP (because Phase 2 = audio lift, Phase 3 = trigger lift). A user who builds from main mid-milestone gets:

1. **Double-trigger:** Client-side detection still on (because the client UI hasn't been demoted yet) AND driver-side detection just turned on. Both fire on a mic cover. Dashboard opens, then "tap" via HTTP triggers a second time, dashboard closes. Looks like noise.

2. **Stale callsite:** Shared lib extracted, but the client still calls `core::createConfigManager()` from its old non-shared location. The two paths return different impls, and the audio thread's `IConfigSource` differs from the UI's. Settings appear desynced.

3. **CMake target divergence:** `libmicmap_core.lib` now exists, but `apps/micmap/main.cpp` still links `audio_capture.cpp` directly (its old path). Silently shadows the lib. Bug fixes to `audio_capture.cpp` don't reach the client because client links its own copy.

4. **Schema skew between phases:** Phase 2 changes the `AppConfig` to add a `triggerInDriver: bool` flag for migration. Phase 3 removes it. A user who upgrades from a Phase 2 tag to a Phase 3 tag has a config with the old flag, parser silently ignores it (nlohmann is permissive there), but the migration script in the installer didn't run because there's no installer for intermediate phases.

**Why it happens:**
- Phased migration without atomic feature-flagging means intermediate states ship.
- `git pull` mid-milestone is a real user behavior in the dev team; if intermediate states crash, productivity stalls.
- CMake's `target_link_libraries` resolves at configure time; orphaned `target_sources(micmap PRIVATE audio_capture.cpp)` lines stay until manually deleted.

**How to avoid:**
- **Single feature flag for the whole migration:** `MICMAP_DRIVER_OWNS_DETECTION` (compile-time or config-runtime). When set, driver does everything; when unset, v1.5 behavior. Each phase that lifts a piece of work flips the flag's coverage incrementally; final phase deletes the flag. This is the v1.5 SVR-04 lesson from a different angle — "don't ship dual-mode" stays the rule, but a *transient* feature flag in development branches is acceptable as long as it's deleted by milestone close.
- **Atomic phase commits:** Each phase ends with the codebase in a self-consistent state. No "Phase 2 is half-done; Phase 3 will finish it" — Phase 2's last commit must build, run, and pass UAT *as-is*.
- **Single feature toggle at runtime:** During migration, `AppConfig` may have a `useDriverDetection` flag that the client honors (skips its own detection if true). Driver always does detection. After milestone close, the flag is removed.
- **CMake target enforcement:** `add_library(micmap_core STATIC ...)` lists every shared-lib TU explicitly. `apps/micmap/CMakeLists.txt` and `driver/CMakeLists.txt` link to `micmap_core` and *do not* compile their own copies. Add a CI check: `find apps/ driver/ -name '*.cpp' | xargs grep -l 'audio_capture'` should find no inline duplications.
- **Schema migration discipline:** Each `AppConfig` change has a version bump. Reader handles unknown version → log + use defaults + don't crash. v1.5 already shipped this for corrupt-file fallback (CFG-02); extend to "unknown version" case.
- **Mid-milestone testing:** After each phase, run the full UAT (or a representative subset). v1.5's discipline of "5-cycle SteamVR restart UAT" between phases caught regressions early — keep doing it.

**Warning signs:**
- A trigger fires twice (dashboard open + immediately close) — symptom of dual-detection.
- After a `git pull`, settings reset to defaults — symptom of schema skew.
- `dumpbin /dependents driver_micmap.dll` shows duplicate dependencies — symptom of lib double-link.
- Dev-team user reports "I can't get from Phase 2 to Phase 3 cleanly" — schema migration missed.

**Phase to address:**
All phases. Apply the discipline at phase boundaries; add the CMake check during shared-lib extraction phase.

---

### Pitfall 11: v1.5 priors — `VR_Init` reentry, `IsApplicationInstalled` poll guard, atomic config write — recurring with new shapes

**What goes wrong:**
v1.5 paid for several lessons. Each can recur in v1.6 in a new place:

1. **Double `VR_Init` (commit `3187fbb`):** v1.5 caught this in the manifest registrar. v1.6 risk: shared-lib code now compiled into `mic_test.exe`. If `mic_test.exe` is updated to optionally connect to OpenVR (e.g., a "send tap to driver" debug feature), and the shared lib is also linked into the driver and the client, you have three potential `VR_Init` callers. Single-process double-init was the v1.5 SEGV. Make sure shared lib never calls `VR_Init` itself — that's a consumer's responsibility.

2. **`IsApplicationInstalled` poll guard (OpenVR #1378):** v1.5 added a 2-second poll between `AddApplicationManifest` and `SetApplicationAutoLaunch`. v1.6 risk: if migration adds new application manifest registration shapes (e.g., the driver itself becoming an "auto-launched scene app"? No — driver is loaded by vrserver, not auto-launched. But if any new `vr::VRApplications()` calls are added — say to query `GetApplicationKeyByProcessId` for some reason — confirm the same propagation-delay model applies and add the poll guard.

3. **`vrpathreg removedriver`-before-`adddriver` (OpenVR #1653):** v1.5 made this unconditional. v1.6 doesn't change driver-registration so this should still hold. But if the installer is rebuilt (Phase 4 carryover or a new installer), make sure this remains.

4. **`ReplaceFileW` atomicity (CFG-04):** v1.5 used it for client-side config save. v1.6 brings driver-side writes for `training_data.bin` (potentially driver-side config save too if the driver is fully self-sufficient). Reuse the same shared-lib `atomicReplaceFile` helper. Don't reimplement.

5. **`AcknowledgeQuit_Exiting` ack-first (commit `73681c5` and Phase 03-05 fix for OpenVR #1425):** v1.5 made the client app respond cleanly to `VREvent_Quit`. v1.6 doesn't change client-side quit handling, but if the client is demoted to "settings only," its lifecycle may shift — does it auto-quit when the driver detects shutdown? If so, the same `AcknowledgeQuit_Exiting` discipline applies.

6. **v1.5 Pitfall 1 (HMD reactivation lifecycle):** Already in v1.5 driver code (`device_provider.cpp:122-134`). Doesn't change in v1.6; just confirm the `Cleanup`→`Init` cycle still works after audio-thread is added (Pitfall 4 above).

**Why it happens:**
Each of these was a multi-day debug session in v1.5. The institutional memory is the v1.5 PITFALLS.md and the commit messages. New code risks rediscovering them.

**How to avoid:**
- Cite v1.5 PITFALLS.md in new requirements/plan docs. When adding any OpenVR call, check "did v1.5 already learn something here?"
- Keep the v1.5 helpers (atomic-replace, manifest-registrar with poll guard, `AcknowledgeQuit_Exiting`-ordered shutdown) in the shared lib and reuse, don't reimplement.
- Add a "learned in v1.5" prefix comment on the relevant call sites so v1.6 PR reviewers can quickly see the heritage.

**Warning signs:**
- A v1.6 PR adds a new `VR_Init` call. Reviewer sees it; cross-checks whether existing in-process session covers the use case (per commit `3187fbb`).
- A v1.6 PR adds a new manifest-registration call without the poll guard. Reviewer flags for OpenVR #1378.
- A v1.6 PR introduces a new `MoveFileExW` for atomic write that doesn't use the shared `atomicReplaceFile` helper.

**Phase to address:**
All phases. Establish in the requirements doc that v1.5 PITFALLS.md is a prerequisite read for anyone working on v1.6.

---

## Moderate Pitfalls

### Pitfall 12: WASAPI capture buffer and FFT pacing inside RunFrame's 11ms budget

**What goes wrong:**
Detection thread runs FFT on every audio packet (~10ms cadence at 100Hz audio). Driver's `RunFrame` runs at 90-120Hz (~8-11ms). If the detection-thread CommandQueue producer rate (audio packets) outpaces RunFrame's consumer rate (queue drain), and the queue depth is bounded at 8, oldest commands drop. The trigger that the user expects never fires because it was dropped under load.

Conversely: if FFT is *too slow* (e.g., a default size of 2048 on a slow CPU), audio thread blocks the device-event-driven WASAPI buffer service → audio glitches → detection sees discontinuous samples → false negatives.

**Why it happens:**
v1.5 RunFrame budget discipline was around HTTP→queue→drain. Audio→queue→drain has higher producer rate (continuous, not bursty).

**How to avoid:**
- For trigger commands: rate-limit at producer side. State machine emits at most one TapCommand per cooldown window (already the behavior). Don't push from raw FFT — push from state-machine state-change (rising edge of Triggered).
- For other state-update commands (e.g., `StateChangedCommand`): batch or drop-newest. The client only needs the latest state for the health UI; older state updates are stale.
- Profile FFT cost on Bigscreen Beyond's representative CPU. CONCERNS.md Performance Bottleneck #2 calls out FFT-on-every-frame as a smell — already on the radar.
- Maintain v1.5 invariant: `RunFrame` < 1ms in dev-build timing asserts. Add audio-driven trigger to the path; re-verify.

**Phase to address:**
Audio-capture-in-driver and detection-in-driver phases.

---

### Pitfall 13: `IMMNotificationClient` callback runs after driver is unloaded → process crash

**What goes wrong:**
The audio capture's `DeviceNotificationClient` (registered with `IMMDeviceEnumerator::RegisterEndpointNotificationCallback`) is called on a system-managed thread. If the driver's `Cleanup()` returns before that callback's `OnDeviceStateChanged` finishes, the callback may dereference freed memory.

Worse: if `Cleanup()` calls `UnregisterEndpointNotificationCallback` and THEN releases the `DeviceNotificationClient`, but a callback was already mid-flight at the moment of unregister, the callback continues executing on freed memory. Classic use-after-free.

**Why it happens:**
COM callbacks are not cleanly synchronizable. `Unregister` is documented to "no longer call the callback after returning" but does NOT wait for in-flight callbacks to complete.

**How to avoid:**
- Wrap the callback in a `std::shared_ptr<State>` lambda. The callback captures the shared_ptr; the State has a `std::atomic<bool> alive`. Callback checks `alive` before doing anything.
- On `Cleanup`: set `alive = false`, then `Unregister`, then drop the `shared_ptr`. The COM lifecycle releases the notification client when the last ref drops; even if a callback is in flight, it sees `alive=false` and bails out before touching destroyed members.
- Hold a brief CS / mutex inside the callback to serialize against the destructor.

**Warning signs:**
- Crash during SteamVR shutdown when an audio device was just unplugged.
- Dump backtrace ends in `mmdevapi.dll` calling our callback.
- "Restart SteamVR" while unplugging the mic crashes vrserver.

**Phase to address:**
Audio-capture-in-driver phase. The IMMNotificationClient hardening goes alongside the lifecycle work.

---

### Pitfall 14: `OnDefaultDeviceChanged` makes the default mic move under the driver's feet

**What goes wrong:**
User sets their gaming headset mic as default capture device, starts MicMap, gets training. Later they plug in a USB mic for a meeting; Windows auto-changes the default capture device. MicMap's selection is "default device" (eMultimedia role). The audio engine quietly switches; MicMap is now capturing from a totally different microphone with totally different acoustic characteristics. Detection threshold trained on headset mic doesn't fire on USB mic, or vice versa, fires at random.

Same problem with `eCommunications` role: many users have their headset mic as default communications and built-in mic as default multimedia — picking the wrong role surprises them.

**Why it happens:**
WASAPI `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eCapture, eMultimedia)` is dynamic. The IMMNotificationClient already exists in the codebase to detect device-removed events but does not currently handle `OnDefaultDeviceChanged` (audio_capture.cpp:166-169 returns S_OK no-op).

**How to avoid:**
- Support both default-device-tracking and explicit-device-pinning modes. UI gives the user three options:
  1. Always use system default capture (eMultimedia).
  2. Always use system default communications (eCommunications).
  3. Pin a specific device by `wstrEndpointDeviceId`.
- When tracking default: handle `OnDefaultDeviceChanged` → tear down current capture session, start new one on new device, log the change. UI surfaces "audio device changed to <name>".
- When pinning: ignore `OnDefaultDeviceChanged`. Handle `OnDeviceStateChanged` for the pinned device → if disconnected, surface error to UI ("Selected mic disconnected"); on reconnection, reattach.
- Default to "pin to specific device" once the user has trained. Trained thresholds are device-specific; switching device implicitly invalidates training.

**Phase to address:**
Audio-capture-in-driver phase, with UI work in the client side (Phase 5 client-demote).

---

### Pitfall 15: Symbol bloat from `nlohmann/json` in three binaries

**What goes wrong:**
Driver, client, and `mic_test.exe` all use `nlohmann::json`. Each binary contains its own template instantiations. Driver DLL grows from ~200KB to ~600KB+ purely from json template bloat. Not a correctness issue — but startup-time, paging cost, and download size all suffer.

**Why it happens:**
nlohmann::json is header-only and template-heavy. Each TU that includes it instantiates whatever it uses. No inline deduplication across modules.

**How to avoid:**
- Centralize JSON serialization in shared lib. Each TU includes only `<micmap/json/serializers.hpp>` (declaration), and the implementation lives in a single `serializers.cpp` in the shared lib. This is the standard nlohmann advice (`from_json`/`to_json` free functions defined once).
- Don't include `<nlohmann/json.hpp>` in headers consumers will pull in. Use forward-declared interfaces and pImpl where needed.
- Acceptable tradeoff: the DLL stays small, the static lib carries the templates, builds are slightly slower because the static lib rebuilds when JSON changes — but binary size and runtime cost win.

**Warning signs:**
- Driver DLL > 1MB after a small functional change.
- Compile-time of a small change spikes.
- Linker emits LNK4006 warnings about duplicate template symbols.

**Phase to address:**
Shared-lib extraction phase.

---

### Pitfall 16: Driver-side health endpoint returns stale state under load

**What goes wrong:**
Client polls `GET /health` every second to display the connection indicator. Driver computes health by checking audio-capture status, FFT-running status, state-machine state. Under load (heavy audio thread, FFT every 10ms), the health computation either (a) blocks on a mutex held by the audio thread, slowing health response and triggering client timeout, or (b) returns a snapshot from N ms ago that's no longer accurate.

**Why it happens:**
v1.5's `/health` is trivial (returns `{"status":"healthy"}`). v1.6 makes it semantic — must include audio device, current state, training status. Each field has its own ownership; gathering them is N reads with N potential mutexes.

**How to avoid:**
- Each subsystem (audio, detection, state machine) maintains a `std::atomic<HealthSnapshot>` (compact struct) that it updates on state change. `/health` handler reads each atomic snapshot, marshals to JSON, returns. No mutex acquisition in the handler.
- For non-trivial fields (e.g., current device name as wstring), use a `shared_mutex` with reads-only-not-blocking-on-readers. Or: precompute the JSON string on state change and cache.
- Add a stale-read guard: each snapshot has a `lastUpdated` timestamp; client compares against its own clock and warns "driver state is older than 30s" instead of "driver disconnected."

**Phase to address:**
IPC reshape phase.

---

### Pitfall 17: WASAPI exclusive-mode lock-out

**What goes wrong:**
Some users have audio software (DAWs, broadcasting tools, OBS) that opens microphones in exclusive mode (`AUDCLNT_SHAREMODE_EXCLUSIVE`). MicMap's shared-mode capture conflicts: `IAudioClient::Initialize` returns `AUDCLNT_E_DEVICE_IN_USE`. User experience: "MicMap doesn't work when OBS is running."

**Why it happens:**
WASAPI exclusive mode is a hardware-level lock. Whoever wins, wins; others get errors.

**How to avoid:**
- MicMap always opens in `AUDCLNT_SHAREMODE_SHARED` (already the case). When exclusive lock is held by another app, retry every 2 seconds, surface "Mic in use by another app" to the client UI.
- Don't try to take exclusive mode ourselves — would harm other apps.

**Phase to address:**
Audio-capture-in-driver phase. Document the limitation in README.

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Call `UpdateBooleanComponent` from audio thread because it's "in the same process now" | Removes a CommandQueue hop | Crash on HMD deactivation, deadlock on Cleanup, undefined OpenVR threading guarantees (Pitfall 3) | Never |
| `CoInitializeEx(MTA)` from `DeviceProvider::Init` to pre-init COM for the audio thread | Saves a few lines in audio worker | Apartment incompatibility with vrserver thread, RPC_E_CHANGED_MODE (Pitfall 1) | Never |
| `#ifdef MICMAP_DRIVER_BUILD` in shared-lib code to handle "small differences" | Quick fix | Drift between `mic_test` and driver, Pitfall 9 | Never (use dependency injection) |
| Driver file-watches `config.json` for client edits | Decoupled IPC | File-share races, watcher thread to manage in Cleanup, Pitfall 5 | Never (use HTTP push for change events) |
| Single `nlohmann::json` include in every header for convenience | Cleaner code | Symbol bloat × 3 binaries, link warnings, Pitfall 15 | Acceptable in implementation .cpp files; never in shared headers |
| Skip `IMMNotificationClient` `Unregister` on shutdown (rely on COM Release) | Simpler code | Use-after-free on shutdown when device removed (Pitfall 13) | Never |
| Static `Logger::instance()` singleton in shared lib | Familiar pattern | Two instances when linked into two binaries, Pitfall 8 | Never (inject sink at construction) |
| Use `SHGetFolderPathW` (deprecated) for AppData lookup | Already in v1.5 codebase | `SHGetKnownFolderPath` is the supported API; service-context resolution differs | Acceptable as v1.5 carryover; migrate when touching the path |
| Validation by clamping without first range-checking | One pass through fields | Loses original value silently, Pitfall 7.3 | Never; schema → range-check (with WARN) → clamp |
| Shared `micmap.log` between client and driver | One log to read | Interleaved writes corrupt output (Pitfall 8) | Never; use separate files |

---

## Integration Gotchas

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| WASAPI inside SteamVR driver process | Calling `CoInitializeEx` on the calling/Init thread | Spawn a dedicated audio worker thread; init COM on that thread; handle `RPC_E_CHANGED_MODE` distinctly (Pitfall 1) |
| OpenVR API + WASAPI thread | `UpdateBooleanComponent` from audio callback | All OpenVR calls from `RunFrame` only; audio thread enqueues to CommandQueue (Pitfall 3) |
| OpenVR driver `Init/Cleanup` cycle + audio session | Audio thread joined after WASAPI handles released | Reverse-order teardown: stop capture → join thread → release COM → Cleanup OpenVR (Pitfall 4) |
| `IMMNotificationClient` callback | Dereferencing driver state after `Cleanup` | Wrap state in `shared_ptr<State>` with `atomic<bool> alive`; callback checks alive (Pitfall 13) |
| `OnDefaultDeviceChanged` no-op | Default device changes silently; capture uses wrong mic | Either tear-down + restart on new default, or pin to explicit device (Pitfall 14) |
| Client→driver settings push | Partial JSON updates | Push full `AppConfig`; validate schema → range → commit (Pitfall 7) |
| Two-process config ownership | File-watcher in driver | HTTP `POST /config` from client; driver doesn't watch disk (Pitfall 5) |
| Shared static lib + driver DLL | `__declspec(dllexport)` annotations in shared lib | Static lib has zero dllexport; only DLL's HmdDriverFactory is exported (Pitfall 6) |
| `mic_test.exe` parallel build | `#ifdef MICMAP_DRIVER_BUILD` in shared lib | Inject differences via constructor params / interfaces (Pitfall 9) |
| Driver log path resolution | `SHGetKnownFolderPath` from a service-spawned token | Defer log-file open to `Init()`; cache resolved path; fall back to env var (Pitfall 8) |
| Mid-migration intermediate states | Phased commits that don't build standalone | Each phase commits a buildable, runnable state; transient feature flag deleted by milestone close (Pitfall 10) |

---

## Performance Traps

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Audio thread → OpenVR direct call | Crash on HMD sleep cycle (Pitfall 3) | All OpenVR calls via CommandQueue from RunFrame | Any user who sleeps the headset |
| FFT-on-every-frame at 2048 on a slow CPU | Compositor frame drops | Profile + decimate; or smaller FFT (e.g. 1024) | Older Ryzen / Atom-class CPUs |
| Per-frame `UpdateBooleanComponent` even when state unchanged | Log spam, marginal CPU | v1.5 already deduplicates via `lastWrittenValue_` (`device_provider.cpp:220`) — preserve in v1.6 | Always under sustained false positives |
| Health endpoint blocking on subsystem mutexes | Client UI lag, "disconnected" false positives | Atomic snapshots updated by subsystems; lockless reads in handler (Pitfall 16) | High-load scenarios with many subsystems |
| Symbol bloat from `nlohmann::json` headers | DLL > 1MB without justification | Centralize serializers.cpp; forward-declare in headers (Pitfall 15) | Always — silent until you measure |
| CommandQueue overflow under audio-driven trigger spam | Triggers silently dropped at depth 8 | Rate-limit at producer (state machine, not raw FFT) | False-positive storms; environments with constant background noise |

---

## Security Mistakes

| Mistake | Risk | Prevention |
|---------|------|------------|
| Driver HTTP server binds `0.0.0.0` | LAN/WAN can trigger settings changes | Explicit `127.0.0.1` constant; smoke-test `netstat`; v1.5 already does this — keep doing it (Pitfall 7) |
| New IPC routes without auth | Any local process can mutate driver state | Per-boot shared secret in headers (deferred this milestone; document) |
| Settings JSON not bounds-checked before use | Config-file tampering → invalid driver state | v1.5 CFG-03 clamp/pow2-snap → reuse the shared-lib helper (Pitfall 11) |
| Training data sent unauthenticated over HTTP | Local malware can inject training to make detection always-trigger or never-trigger | Deferred mitigation; localhost-only is the v1.5 stance, same v1.6 |
| Driver writes to `%APPDATA%` paths derived from system token (service spawn) | Files land in SYSTEM AppData, accessible to admin malware | Resolve `FOLDERID_RoamingAppData` once; verify path is in user profile; log if not (Pitfall 8) |

---

## UX Pitfalls

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Mic-access denial silent | "Detection just doesn't work" with no clue why | Detect zero-RMS for 10s; surface "MIC ACCESS DENIED — open Settings > Privacy > Microphone" in client UI (Pitfall 2) |
| Default device changes silently mid-session | Detection threshold no longer matches mic | Surface device change as toast; suggest retraining (Pitfall 14) |
| Exclusive-mode lockout (e.g., OBS open) | "MicMap broken when streaming" | Detect AUDCLNT_E_DEVICE_IN_USE; show "Mic in use by another app — close it or switch device" (Pitfall 17) |
| Settings save with no confirmation that driver applied them | "Did my change take effect?" anxiety | Driver's response to `POST /settings` is `{"applied":true,"errors":[]}`; client shows "Saved & applied" |
| Training mid-session with the wrong mic | Trained on wrong device; detection unreliable | Pin device + warn if device changes during training; refuse to start training without an explicit device pin |
| Client demoted to settings-only with no health visibility | User can't tell if driver is running | Driver-health indicator pulls `/health` every 2s; client shows green/yellow/red |

---

## "Looks Done But Isn't" Checklist

Run before declaring v1.6 done.

- [ ] **Audio capture inside driver:** Driver logs show RMS dB > -∞ continuously after first detection start; verified on a fresh Windows install where mic-access prompt was triggered. (Pitfall 2)
- [ ] **All OpenVR calls from RunFrame only:** `grep -rn 'VRDriverInput\|VRProperties\|VRServerDriverHost\|VRSettings\|VRApplications\(\)' driver/src/` returns hits in `device_provider.cpp` (RunFrame and DeviceProvider methods only) and `manifest_registrar.cpp`. Audio/detection/state-machine TUs return zero hits. (Pitfall 3)
- [ ] **Init/Cleanup stress test:** 50× Init→500ms→Cleanup loop; no GDI handle leak (Process Explorer baseline same), no audio device handle held after Cleanup. (Pitfall 4)
- [ ] **5-cycle SteamVR restart UAT:** Identical behavior to v1.5 plus "audio capture works in cycle 5 like cycle 1." (Pitfall 4)
- [ ] **Mic permission UX:** Fresh-install user, auto-launched at login, gets a visible cue (tray balloon or client-UI message) within 30s of zero-audio. (Pitfall 2)
- [ ] **Default device change handled:** Test by switching default mic in Sound settings while MicMap is running; either driver follows (with toast) or driver pins (with toast on disconnect). (Pitfall 14)
- [ ] **Settings IPC round-trip:** Client posts settings; driver applies and persists; client restarts; driver state matches. (Pitfall 7)
- [ ] **Training data IPC round-trip:** Client streams 150 samples; driver finalizes and persists; restart driver; trained pattern survives. (Pitfall 7)
- [ ] **Single nlohmann instantiation per binary:** `dumpbin /symbols driver_micmap.dll | findstr nlohmann` shows centralized serializer references, not bloat. (Pitfall 15)
- [ ] **No `MICMAP_DRIVER_BUILD` in shared lib:** `grep -rn 'MICMAP_DRIVER_BUILD' src/` returns zero hits inside any shared-lib TU. (Pitfall 9)
- [ ] **Driver DLL exports only HmdDriverFactory:** `dumpbin /exports driver_micmap.dll` shows exactly one export. (Pitfall 6)
- [ ] **Localhost-only HTTP confirmed for ALL driver routes:** `netstat -an | findstr <port>` shows `127.0.0.1`, not `0.0.0.0`, for every endpoint. (Pitfall 7)
- [ ] **`mic_test.exe` exercises the full pipeline:** Detection → trigger callback (no OpenVR) reproduces driver behavior on identical input. (Pitfall 9)
- [ ] **Mid-milestone build cleanliness:** Each phase tag (e.g., `v1.6-phase-2-end`) builds, runs, and passes a representative subset of UAT. (Pitfall 10)
- [ ] **v1.5 priors preserved:** `VR_Init` reentry guard, `IsApplicationInstalled` poll, `vrpathreg removedriver`-before-`adddriver`, `ReplaceFileW` atomic save, `AcknowledgeQuit_Exiting` ordered shutdown — all still in place. (Pitfall 11)
- [ ] **Driver log lands in correct AppData:** Path resolves to interactive user's `%APPDATA%`, not SYSTEM profile, even when auto-launched at login. (Pitfall 8)

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| COM apartment incompatibility (P1) | LOW if caught in dev; MEDIUM if shipped | Patch: move COM init to dedicated audio thread; users rerun installer (no data loss) |
| Mic permission denied at auto-launch (P2) | LOW (user-fixable) | Document the fix in README; add detect-and-surface logic in a patch |
| Audio thread → OpenVR direct call (P3) | HIGH if shipped (random crashes for users) | Patch: introduce CommandQueue path; tell users "restart SteamVR" as interim |
| Lifecycle leak on Cleanup (P4) | MEDIUM | Patch: tighten teardown order; users restart SteamVR to recover |
| Config race (P5) | LOW | Patch: switch to HTTP push; users see no behavior change once patched |
| Symbol-visibility issue (P6) | LOW (build-time fail) | Catch at CI; never ships |
| Dead trigger code paths (P7) | LOW | Patch removes residue; no user-visible impact |
| Logger misroute (P8) | LOW | Patch: defer log-open to `Init()`; logs reappear after upgrade |
| `mic_test`/driver drift (P9) | MEDIUM (silent regressions) | Add canary; backfill missed bugs |
| Migration boundary states (P10) | MEDIUM (dev-team friction) | Add CMake checks; phase-end UAT |
| v1.5 prior recurrence (P11) | LOW-HIGH depending on which | Cite v1.5 PITFALLS.md in PR review; reuse v1.5 helpers |
| RunFrame budget breach (P12) | LOW | Profile + reduce; users see compositor judder until patch |
| IMMNotificationClient UAF (P13) | HIGH if shipped | Patch wraps in shared_ptr<State> with alive flag |
| Default device drift (P14) | LOW | Patch: support pin-mode; users re-pin |
| Symbol bloat (P15) | LOW | Patch: centralize serializers; binary shrinks |
| Health endpoint blocking (P16) | LOW | Patch: atomic snapshots; users see lower latency |
| Exclusive-mode lockout (P17) | LOW (documented limitation) | README note + UI surface; user closes the conflicting app |

---

## Pitfall-to-Phase Mapping

Phase names below are placeholders matching the milestone sketch in PROJECT.md ("relocate audio/detection/state-machine/config/trigger from client to driver; extract shared library; client demoted to settings/health UI"). Roadmapper will rename to actual phase IDs.

| # | Pitfall | Prevention Phase | Verification |
|---|---------|------------------|--------------|
| 1 | COM apartment incompatibility | Shared-lib extract / WASAPI lift | Driver log shows audio worker thread COM init succeeded; RPC_E_CHANGED_MODE handled distinctly |
| 2 | Mic permission denial at auto-launch | WASAPI lift | Fresh-install + auto-launch UAT; zero-RMS detection triggers user-facing notification |
| 3 | Audio thread → OpenVR direct call | Detection-in-driver / trigger-in-driver | `grep` lint check; HMD sleep/wake stress test |
| 4 | Driver lifecycle vs WASAPI lifecycle | WASAPI lift | 50× Init/Cleanup stress test; no leaked handles |
| 5 | Config race | IPC reshape | HTTP push for change events; no file-watcher in driver |
| 6 | Static-lib symbol pollution | Shared-lib extract | `dumpbin /exports` shows only HmdDriverFactory; CMake STATIC enforcement |
| 7 | IPC reshape — dead routes + binding | IPC reshape | Forbidden-string sweep; netstat smoke check; settings round-trip test |
| 8 | Driver log path / dual-writer | Logger setup (within shared-lib extract) | Path resolves to user profile under all launch modes; separate driver.log file |
| 9 | `mic_test` drift | Shared-lib extract + every later phase | No `MICMAP_DRIVER_BUILD` in shared lib; canary at CI |
| 10 | Migration boundary states | All phases | Phase-tag build/UAT discipline; runtime feature flag deleted by close |
| 11 | v1.5 priors recurring | All phases | v1.5 PITFALLS.md cited in plans; reuse v1.5 helpers (atomicReplaceFile, manifest registrar with poll guard, AcknowledgeQuit_Exiting ordering) |
| 12 | RunFrame budget breach | Detection-in-driver / trigger-in-driver | `RunFrame < 1ms` dev-build assert maintained; CommandQueue rate at state machine, not FFT |
| 13 | `IMMNotificationClient` UAF | WASAPI lift | shared_ptr<State> + alive flag pattern; shutdown-during-unplug stress test |
| 14 | Default device drift | WASAPI lift + client UI demote | `OnDefaultDeviceChanged` handled; UI offers pin mode |
| 15 | nlohmann symbol bloat | Shared-lib extract | Single `serializers.cpp`; binary size budget asserted in CI |
| 16 | Health endpoint stale/blocking | IPC reshape | Atomic snapshots; lockless read path |
| 17 | WASAPI exclusive-mode lockout | WASAPI lift | Test with OBS open; UI surfaces clear error |

---

## Phase-Ordering Implications

Based on pitfall dependencies:

1. **Shared-lib extraction (Phase 1)** — first. Establishes the rules (STATIC, no dllexport, no static singletons, dependency-injected sinks, no `MICMAP_DRIVER_BUILD`). Makes Pitfalls 6, 9, 15 cheap to enforce. Also relocates the v1.5 helpers (atomic save, JSON serialize/parse) into the shared lib so Pitfall 11 is automatic.

2. **WASAPI lift into driver (Phase 2)** — second. Pitfalls 1, 4, 13, 14, 17 all live here. This is the riskiest phase technically because vrserver-as-audio-app is novel territory. Budget a research spike if the team hasn't done WASAPI-in-DLL before.

3. **Detection + state machine + trigger lift (Phase 3)** — third. Pitfalls 3, 12 live here. Threading discipline (CommandQueue producer rules) is the load-bearing requirement. Once this lands, the driver runs end-to-end without the client.

4. **IPC reshape (Phase 4)** — fourth. Pitfalls 5, 7, 16 live here. Client demoted to settings + health. Settings push, training-sample stream, health pull all formalized.

5. **Mic-permission UX + client UI demote (Phase 5)** — fifth. Pitfalls 2, 14 surface here from the user side. README updates, finish-page guidance, in-app permission detection.

6. **Documentation + DOC carryover (Phase 6)** — last. Per v1.5 carryover from MILESTONES.md.

**Phases that should have a research-spike flag:**
- Phase 2 (WASAPI lift) — the COM-apartment-in-driver-DLL question is the biggest unknown. Budget a half-day spike before plan-out.
- Phase 4 (IPC reshape) — settings-vs-training stream semantics are not obviously decided; spike to nail down the contract.

**Phases that are mechanical:**
- Phase 1 (shared-lib extract) — boundary-setting work; no novel behavior.
- Phase 6 (docs) — writing.

---

## Sources

### Primary (HIGH confidence, prior art and shipped artifacts)
- `.planning/research/PITFALLS.md` (v1.5 catalog) — 17 pitfalls with phase mapping; lessons paid for in commits (e.g., `3187fbb` double `VR_Init`, `73681c5` startup hang)
- `.planning/PROJECT.md` (v1.6 milestone scope, Key Decisions table)
- `.planning/MILESTONES.md` (v1.5 closeout summary, deferred items)
- `.planning/STATE.md` (open architectural questions for v1.6)
- `.planning/codebase/CONCERNS.md` (existing tech debt, COM lifecycle audit, fragile audio device disconnect handling)
- `.planning/codebase/ARCHITECTURE.md` (current two-process model)
- `driver/src/device_provider.cpp` (v1.5-shipped reference: HMD reactivation handling, lastWrittenValue dedup, driverLogSink shape)
- `driver/src/command_queue.hpp` (v1.5-shipped HTTP→RunFrame boundary; the pattern v1.6 must extend to audio)
- `driver/src/http_server.cpp` (v1.5-shipped POST /button comment: "Never touches OpenVR API"; the rule v1.6 must enforce on more producers)
- `src/audio/src/audio_capture.cpp:116-179` (existing IMMNotificationClient with manual reference counting; the use-after-free risk in Pitfall 13)
- `src/audio/src/audio_capture.cpp:196` (existing `CoInitializeEx(MTA)` call; Pitfall 1 starting point)
- `src/core/src/config_manager.cpp:25-34` (existing `SHGetFolderPathW`-based AppData resolution; Pitfall 8 upgrade target)

### Primary (HIGH confidence, sister project and external)
- `C:\Users\decid\Documents\projects\bey-closer-t1\HMD Button Stub.md` — duplicate-component-on-HMD technique (already absorbed in v1.5; baseline for cross-driver permission shape)
- OpenVR SDK v2.5.1 `openvr_driver.h` — `IServerTrackedDeviceProvider`, `IVRDriverInput`, `IVRServerDriverHost::PollNextEvent`
- Microsoft WASAPI documentation — `IMMDeviceEnumerator`, `IMMNotificationClient`, `IAudioClient`, `AUDCLNT_E_DEVICE_IN_USE`, `AUDCLNT_SHAREMODE_SHARED`/`EXCLUSIVE`
- Microsoft COM Apartments documentation — `RPC_E_CHANGED_MODE`, MTA vs STA, threading model

### Secondary (MEDIUM confidence, issue trackers and community)
- OpenVR issue #1378 (`SetApplicationAutoLaunch` propagation delay) — v1.5 absorbed; v1.6 carry-forward
- OpenVR issue #1425 (SteamVR relaunch loop) — v1.5 absorbed via `AcknowledgeQuit_Exiting`-first ordering
- OpenVR issue #1597 (custom driver crash) — v1.5 absorbed via SVR-04 rip-out discipline; v1.6 risk in audio→OpenVR direct call (Pitfall 3)
- Microsoft Learn: "Audio Session Volume" — exclusive vs shared mode behavior (Pitfall 17)
- Windows 11 microphone privacy framework — application identity gating (Pitfall 2)

### Tertiary (LOW confidence)
- General Stack Overflow / community discussions on COM apartment in DLLs — confirms `RPC_E_CHANGED_MODE` should not be treated as fatal but should be handled distinctly (Pitfall 1)
- Anecdotal reports on audio thread → OpenVR direct calls causing crashes — supports Pitfall 3 risk profile but no canonical reproduction

---

*Pitfalls research for: MicMap v1.6 Feature Migration milestone*
*Researched: 2026-04-30*
