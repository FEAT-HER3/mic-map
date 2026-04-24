# Phase 4: Installer - Pattern Map

**Mapped:** 2026-04-23
**Files analyzed:** 11 new + 6 modified + 4 deleted = 21 files
**Analogs found:** 15 / 17 (2 files have no in-repo analog; external reference called out)

## File Classification

| New / Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---------------------|------|-----------|----------------|---------------|
| NEW `installer/MicMap.iss` | installer-script (Inno Setup Pascal) | orchestration / external-process | `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` (**external, not in repo**) | external-only (no in-repo analog) |
| NEW `installer/micmap.ico` | binary asset (icon) | static file | `apps/micmap/micmap.rc` (references custom-icon slot, currently uses system) | asset-slot precedent |
| NEW `src/bindings/CMakeLists.txt` | build config (static lib) | build-time | `src/steamvr/CMakeLists.txt` (STATIC lib, PUBLIC include + alias) | exact (same lib shape, also consumed by driver + app) |
| NEW `src/bindings/include/micmap/bindings/bindings_patcher.hpp` | service interface | file-I/O / transform | `driver/src/bindings_patcher.hpp` (direct lift source) | exact (verbatim lift, minus DriverLog) |
| NEW `src/bindings/src/bindings_patcher.cpp` | service impl | file-I/O / transform | `driver/src/bindings_patcher.cpp` (direct lift source) | exact (verbatim lift with logger injection) |
| NEW `tests/installer/CMakeLists.txt` | test build config | build-time | `tests/CMakeLists.txt` lines 31-35 (test_config_manager shape) | role-match (inlined in tests/ — see Shared Patterns) |
| NEW `tests/test_bindings_patcher.cpp` | unit test | file-I/O test | `tests/test_config_manager.cpp` (filesystem-tmp scenarios, MM_CHECK macro) | exact (identical test style) |
| MODIFIED `CMakeLists.txt` (root) | build orchestration | build-time | `CMakeLists.txt:113` `add_custom_target(copy_distributable_files …)` — template for new `package` target | exact (same `add_custom_target` shape) |
| MODIFIED `driver/CMakeLists.txt` | build config | build-time | self (remove `src/bindings_patcher.cpp` from line 27, add `target_link_libraries(driver_micmap PRIVATE micmap::bindings)`) | self-edit |
| MODIFIED `apps/micmap/CMakeLists.txt` | build config | build-time | self (add `micmap::bindings` to `target_link_libraries(micmap PRIVATE …)` at line 39, add `install(FILES ${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest DESTINATION bin)`) | self-edit |
| MODIFIED `apps/micmap/main.cpp` | controller (WinMain CLI fork) | request-response / CLI | self at lines 696-730 (`--register-vrmanifest` handler) | exact (sibling CLI mode in same fork block) |
| MODIFIED `src/common/include/micmap/common/cli_flags.hpp` | data struct | pure | self (add `bool patchBindings` + `bool unpatchBindings` fields) | self-edit |
| MODIFIED `src/common/src/cli_flags.cpp` | parser | pure | self at lines 24-34 (wcscmp loop) | self-edit |
| MODIFIED `tests/test_cli_flags_parse.cpp` | unit test | pure | self (add cases 7-10 for new flags) | self-edit |
| MODIFIED `driver/src/device_provider.cpp:15` | include-path fix | build-time | current `#include "bindings_patcher.hpp"` → new include path | trivial |
| DELETED `scripts/install_driver.bat` | (removed) | — | — | N/A |
| DELETED `scripts/uninstall_driver.bat` | (removed) | — | — | N/A |
| DELETED `scripts/install_driver_test.bat` | (removed) | — | — | N/A |
| DELETED `scripts/test_driver.bat` | (removed) | — | — | N/A |
| MODIFIED `CMakeLists.txt:104-140` | build cleanup | build-time | self — delete `file(GLOB SCRIPT_FILES …)` loop + `add_custom_target(copy_distributable_files …)` block once scripts/ is gone | self-delete |

## Pattern Assignments

### NEW `src/bindings/include/micmap/bindings/bindings_patcher.hpp` (service interface, file-I/O / transform)

**Analog:** `driver/src/bindings_patcher.hpp` (verbatim source for the D-10 lift).

**Current public surface (to PRESERVE after lift):**

The driver-side header (`driver/src/bindings_patcher.hpp:23-35`) declares ONE public function today:

```cpp
namespace micmap::driver {
bool PatchGenericHmdBindings();
} // namespace micmap::driver
```

**But the .cpp additionally exposes three implementation functions** (at file scope inside `namespace micmap::driver`, not in the anonymous `namespace`, so they are externally linkable even though no header declares them):

