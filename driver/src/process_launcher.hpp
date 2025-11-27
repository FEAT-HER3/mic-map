/**
 * @file process_launcher.hpp
 * @brief Process management utilities for launching and terminating external applications
 *
 * Provides functionality to launch the MicMap application when SteamVR starts
 * and terminate it when SteamVR shuts down.
 */

#pragma once

#include <string>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace micmap::driver {

/**
 * @brief Handle wrapper for process management
 *
 * Encapsulates platform-specific process handle and provides RAII cleanup.
 */
class ProcessHandle {
public:
    ProcessHandle() = default;
    ~ProcessHandle();

    // Non-copyable
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;

    // Movable
    ProcessHandle(ProcessHandle&& other) noexcept;
    ProcessHandle& operator=(ProcessHandle&& other) noexcept;

    /**
     * @brief Check if the handle is valid
     * @return True if the handle represents a valid process
     */
    bool isValid() const;

    /**
     * @brief Get the native handle
     * @return Platform-specific process handle
     */
#ifdef _WIN32
    HANDLE getNativeHandle() const { return processHandle_; }
    void setNativeHandle(HANDLE handle) { processHandle_ = handle; }
    void setThreadHandle(HANDLE handle) { threadHandle_ = handle; }
#endif

    /**
     * @brief Close and invalidate the handle
     */
    void close();

private:
#ifdef _WIN32
    HANDLE processHandle_ = nullptr;
    HANDLE threadHandle_ = nullptr;
#endif
};

/**
 * @brief Process launcher for managing external application lifecycle
 *
 * This class provides utilities to launch, monitor, and terminate external
 * processes. It's designed to manage the MicMap application lifecycle
 * alongside SteamVR.
 */
class ProcessLauncher {
public:
    ProcessLauncher() = default;
    ~ProcessLauncher() = default;

    /**
     * @brief Launch a process with the specified path and arguments
     * @param path Path to the executable
     * @param args Command line arguments (optional)
     * @param workingDir Working directory for the process (optional, uses executable's directory if empty)
     * @return ProcessHandle for the launched process, or invalid handle on failure
     */
    static ProcessHandle launchProcess(
        const std::string& path,
        const std::string& args = "",
        const std::string& workingDir = ""
    );

    /**
     * @brief Terminate a process gracefully
     *
     * First attempts to close the process gracefully by posting WM_CLOSE.
     * If the process doesn't terminate within the timeout, it will be forcefully terminated.
     *
     * @param handle Process handle to terminate
     * @param timeoutMs Timeout in milliseconds to wait for graceful termination (default: 3000ms)
     * @return True if the process was terminated successfully
     */
    static bool terminateProcess(ProcessHandle& handle, uint32_t timeoutMs = 3000);

    /**
     * @brief Check if a process is still running
     * @param handle Process handle to check
     * @return True if the process is still running
     */
    static bool isProcessRunning(const ProcessHandle& handle);

    /**
     * @brief Get the directory containing the driver DLL
     * @return Path to the driver directory
     */
    static std::string getDriverDirectory();

    /**
     * @brief Resolve a path relative to the driver directory
     * @param relativePath Relative path from the driver directory
     * @return Absolute path
     */
    static std::string resolveRelativePath(const std::string& relativePath);
};

} // namespace micmap::driver