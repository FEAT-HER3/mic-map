---
slug: micmap-title-m-truncation
status: resolved
trigger: Title "M" truncation
created: 2026-04-23
updated: 2026-04-23
---

# Debug: micmap-title-m-truncation

## Symptoms

- **Expected behavior:** micmap.exe taskbar + titlebar show full window title "MicMap".
- **Actual behavior:** Title shows single character "M" (GetWindowTextLengthW returns 1).
- **Error messages:** None.
- **Timeline:** Pre-existing. Present in fe11e7c (`[claude] Main app mostly works`). Not a phase 1 regression.
- **Reproduction:** Launch micmap.exe → inspect titlebar / taskbar / `Get-Process micmap | Select MainWindowTitle`.

## Current Focus

- **hypothesis:** `DefWindowProc` at `apps/micmap/main.cpp:559` resolves to `DefWindowProcA` because `UNICODE` / `_UNICODE` are not defined for the `micmap` target (verified — `apps/micmap/CMakeLists.txt` has no `target_compile_definitions(... UNICODE _UNICODE)`). Window class is registered with `RegisterClassExW` and created with `CreateWindowW(L"MicMap", ...)`, so Windows delivers W-path `WM_NCCREATE`/`WM_SETTEXT` — but the handler returns through `DefWindowProcA`, which mishandles the already-wide title: the UTF-16 `L"MicMap"` (bytes `4D 00 69 00 63 00 ...`) is interpreted as narrow, stops at the first embedded NUL after `4D` ("M"), and stores a 1-char title.
- **test:** flip `DefWindowProc` to explicit `DefWindowProcW` (matches rest of file's explicit-W pattern: `CreateWindowW`, `FindWindowW`, `PostMessageW`, `SendMessageW`, etc.), rebuild, check titlebar + `GetWindowTextLengthW`.
- **expecting:** titlebar reads "MicMap", length = 6.
- **next_action:** applied — `DefWindowProc → DefWindowProcW` at line 559.
- **reasoning_checkpoint:** Single-line change matches diagnosis from prior session `micmap-white-frozen-launch.md` notes. Alternative (add `UNICODE`/`_UNICODE` target defines in CMakeLists.txt) is broader-blast-radius; explicit W-call is local + consistent with surrounding code.
- **tdd_checkpoint:**

## Evidence

- Prior session `.planning/debug/micmap-white-frozen-launch.md` notes: GetWindowTextLengthW returns 1, raw bytes `4D 00`; reproduces across clean rebuilds; present in pre-phase-1 commit fe11e7c.
- `apps/micmap/CMakeLists.txt` — no `UNICODE`/`_UNICODE` compile definitions; only `cxx_std_17` + `shell32`/`dwmapi` link libs.
- `apps/micmap/main.cpp:559` — only bare `DefWindowProc` call in file; everything else uses explicit `W` variants.

## Eliminated

- Wide literal corruption in binary — prior session confirmed `L"MicMap"` is pooled as UTF-16 in the binary; only the live window text is 1-char.
- CreateWindow A/W mismatch — file uses `CreateWindowW` explicitly.
- Phase-1 regression — symptom predates phase 1.

## Resolution

- **root_cause:** `DefWindowProc` macro resolved to `DefWindowProcA` due to absent `UNICODE`/`_UNICODE` defines on the `micmap` target. W-path `WM_NCCREATE` return through ANSI DefWindowProc truncated the UTF-16 title at the first NUL byte, storing single char "M".
- **fix:** `apps/micmap/main.cpp:559` — `DefWindowProc` → `DefWindowProcW`. Matches explicit-W pattern used throughout the file.
- **verification:** rebuild Release, launch, expect titlebar reads "MicMap" (length 6). Also fixes any other W-path messages silently degraded through the ANSI default (WM_SETTEXT future calls, WM_GETTEXT, etc.).
- **files_changed:** `apps/micmap/main.cpp` (1 line).