| Function | Signature (from driver/src/bindings_patcher.cpp) | Purpose |
|----------|--------------------------------------------------|---------|
| `AtomicWriteJson` | `bool AtomicWriteJson(const fs::path& target, const json& j)` — cpp:146 | tmp-then-rename atomic write |
| `PatchGenericHmdBindingsFile` | `bool PatchGenericHmdBindingsFile(const fs::path& configDir)` — cpp:171 | in-place patch of one file |
| `EnsureControllerTypeFiles` | `bool EnsureControllerTypeFiles(const fs::path& configDir, const std::string& controllerType)` — cpp:290 | write controller-type bindings + profile |

Plus the `static` (anonymous-namespace) helper `ResolveSteamVrConfigDir()` at cpp:29 that the app side now also needs (D-12).

**Planner task for the lift header:**

1. Move to namespace `micmap::bindings` (neutral — no `::driver` suffix).
2. Promote `PatchGenericHmdBindingsFile`, `EnsureControllerTypeFiles`, `AtomicWriteJson`, `ResolveSteamVrConfigDir` to the public header so `micmap.exe --patch-bindings` can call them.
3. Add `UnpatchGenericHmdBindingsFile(configDir)` — new per D-11 (restore from `.micmap_backup` if present, else `j.erase(kMarkerKey)` in place + atomic-write; log+return true if file missing or marker absent).
4. Expose the `kMarkerKey` / `kMarkerKeyV1` string constants in the header so `UnpatchGenericHmdBindingsFile` and future tests can reference them.
5. **Logger injection shape** — planner discretion (D-10). Three defensible options:
   - `using LogFn = std::function<void(const char*)>;` passed to each function (thread-safety-free, test-friendly).
   - Injected interface: `class IBindingsLogger { virtual void log(std::string_view) = 0; };` (matches `IVRApplicationsSurface` precedent at `src/steamvr/include/micmap/steamvr/manifest_registrar.hpp:57-72`).
   - Module-level sink `void SetLogger(std::function<void(const char*)>)` invoked once at lib init.

**Include-style conventions to match (mirror src/steamvr and src/common):**

```cpp
// Header guard style — micmap::steamvr uses #pragma once, micmap::common uses
// #ifndef MICMAP_COMMON_CLI_FLAGS_HPP. Either is fine. Match sibling lib you pick.
#pragma once  // matches bindings_patcher.hpp line 21 and steamvr manifest_registrar.hpp line 1

#include <filesystem>  // fs::path in public signature
#include <nlohmann/json.hpp>  // json in AtomicWriteJson signature
#include <functional>  // if LogFn is std::function
#include <string>

namespace micmap::bindings { /* … */ }
```

---

### NEW `src/bindings/src/bindings_patcher.cpp` (service impl, file-I/O / transform)

**Analog:** `driver/src/bindings_patcher.cpp` (verbatim lift).

**DriverLog → injected logger replacement pattern:**

Every `DriverLog("MicMap[patch]: …\n", …)` call-site (cpp:33, 47, 57, 63, 155, 162, 175, 185, 191, 201, 203, 211, 301, 321, 326, 329, 337, 354, 360, 363, 377) must be replaced with a call through the injected sink. Keep the exact format string + trailing `\n` (so driver-side DriverLog output is byte-identical after the lift — zero behavior change).

**Shape hint (std::function variant):**

```cpp
// In the .cpp, near file top:
namespace {
LogFn g_log = [](const char* msg){ (void)msg; };  // no-op default
}
void SetLogger(LogFn fn) { g_log = std::move(fn); }

// Replace DriverLog calls with a varargs formatter that feeds g_log:
static void LogFmt(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    g_log(buffer);
}
// Then s/DriverLog/LogFmt/g.
```

**Driver-side adapter (driver/src/driver_main.cpp or device_provider.cpp):**

```cpp
// Before calling any bindings fn:
micmap::bindings::SetLogger([](const char* msg) { DriverLog("%s", msg); });
```

**App-side adapter (apps/micmap/main.cpp CLI fork):**

```cpp
// Same SetLogger call but route to the common Logger:
micmap::bindings::SetLogger([](const char* msg) {
    MICMAP_LOG_INFO(msg);
});
```

**Core patterns to copy verbatim:**

| Pattern | Source (`driver/src/bindings_patcher.cpp`) | Notes |
|---------|--------------------------------------------|-------|
| `ResolveSteamVrConfigDir()` — `%LOCALAPPDATA%\openvr\openvrpaths.vrpath` parse | cpp:29-66 | Preserve `_WIN32` guard; app side only uses Windows branch |
| Marker-key constants | cpp:23-24 (`kMarkerKey = "micmap_patched_v2"`, `kMarkerKeyV1 = "micmap_patched_v1"`) | Do NOT bump to v3 — installer and driver share ownership of v2 |
| `AtomicWriteJson` tmp-then-rename | cpp:146-168 | Error cleanup path (line 157, 164): `fs::remove(tmp, ec)` after failure |
| Write-once backup (cpp:198 — AUTHORITATIVE per D-08) | cpp:196-206 | Comment must cite line 198 as source-of-truth |
| `AlreadyPatched` / `OwnedByLegacyMicmap` marker checks | cpp:68-79 | Used by both patch (idempotency) and unpatch (ownership) paths |

