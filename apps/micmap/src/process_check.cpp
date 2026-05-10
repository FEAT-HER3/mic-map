#include "process_check.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>

namespace micmap::client {

namespace {
struct CacheEntry {
    std::wstring exeName;
    std::chrono::steady_clock::time_point expiresAt;
    bool result;
};

constexpr auto kCacheTtl = std::chrono::seconds(2);

std::mutex g_cacheMutex;
// Small fixed-size cache keyed by exe name; expected size 1-2 entries (typically
// just "vrserver.exe"). Linear scan is fine at this size.
CacheEntry g_cache[4];
size_t g_cacheCount = 0;
} // anonymous

bool isProcessRunning(const wchar_t* exeName) {
    if (!exeName) return false;
    const auto now = std::chrono::steady_clock::now();

    // Cache lookup. Hold the lock only while reading; copy result out before returning.
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        for (size_t i = 0; i < g_cacheCount; ++i) {
            if (g_cache[i].exeName == exeName) {
                if (now < g_cache[i].expiresAt) {
                    return g_cache[i].result;
                }
                // expired -- fall through to refresh below
                break;
            }
        }
    }

    // Fresh enumeration via CreateToolhelp32Snapshot (Pitfall 6: NEVER tasklist).
    bool found = false;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (::Process32FirstW(snap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                    found = true;
                    break;
                }
            } while (::Process32NextW(snap, &pe));
        }
        ::CloseHandle(snap);
    }

    // Cache write.
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        bool updated = false;
        for (size_t i = 0; i < g_cacheCount; ++i) {
            if (g_cache[i].exeName == exeName) {
                g_cache[i].expiresAt = now + kCacheTtl;
                g_cache[i].result = found;
                updated = true;
                break;
            }
        }
        if (!updated && g_cacheCount < (sizeof(g_cache) / sizeof(g_cache[0]))) {
            g_cache[g_cacheCount].exeName = exeName;
            g_cache[g_cacheCount].expiresAt = now + kCacheTtl;
            g_cache[g_cacheCount].result = found;
            ++g_cacheCount;
        }
        // If the small array is full and we got a new key, just don't cache --
        // sub-millisecond fresh is acceptable; cache is an optimization.
    }

    return found;
}

} // namespace micmap::client
#endif // _WIN32
