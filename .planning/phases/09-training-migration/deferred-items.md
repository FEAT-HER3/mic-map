# Phase 9 Deferred Items

Pre-existing issues discovered during Phase 9 execution that are out of scope
for the current plan and should be addressed in a follow-up.

## PutSettingsRoundTrip ctest failing (exit 3)

**Discovered during:** 09-02 Task 4 (driver_api extension) regression check.

**Status:** Pre-existing — fails identically on HEAD (commit 5edc48b) BEFORE
any 09-02 Task 4 edits, and reproduces with the test executable run directly.

**Symptom:** `tests/driver/put_settings_round_trip_test.cpp` exits with code 3
(C++ exception not caught by `MM_CHECK`). Process logs show the HttpServer
starts, accepts a request, then stops cleanly — but exit code 3 indicates a
caught-by-runtime exception (likely `nlohmann::json::exception` thrown by
`nlohmann::json::parse(ss.str())` against the on-disk file when the
disk-write content has not yet flushed).

**Out of scope for 09-02:** No 09-02 file (settings_validator, http_server,
device_provider, driver_api) is modified by this test path. Test exercises
PUT /settings → saveConfigJson → on-disk readback. Plan 09-02 ships only the
training endpoints + driver_api training methods.

**Suggested fix path:** Likely needs an explicit `f.close()` between the
saveConfigJson and the readback `std::ifstream` open, or a brief sleep, or a
`std::filesystem::file_size` poll loop to let the OS flush the FILE_SHARE_*
handle. Belongs in P8 follow-up (08-04 or a P10 housekeeping plan).

## P10 carryover (added 2026-05-09 during 09-05 UAT)

### Latent: dual correlation-threshold formula in `noise_detector.cpp`

`finishTraining()` at line 167 and `setSensitivity()` at line 450 use **different** formulas to derive `correlationThreshold` from the same `sensitivity` input. Predates Phase 9 (v1.5 carryover) but surfaced during D-39(3) UAT inspection of persisted bytes.

| Site | Formula | Range |
|------|---------|-------|
| `finishTraining()` line 167 | `0.4 + (1 - sensitivity) * 0.3` | [0.40, 0.70] |
| `setSensitivity()` line 450 | `0.3 + (1 - sensitivity) * 0.5` | [0.30, 0.80] |

Result: an initial training with S=0.7 yields correlation=0.49; a subsequent recompute with the same S=0.7 yields correlation=0.45. Detection behavior shifts after the first recompute even when sensitivity is unchanged.

**Action for P10**: pick canonical form (recommend `finishTraining()`'s — matches the v1.5 default-detector instantiation), update `setSensitivity()` to match, add a unit test pinning the contract.

### Latent: orphan-timeout user-walked-away gate (D-39(5))

Implementation re-arms `lastAcceptedSample_` on every `addSample` call regardless of audio energy/content. Mic streaming continuously means the 30 s timeout fires only on capture-thread death, never on user-idle-with-mic-active. The capture-death gate is genuinely useful and should stay; the user-idle gate needs a separate mechanism.

**Action for P10**: add a 5-minute hard cap on session age regardless of sample activity, OR introduce a quality bar (above-threshold-only) inside `addSample` that determines whether the watchdog re-arms. Detector-contract change OR new `TrainingSession` field; either is small.

### Latent: client survives SteamVR restart (D-39(6))

`micmap.exe` is a SteamVR overlay app — receives WM_QUIT from OpenVR when SteamVR exits and self-terminates. SteamVR's vrmanifest auto-launch brings it back when SteamVR restarts, so the user-visible behavior is "client recycles with SteamVR" rather than "client survives and recovers." UAT spec's red-then-green driver-loaded indicator on the same client instance is therefore unobservable. The file-integrity invariant the case was really gating (training_data.bin untouched on driver kill) does pass.

**Action for P10**: optional UX upgrade — make client survive SteamVR restart and re-engage cleanly. Lower priority than the formula and timeout items.