**New `UnpatchGenericHmdBindingsFile` (per D-11) follows this skeleton (mirror of `PatchGenericHmdBindingsFile` at cpp:171-213):**

```cpp
bool UnpatchGenericHmdBindingsFile(const fs::path& configDir) {
    fs::path target = configDir / "vrcompositor_bindings_generic_hmd.json";
    std::error_code ec;
    if (!fs::exists(target, ec)) {
        LogFmt("MicMap[unpatch]: target missing at %s (nothing to do)\n",
               target.string().c_str());
        return true;  // D-11: skip + return 0
    }
    fs::path backup = target; backup += ".micmap_backup";
    if (fs::exists(backup, ec)) {
        // Restore from backup (D-11 primary path)
        fs::copy_file(backup, target, fs::copy_options::overwrite_existing, ec);
        if (ec) return false;
        // Do NOT remove the backup — write-once semantics (D-08).
        LogFmt("MicMap[unpatch]: restored from %s\n", backup.string().c_str());
        return true;
    }
    // Fallback: marker-erase in place (D-11 secondary path)
    json j;
    try { std::ifstream in(target); in >> j; }
    catch (...) { return false; }
    if (!AlreadyPatched(j) && !OwnedByLegacyMicmap(j)) {
        LogFmt("MicMap[unpatch]: marker absent — leaving file alone\n");
        return true;  // D-11: skip + return 0
    }
    j.erase(kMarkerKey);
    j.erase(kMarkerKeyV1);
    return AtomicWriteJson(target, j);
}
```

---

### NEW `src/bindings/CMakeLists.txt` (build config, static lib)

**Analog:** `src/steamvr/CMakeLists.txt` (same lib shape: static, PUBLIC include dir, alias, consumed by multiple targets).

**Excerpt pattern (from `src/steamvr/CMakeLists.txt:12-39`):**

```cmake
add_library(micmap_steamvr STATIC
    src/vr_input.cpp
    src/vr_input_events.cpp
    src/manifest_registrar.cpp
)

target_include_directories(micmap_steamvr
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(micmap_steamvr
    PUBLIC
        micmap_common
    PRIVATE
        httplib::httplib
)

target_compile_features(micmap_steamvr PUBLIC cxx_std_17)

# Alias pattern (line 67):
add_library(micmap::steamvr ALIAS micmap_steamvr)
```

**Adapt for bindings:**

- Source: `src/bindings_patcher.cpp` only.
- Public include dir: `${CMAKE_CURRENT_SOURCE_DIR}/include`.
- `target_link_libraries PUBLIC nlohmann_json` (since `bindings_patcher.hpp` includes `<nlohmann/json.hpp>` in its public surface — or make it PRIVATE if you move the json include into the `.cpp` and expose only `fs::path` in the header).
- Alias `micmap::bindings`.
- **Register the new subdirectory in `src/CMakeLists.txt`** — add `add_subdirectory(bindings)` alongside lines 4-8.
- **Add to `micmap_lib` INTERFACE** at `src/CMakeLists.txt:11-18` so app transitively gets the lib.

---

### MODIFIED `CMakeLists.txt` (root — new `package` target + install rules)

**Analog:** `CMakeLists.txt:113-140` (`add_custom_target(copy_distributable_files ALL …)`) — exact structural template for the new `package` target.

**Current `copy_distributable_files` shape (to be DELETED per D-18):**

```cmake
# CMakeLists.txt:113-135
add_custom_target(copy_distributable_files ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/driver"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/scripts/install_driver.bat"
        "${CMAKE_BINARY_DIR}/bin/install_driver.bat"
    …
    COMMENT "Copying distributable files (scripts and driver) to build/bin directory"
)

# CMakeLists.txt:137-140
if(MICMAP_BUILD_DRIVER AND OpenVR_FOUND)
    add_dependencies(copy_distributable_files driver_micmap)
endif()
```

**Replace with `package` target (D-18 / D-19 / D-20 / D-21) following the same shape but NO `ALL` (opt-in build):**

