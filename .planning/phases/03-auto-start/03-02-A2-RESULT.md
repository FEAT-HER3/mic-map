---
phase: 03-auto-start
plan: 02
open_item: A2
status: RESOLVED
resolved: 2026-04-23
decision: STRING form wins
---

# A2 Empirical Resolution — `arguments` field format

## Decision

**STRING form wins.** The canonical `app.vrmanifest` ships with:

```json
"arguments": "--minimized"
```

The 1-element array fallback (`["--minimized"]`) and its template variant
(`apps/micmap/app.vrmanifest.A2test_array.in`) have been deleted from the
codebase by Plan 03-02 Task 3.

## Empirical Evidence

**Test environment**

| Property | Value |
|----------|-------|
| HMD | Bigscreen Beyond |
| OS | Windows 11 Pro 10.0.26200 |
| SteamVR | latest (as of 2026-04-23) |
| Manifest path | `C:\Users\decid\Documents\projects\mic-map\build\bin\app.vrmanifest` |
| Registration method | Direct edit of `appconfig.json` `manifest_paths` (after the API-driven path failed; see pitfall below) |

**Procedure**

1. SteamVR was launched with the string-form manifest registered and
   `bigscreen.micmap` autolaunch flag set to `true` in
   `bigscreen.micmap.vrappconfig`.
2. SteamVR auto-launched `micmap.exe` within ~1 second of `vrserver` becoming
   ready (effectively immediate).
3. The launched process command line was captured via PowerShell:
   ```powershell
   Get-CimInstance Win32_Process -Filter "Name='micmap.exe'" |
       Select-Object CommandLine
   ```

**Observed command line (verbatim)**

```
"C:\Users\decid\Documents\projects\mic-map\build\bin\Release\micmap.exe" --minimized
```

SteamVR passed exactly **one** argument, `--minimized`, with no extra quoting,
no shell interpolation, no concatenation artifacts.

**Conclusion**

String-form `"arguments": "--minimized"` is the canonical SteamVR auto-launch
form. The array-form variant was never exercised against live SteamVR because
the string form succeeded on the first attempt; per the plan's decision rule
("if string form succeeds, array branch is dead"), the array template was
deleted without further testing.

## Critical Pitfall Discovered (Forward-Slash Manifest Path)

During the empirical test, an unrelated but high-impact pitfall surfaced.
**This pitfall MUST be propagated to Plan 03-04 (`manifest_registrar`) and
Plan 03-06 (WinMain CLI fork) — surfacing it now to prevent silent
auto-launch failures downstream.**

### Symptom

When the manifest path was first registered with FORWARD slashes
(`C:/Users/decid/Documents/projects/mic-map/build/bin/app.vrmanifest`),
`vrserver` logged:

```
App bigscreen.micmap Working directory C:\Users\decid\Documents\projects\mic-map\build\bin\app.vrmanifest is invalid. Skipping
```

(SteamVR re-rendered the slashes as backslashes in the log line — but
internally it was treating the **entire forward-slash path AS the working
directory**, having failed to find a `\` to split on for parent-directory
extraction.)

The manifest entry was **silently SKIPPED**. Auto-launch never fired. The
SteamVR Startup Overlay Apps UI did not list MicMap. There was no error
surface visible to the user — only the log line above, buried in
`%APPDATA%\openvr\logs\vrserver.txt`.

### Fix

Rewriting the `manifest_paths` entry in
`C:\Program Files (x86)\Steam\config\appconfig.json` with double-backslash
escapes:

```json
"manifest_paths": [
    "C:\\Users\\decid\\Documents\\projects\\mic-map\\build\\bin\\app.vrmanifest"
]
```

…caused `vrserver` to accept the manifest immediately on next startup and
auto-launch fired exactly as designed.

### Implication for Downstream Plans

**Plan 03-04 (`manifest_registrar.cpp`)**

When calling
`vr::VRApplications()->AddApplicationManifest(absPath, false)`, the absolute
path passed in **MUST use Windows-native backslashes**, NOT forward slashes.

`GetModuleFileNameW(nullptr, ...)` already returns native backslashes on
Windows, so the path computed via D-21 (`GetModuleFileNameW` +
`PathCchRemoveFileSpec` + append `L"app.vrmanifest"`) is correct **as long
as no later step substitutes `/` for `\`**.

`PathCchRemoveFileSpec` preserves backslashes; `PathCchAppendEx` /
`PathCchCombineEx` also produce backslash-canonical output. Manual `+=` of
`L"app.vrmanifest"` after `PathCchRemoveFileSpec` works because the buffer
already ends with the parent dir (no trailing slash) — but the safer
formulation is `wcscat_s(buf, L"\\app.vrmanifest")` (explicit backslash) or
`PathCchAppendEx(buf, _countof(buf), L"app.vrmanifest", PATHCCH_ALLOW_LONG_PATHS)`.

**Plan 03-06 (WinMain CLI fork)**

The WinMain entrypoint that constructs the absolute manifest path before
invoking the registrar must NOT normalize to forward slashes.
`std::filesystem::path` may canonicalize separators depending on platform
preference — if Plan 06 routes the path through `std::filesystem`, call
`.make_preferred()` to force backslashes on Windows, or convert via
`.wstring()` (which preserves native form) rather than `.string()` (which
on Windows still uses backslashes, but mixing API styles is a hazard).

**Hard rule for both plans:** the wide-char absolute path passed to
`AddApplicationManifest` (after `WideCharToMultiByte` to UTF-8) must
contain `\\` (backslash) as the path separator. A `assert` or runtime
`MICMAP_LOG_ERROR` guard in `OpenVRManifestRegistrar::registerApp` that
checks for `'/'` in the path before the API call would catch this
regression cheaply:

```cpp
if (manifestUtf8.find('/') != std::string::npos) {
    MICMAP_LOG_ERROR("manifest path contains forward slash; SteamVR will treat it as a working dir and silently skip the manifest. Path: ", manifestUtf8);
    return RegisterResult::AddFailed;
}
```

This guard is recommended (Rule 2 — missing critical functionality) for
inclusion in Plan 03-04 Task 2.

## References

- Plan 03-02 PLAN.md, Task 2 (this checkpoint)
- Plan 03-04 PLAN.md, must_haves (D-21 path resolution)
- Plan 03-06 PLAN.md, WinMain CLI fork
- `%APPDATA%\openvr\logs\vrserver.txt` — log file where the
  forward-slash error surfaces if reintroduced
- `C:\Program Files (x86)\Steam\config\appconfig.json` — `manifest_paths`
  array, written by `AddApplicationManifest`
