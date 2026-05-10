/**
 * @file process_check.hpp
 * @brief Phase 10 / FAIL-02 vs FAIL-03 disambiguation helper (Pitfall 6).
 *
 * isProcessRunning(L"vrserver.exe") via CreateToolhelp32Snapshot with a 2-second
 * result cache. Cost: microseconds (cached) / sub-millisecond (fresh). Safe to
 * call from the UI thread at the 1Hz health-poll cadence; the cache amortizes
 * to one snapshot every 2 seconds while driver is unreachable.
 *
 * NEVER use tasklist.exe (50-200ms wall-clock spawn cost; UI freeze risk).
 */
#pragma once

#ifdef _WIN32
namespace micmap::client {
bool isProcessRunning(const wchar_t* exeName);
} // namespace
#endif