```cmake
find_program(ISCC_EXECUTABLE ISCC
    PATHS
        "$ENV{ProgramFiles\(x86\)}/Inno Setup 6"
        "$ENV{ProgramFiles}/Inno Setup 6"
    DOC "Inno Setup 6 command-line compiler (ISCC.exe)"
)
if(NOT ISCC_EXECUTABLE)
    message(WARNING "ISCC.exe not found — 'package' target will not be available. "
                    "Install Inno Setup 6.7.1 from https://jrsoftware.org/isdl.php")
else()
    add_custom_target(package
        # Stage via install(DESTINATION …) rules — single source of truth (D-19)
        COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                                 --prefix "${CMAKE_BINARY_DIR}/stage"
                                 --config $<CONFIG>
        # Invoke ISCC with /D defines (D-21)
        COMMAND ${ISCC_EXECUTABLE}
                /DMICMAP_VERSION=${PROJECT_VERSION}
                /DSTAGE_DIR=${CMAKE_BINARY_DIR}/stage
                /DOUTPUT_DIR=${CMAKE_BINARY_DIR}/installer
                "${CMAKE_SOURCE_DIR}/installer/MicMap.iss"
        COMMENT "Building MicMap-Setup-v${PROJECT_VERSION}.exe"
        VERBATIM
    )
    # Ensure everything is built + install staged before ISCC runs
    add_dependencies(package micmap)
    if(MICMAP_BUILD_DRIVER AND OpenVR_FOUND)
        add_dependencies(package driver_micmap)
    endif()
endif()
```

**Also DELETE (per D-18):**
- Lines 104-109 (`file(GLOB SCRIPT_FILES …)` + `configure_file` loop)
- Lines 111-140 (`add_custom_target(copy_distributable_files …)` + its `add_dependencies`)

**Add (per D-19, check if missing):** `install(FILES ${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest DESTINATION bin)` in `apps/micmap/CMakeLists.txt` (see below).

---

### MODIFIED `apps/micmap/main.cpp` (controller, WinMain CLI fork)

**Analog:** Same file, lines 696-730 — the `--register-vrmanifest` / `--unregister-vrmanifest` handler IS the template.

**Verbatim excerpt (lines 696-730) — the template to mirror:**

```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR /*lpCmdLine-unused*/, int nCmdShow) {
    // Phase 3 D-01: parse CLI flags FIRST, before anything else. Use the
    // wide-char argv from CommandLineToArgvW (Pitfall 8: free it the moment
    // CliFlags is populated — no argv pointers stored).
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    micmap::common::CliFlags flags = micmap::common::parseCliArgs(argc, argvW);
    if (argvW) { LocalFree(argvW); argvW = nullptr; }
    // argvW is now INVALID; use only `flags` below.

    // D-02 / D-03 / D-04: CLI fork — headless register/unregister BEFORE
    // any GUI init (no RegisterClassExW, no CreateWindowW, no D3D, no
    // ImGui). D-04: no console allocation — the default ConsoleLogger's
    // stdout writes are dropped on the floor in headless CLI mode; exit
    // code carries the result (0 success, 1 failure).
    if (flags.registerManifest || flags.unregisterManifest) {
#ifdef MICMAP_HAS_OPENVR
        vr::EVRInitError initErr = vr::VRInitError_None;
        vr::VR_Init(&initErr, vr::VRApplication_Utility);
        if (initErr != vr::VRInitError_None) {
            MICMAP_LOG_ERROR("CLI VR_Init(Utility) failed: ",
                             vr::VR_GetVRInitErrorAsEnglishDescription(initErr));
            return 1;  // D-03
        }
        auto registrar = micmap::steamvr::createManifestRegistrar();
        micmap::steamvr::RegisterResult r = flags.registerManifest
            ? registrar->registerApp()
            : registrar->unregisterApp();
        vr::VR_Shutdown();
        return (r == micmap::steamvr::RegisterResult::Success) ? 0 : 1;
#else
        MICMAP_LOG_ERROR("OpenVR not available in this build; cannot register manifest");
        return 1;
#endif
    }
    /* … rest of WinMain … */
}
```

**Extension for --patch-bindings / --unpatch-bindings — insert as a sibling `if` block immediately AFTER the manifest block (before the `CreateMutexW` at line 732):**

```cpp
if (flags.patchBindings || flags.unpatchBindings) {
    // D-12: patcher resolves SteamVR config dir internally via openvrpaths.vrpath
    // parse (same as driver side at driver/src/bindings_patcher.cpp:29). No
    // VR_Init required — we do NOT need a live vrserver to patch/unpatch the
    // bindings file; we only need the runtime install path. Skip the VR_Init
    // dance that --register-vrmanifest performs.
    micmap::bindings::SetLogger([](const char* msg) {
        MICMAP_LOG_INFO(msg);
    });
    fs::path configDir = micmap::bindings::ResolveSteamVrConfigDir();
    if (configDir.empty()) {
        MICMAP_LOG_ERROR("Could not resolve SteamVR config dir from openvrpaths.vrpath");
        return 1;
    }
    bool ok = flags.patchBindings
        ? micmap::bindings::PatchGenericHmdBindingsFile(configDir) &&
          micmap::bindings::EnsureControllerTypeFiles(configDir, "lighthouse_hmd")
        : micmap::bindings::UnpatchGenericHmdBindingsFile(configDir);
    // Exit-code contract from Phase 3 D-03: 0 success, 1 failure.
    return ok ? 0 : 1;
}
```

**Critical ordering notes:**
1. **Insertion point:** after line 730 (end of manifest block), before line 732 (`CreateMutexW`). The single-instance mutex MUST NOT be taken in CLI mode (would block a real GUI instance that happens to be running).
2. **No VR_Init.** Phase 3 needed VR_Init(Utility) because `IVRApplications` surface is only live inside an OpenVR session. The bindings patcher reads/writes files directly — no runtime session needed. This is faster and works even if SteamVR is uninstalled.
3. **Same 0/1 exit contract** as Phase 3 D-03 (cited in the Phase 3 context for `--patch-bindings` / `--unpatch-bindings` too per RESEARCH §"Open Question 9").

---

### MODIFIED `src/common/include/micmap/common/cli_flags.hpp` (data struct)

**Analog:** self, lines 28-32. Add two fields:

```cpp
struct CliFlags {
    bool registerManifest   = false;  ///< --register-vrmanifest
    bool unregisterManifest = false;  ///< --unregister-vrmanifest
    bool minimized          = false;  ///< --minimized (silent auto-launch)
    bool patchBindings      = false;  ///< --patch-bindings (Phase 4 INST-08)
    bool unpatchBindings    = false;  ///< --unpatch-bindings (Phase 4 INST-08)
};
```

### MODIFIED `src/common/src/cli_flags.cpp` (parser)

**Analog:** self, lines 24-34. Add two `wcscmp` branches inside the existing for-loop:

```cpp
for (int i = 1; i < argc; ++i) {
    if (!argv[i]) continue;
    if (std::wcscmp(argv[i], L"--register-vrmanifest") == 0) {
        flags.registerManifest = true;
    } else if (std::wcscmp(argv[i], L"--unregister-vrmanifest") == 0) {
        flags.unregisterManifest = true;
    } else if (std::wcscmp(argv[i], L"--minimized") == 0) {
        flags.minimized = true;
    } else if (std::wcscmp(argv[i], L"--patch-bindings") == 0) {        // NEW
        flags.patchBindings = true;                                      // NEW
    } else if (std::wcscmp(argv[i], L"--unpatch-bindings") == 0) {      // NEW
        flags.unpatchBindings = true;                                    // NEW
    }
    // Unknown flags are silently ignored (D-01).
}
```

### MODIFIED `tests/test_cli_flags_parse.cpp` (unit test)

**Analog:** self, cases 1-6 (lines 36-93). Pattern: each case is a scoped block with a `const wchar_t* argv[]`, a `parseCliArgs` call, and `MM_CHECK` assertions. Add two symmetrical cases 7 and 8:

```cpp
// ---- case_7: --patch-bindings sets patchBindings only ----
{
    const wchar_t* argv[] = { L"micmap.exe", L"--patch-bindings" };
    CliFlags f = parseCliArgs(2, argv);
    MM_CHECK(f.patchBindings == true);
    MM_CHECK(f.unpatchBindings == false);
    MM_CHECK(f.registerManifest == false);
    std::cout << "PASS case_7_patch_bindings\n";
}
// ---- case_8: --unpatch-bindings sets unpatchBindings only ----
{
    const wchar_t* argv[] = { L"micmap.exe", L"--unpatch-bindings" };
    CliFlags f = parseCliArgs(2, argv);
    MM_CHECK(f.unpatchBindings == true);
    MM_CHECK(f.patchBindings == false);
    MM_CHECK(f.registerManifest == false);
    std::cout << "PASS case_8_unpatch_bindings\n";
}
```

---

### NEW `tests/test_bindings_patcher.cpp` (unit test, file-I/O test)

**Analog:** `tests/test_config_manager.cpp` (same file-I/O test shape: `fs::temp_directory_path() / "micmap_test_<name>"`, `fs::remove_all` + `fs::create_directories`, `MM_CHECK` macro, plain-main with exit 0/1).

**Core pattern (from `tests/test_config_manager.cpp:21-32`):**

```cpp
#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    namespace fs = std::filesystem;
    auto tmpDir = fs::temp_directory_path() / "micmap_test_bindings";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);
    /* … scoped test blocks … */
}
```

**Test scenarios to cover (exit-criterion for Phase 4 INST-08 in a headless, no-SteamVR environment):**

1. **Idempotency:** seed a `vrcompositor_bindings_generic_hmd.json` with Valve's original empty-sources shape → `PatchGenericHmdBindingsFile(tmpDir)` twice → second call sees `kMarkerKey == true` and is a no-op (file unchanged after first patch).
2. **Write-once backup (D-08 / cpp:198):** patch once → `.micmap_backup` exists with ORIGINAL content. Modify target, patch again → `.micmap_backup` STILL reflects the pristine original (not the modified version).
3. **Unpatch-restore path (D-11 primary):** seed + patch + then `UnpatchGenericHmdBindingsFile(tmpDir)` → target content == backup content.
4. **Unpatch marker-erase path (D-11 secondary):** seed + patch + delete `.micmap_backup` + unpatch → target no longer contains `kMarkerKey`.
5. **Unpatch no-op (D-11 skip):** target has no marker → unpatch returns true and does not touch the file.
6. **AtomicWriteJson crash safety:** write a garbage `.micmap_tmp` file → call `AtomicWriteJson(target, j)` → verify only `target` (not `.micmap_tmp`) remains on success.

**No dependency on real SteamVR runtime.** All tests work on a throwaway `tmpDir` so ctest can run on CI / devbox.

---

### NEW `tests/installer/CMakeLists.txt` OR inline in `tests/CMakeLists.txt` (planner discretion)

**Analog:** `tests/CMakeLists.txt:31-35` (test_config_manager — exact structural precedent).

**Recommendation:** Do NOT create a `tests/installer/` subdir — every existing test lives in flat `tests/*.cpp` and is registered directly in `tests/CMakeLists.txt`. Match that convention. Append to `tests/CMakeLists.txt`:

```cmake
# Phase 4 INST-08 / D-08 / D-11: bindings patcher unit tests
add_executable(test_bindings_patcher test_bindings_patcher.cpp)
target_compile_features(test_bindings_patcher PRIVATE cxx_std_17)
target_link_libraries(test_bindings_patcher PRIVATE micmap::bindings nlohmann_json)
add_test(NAME test_bindings_patcher COMMAND test_bindings_patcher)
```

(If the planner prefers a subdir, `tests/installer/CMakeLists.txt` should be `add_subdirectory(installer)`-included from the root `tests/CMakeLists.txt` — but the flat convention is preferred.)

---

### MODIFIED `driver/CMakeLists.txt` (remove bindings_patcher.cpp, link to new lib)

**Analog:** self, line 23-28 (driver source list) and line 38 (OpenVR link).

**Current sources (lines 23-28):**

```cmake
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
    src/bindings_patcher.cpp    # <-- DELETE this line (D-10)
)
```

**After lift:**

```cmake
add_library(driver_micmap SHARED
    src/driver_main.cpp
    src/device_provider.cpp
    src/http_server.cpp
)

# … existing target_link_libraries blocks …

# NEW: link to the lifted bindings shared lib
target_link_libraries(driver_micmap PRIVATE micmap::bindings)
```

**Note:** The driver already links `nlohmann_json` at lines 58-64 — `micmap::bindings` will transitively pull it too; keep both declarations explicit (matches existing paranoia patterns in this file).

---

### MODIFIED `apps/micmap/CMakeLists.txt` (link to bindings, install manifest)

**Analog:** self, lines 39-43 (existing `target_link_libraries`) and root-level `install(TARGETS micmap …)` at `CMakeLists.txt:143`.

**Edit 1 — link to new lib (after line 43):**

```cmake
target_link_libraries(micmap
    PRIVATE
        micmap_lib
        imgui
        micmap::bindings  # NEW — Phase 4 D-10 lift
)
```

**Edit 2 — ensure app.vrmanifest is installed (D-19 requires this for the stage dir):**

The `configure_file` at line 25-29 emits `${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest` and POST_BUILD-copies it to `$<TARGET_FILE_DIR:micmap>/app.vrmanifest` (lines 65-72) — but there is NO `install(FILES …)` rule for it. Add one:

```cmake
# Install app.vrmanifest alongside micmap.exe in the bin/ stage dir
# so Inno Setup's [Files] can pick it up via STAGE_DIR/bin/app.vrmanifest.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/app.vrmanifest"
        DESTINATION bin)
```

Note: The root `install(TARGETS micmap RUNTIME DESTINATION bin …)` at `CMakeLists.txt:143-147` already puts `micmap.exe` in `{stage}/bin/`. This new `install(FILES)` line puts the manifest next to it.

---

### MODIFIED `driver/src/device_provider.cpp:15` (include path fix)

**Current line 15:**
```cpp
#include "bindings_patcher.hpp"
```

**After lift (bindings moved to `src/bindings/include/micmap/bindings/bindings_patcher.hpp`):**
```cpp
#include "micmap/bindings/bindings_patcher.hpp"
```

Also update the `using namespace` / qualified calls in `device_provider.cpp` (planner must grep for `micmap::driver::PatchGenericHmdBindings` → `micmap::bindings::PatchGenericHmdBindingsFile` + `EnsureControllerTypeFiles`; exact call-site count determined during plan step).

---

### DELETED scripts

- `scripts/install_driver.bat`
- `scripts/uninstall_driver.bat`
- `scripts/install_driver_test.bat`
- `scripts/test_driver.bat`

After deletion, verify no grep hits on these filenames in:
- `README.md` (lines 23, 38 per CONTEXT.md `<canonical_refs>` — Phase 5 DOC-01 cleans these).
- `CMakeLists.txt` (lines 104-127 — deleted in this phase per D-18).
- Any CI config (none known in this repo — `.github/` not present at planning time).

---

### NEW `installer/MicMap.iss` (Inno Setup script)

**Analog status:** **NO in-repo analog.** The repo ships zero Inno Setup scripts. Nearest precedent is the sister project `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` which Research notes is **not readable from this session** (RESEARCH §Open Question 1, point 4 — "External bey-closer-t1 reference is NOT accessible from the current shell session").

**Planner action:**
1. At plan time, re-attempt `Read D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss`. If the path resolves, extract verbatim Pascal snippets for: WMI loop, registry lookup, `vrpathreg` sequence, TaskDialog invocation. **Differences to apply:** MicMap does NOT nest under another vendor's driver (D-01: `{SteamVR}\drivers\micmap\`), uses its own AppId, INCLUDES the `--patch-bindings` `[Run]` step that bey-closer-t1 did not ship.
2. If external path still inaccessible, use RESEARCH.md's verbatim snippets (all present — §1c `IsProcessRunning`, §1d `GetRunningSteamVrProcesses`, §1e `PrepareToInstall` loop, §1f `GetSteamPath`, plus `vrpathreg` sequence in later open-questions).
3. **Section skeleton (from CONTEXT.md `<code_context>` "Integration Points"):**
   - `[Setup]` preamble: AppId GUID (generate via `uuidgen` at plan time — freeze forever per INST-01), `AppName=MicMap`, `AppVersion={#MICMAP_VERSION}`, `DefaultDirName={code:GetInstallDir}`, `DisableDirPage=yes`, `ArchitecturesAllowed=x64os` (per RESEARCH §Executive Summary point 1 — `x64` is deprecated in IS 6.3+), `ArchitecturesInstallIn64BitMode=x64os`, `PrivilegesRequired=admin`, `WizardStyle=modern`, `OutputDir={#OUTPUT_DIR}`, `OutputBaseFilename=MicMap-Setup-v{#MICMAP_VERSION}`, `Uninstallable=yes`.
   - `[Files]`: source from `{#STAGE_DIR}\driver\micmap\*` → `{app}\drivers\micmap\*`; source from `{#STAGE_DIR}\bin\micmap.exe` + `app.vrmanifest` → `{app}\drivers\micmap\bin\`; `.dll` files get `restartreplace` flag (D-06 defense-in-depth).
   - `[Run]` (post-install, admin): `vrpathreg removedriver` then `adddriver {app}\drivers\micmap` (INST-03); `micmap.exe --register-vrmanifest` (INST-04); `micmap.exe --patch-bindings` (INST-08 / D-09).
   - `[UninstallRun]` (symmetric, D-09): `micmap.exe --unpatch-bindings`; `micmap.exe --unregister-vrmanifest`; `vrpathreg removedriver {app}\drivers\micmap`.
   - `[Code]`: `InitializeSetup` (D-04 no-Steam abort), `GetInstallDir` (D-01 compose), `PrepareToInstall` (D-05/D-06 WMI loop), `CurUninstallStepChanged(usUninstall)` (D-13 data-retention prompt).
   - `[Registry]`: read `HKCU\Software\Valve\Steam\SteamPath` (D-02; reads only, no writes).
4. **Icon reference:** `SetupIconFile=installer/micmap.ico` (new asset; planner exports from `apps/micmap/micmap.rc` or accepts Claude's discretion to supply a new `.ico`).

---

### NEW `installer/micmap.ico` (binary asset)

**Analog:** `apps/micmap/micmap.rc` at line 10-11 flags a custom-icon slot:
```
// If you want a custom icon, create micmap.ico and uncomment:
// IDI_MICMAP_ICON ICON "micmap.ico"
```
No `.ico` file exists in the repo today. Planner discretion per CONTEXT.md `<decisions>` "Claude's Discretion" section: reuse the icon source (if any — likely needs new generation) or supply a simple placeholder. Recommend producing ONE `.ico` and pointing both `apps/micmap/micmap.rc` (uncomment line 11) and `installer/MicMap.iss` (`SetupIconFile`) at it, so the exe and the installer share one visual identity.

---

## Shared Patterns

### Pattern A: CLI fork at WinMain entry (Phase 3 D-02 / D-03)
**Source:** `apps/micmap/main.cpp:696-730`
**Apply to:** All new CLI modes (`--patch-bindings`, `--unpatch-bindings`)
**Template:**
```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    micmap::common::CliFlags flags = micmap::common::parseCliArgs(argc, argvW);
    if (argvW) { LocalFree(argvW); argvW = nullptr; }

    // CLI mode block(s) — each returns 0 success / 1 failure.
    if (flags.registerManifest || flags.unregisterManifest) { /* … */ return ok ? 0 : 1; }
    if (flags.patchBindings   || flags.unpatchBindings)     { /* … */ return ok ? 0 : 1; }

    // Only GUI mode reaches here — CreateMutexW, RegisterClassExW, D3D init follow.
}
```

### Pattern B: 0/1 CLI exit-code contract
**Source:** Phase 3 D-03 (cited in CONTEXT.md `<canonical_refs>` Prior-phase section)
**Apply to:** All CLI modes
**Contract:** Return `0` on success, `1` on any failure. `main.cpp:725-728` is the canonical Phase-3 example (`return (r == Success) ? 0 : 1;`). Inno Setup `[Run]` inspects this via `Check:` clauses (see `--patch-bindings` error-handling in `installer/MicMap.iss`).

### Pattern C: File-I/O unit test with tmp dir + MM_CHECK
**Source:** `tests/test_config_manager.cpp:21-32`
**Apply to:** `tests/test_bindings_patcher.cpp`
**Template:**
```cpp
#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)
int main() {
    namespace fs = std::filesystem;
    auto tmpDir = fs::temp_directory_path() / "micmap_test_bindings";
    fs::remove_all(tmpDir);
    fs::create_directories(tmpDir);
    /* scoped test blocks */
    return 0;
}
```

### Pattern D: Static lib with PUBLIC include + alias
**Source:** `src/steamvr/CMakeLists.txt:12-67` and `src/common/CMakeLists.txt:4-18`
**Apply to:** `src/bindings/CMakeLists.txt`
**Template:**
```cmake
add_library(micmap_<name> STATIC src/<name>.cpp)
target_include_directories(micmap_<name> PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(micmap_<name> PUBLIC micmap_common)
target_compile_features(micmap_<name> PUBLIC cxx_std_17)
add_library(micmap::<name> ALIAS micmap_<name>)
```

### Pattern E: CLI flag extension (struct field + wcscmp branch + test case)
**Source:** `src/common/{include,src}/…/cli_flags.*` + `tests/test_cli_flags_parse.cpp`
**Apply to:** Adding any new CLI flag in future
**Contract:** Three edits required in lockstep: (1) struct field default `= false`, (2) `wcscmp` branch in `parseCliArgs`, (3) symmetric test case in `tests/test_cli_flags_parse.cpp`.

### Pattern F: Install rules as single source of truth (D-19)
**Source:** `CMakeLists.txt:143-153` + `driver/CMakeLists.txt:138-153`
**Apply to:** Anything Phase 4 installer ships that is NOT already staged via `install(…)`
**Contract:** If `cmake --install . --prefix build/stage` misses a file, FIX THE `install(…)` RULE — do NOT paper over in `installer/MicMap.iss` `[Files]`. Keeps `cmake --install` dev workflow bit-identical to the packaged installer.

### Pattern G: DriverLog → injected logger sink (D-10)
**Source:** `driver/src/driver_log.hpp:20-44` (the `DriverLog` macro) vs. `src/common/include/micmap/common/logger.hpp:123-128` (`MICMAP_LOG_*` macros)
**Apply to:** Every call site in the lifted `bindings_patcher.cpp` (21 `DriverLog` calls, grep-verifiable)
**Contract:** Replace per-TU `DriverLog` → per-lib `LogFn` sink. Driver-side adapter calls `DriverLog`; app-side adapter calls `MICMAP_LOG_INFO`. Net behavior: byte-identical log output on the driver side; new log output on the app side that routes through the common Logger.

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `installer/MicMap.iss` | installer-script | orchestration | No `.iss` exists in this repo. Planner must use external `D:\Documents\Projects\bey-closer-t1\installer\BeyondProximity.iss` (external, possibly inaccessible) or fall back to RESEARCH.md verbatim Pascal snippets. |
| `installer/micmap.ico` | binary asset | static | No `.ico` exists in repo. Planner discretion — generate one or reuse from a design asset. `apps/micmap/micmap.rc:11` has a slot ready for it. |

---

## Metadata

**Analog search scope:**
- `CMakeLists.txt` (root), `driver/CMakeLists.txt`, `apps/micmap/CMakeLists.txt`
- `src/steamvr/`, `src/common/`, `src/core/` (CMake patterns)
- `apps/micmap/main.cpp` (CLI fork pattern)
- `driver/src/bindings_patcher.{hpp,cpp}` (the lift source)
- `tests/` (all `tests/test_*.cpp` — pattern survey)
- `src/common/{include,src}/.../cli_flags.*` (flag-extension pattern)
- `apps/micmap/micmap.rc` (icon-slot precedent)
- `scripts/` (to confirm the 4 files to delete exist)

**Files scanned:** 22

**Pattern extraction date:** 2026-04-23
