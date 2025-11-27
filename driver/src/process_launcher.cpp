/**
 * @file process_launcher.cpp
 * @brief Implementation of process management utilities
 */

#include "process_launcher.hpp"
#include <openvr_driver.h>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#include <filesystem>

namespace micmap::driver {

// ProcessHandle implementation

ProcessHandle::~ProcessHandle() {
    close();
}

ProcessHandle::ProcessHandle(ProcessHandle&& other) noexcept {
#ifdef _WIN32
    processHandle_ = other.processHandle_;
    threadHandle_ = other.threadHandle_;
    other.processHandle_ = nullptr;
    other.threadHandle_ = nullptr;
#endif
}

ProcessHandle& ProcessHandle::operator=(ProcessHandle&& other) noexcept {
    if (this != &other) {
        close();
#ifdef _WIN32
        processHandle_ = other.processHandle_;
        threadHandle_ = other.threadHandle_;
        other.processHandle_ = nullptr;
        other.threadHandle_ = nullptr;
#endif
    }
    return *this;
}

bool ProcessHandle::isValid() const {
#ifdef _WIN32
    return processHandle_ != nullptr && processHandle_ != INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}

void ProcessHandle::close() {
#ifdef _WIN32
    if (threadHandle_ != nullptr && threadHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(threadHandle_);
        threadHandle_ = nullptr;
    }
    if (processHandle_ != nullptr && processHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
#endif
}

// ProcessLauncher implementation

ProcessHandle ProcessLauncher::launchProcess(
    const std::string& path,
    const std::string& args,
    const std::string& workingDir
) {
    ProcessHandle handle;

#ifdef _WIN32
    // Build the command line
    std::string commandLine = "\"" + path + "\"";
    if (!args.empty()) {
        commandLine += " " + args;
    }

    // Determine working directory
    std::string workDir = workingDir;
    if (workDir.empty()) {
        // Use the directory containing the executable
        std::filesystem::path exePath(path);
        if (exePath.has_parent_path()) {
            workDir = exePath.parent_path().string();
        }
    }

    // Set up startup info
    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_SHOW;  // Show the window normally

    PROCESS_INFORMATION processInfo = {};

    // Create the process
    BOOL success = CreateProcessA(
        nullptr,                                    // Application name (use command line)
        const_cast<char*>(commandLine.c_str()),    // Command line
        nullptr,                                    // Process security attributes
        nullptr,                                    // Thread security attributes
        FALSE,                                      // Inherit handles
        CREATE_NEW_CONSOLE,                         // Creation flags - give it its own console
        nullptr,                                    // Environment
        workDir.empty() ? nullptr : workDir.c_str(), // Working directory
        &startupInfo,                               // Startup info
        &processInfo                                // Process information
    );

    if (success) {
        handle.setNativeHandle(processInfo.hProcess);
        handle.setThreadHandle(processInfo.hThread);
        vr::VRDriverLog()->Log("ProcessLauncher: Successfully launched process\n");
    } else {
        DWORD error = GetLastError();
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), 
                 "ProcessLauncher: Failed to launch process. Error code: %lu\n", error);
        vr::VRDriverLog()->Log(errorMsg);
    }
#else
    // Linux/macOS implementation would go here
    vr::VRDriverLog()->Log("ProcessLauncher: Process launching not implemented for this platform\n");
#endif

    return handle;
}

bool ProcessLauncher::terminateProcess(ProcessHandle& handle, uint32_t timeoutMs) {
    if (!handle.isValid()) {
        return true;  // Already terminated or invalid
    }

#ifdef _WIN32
    HANDLE hProcess = handle.getNativeHandle();

    // First, try to terminate gracefully by enumerating windows and sending WM_CLOSE
    // We need to find windows belonging to this process
    struct EnumData {
        DWORD processId;
        bool foundWindow;
    };

    DWORD processId = GetProcessId(hProcess);
    EnumData enumData = { processId, false };

    // Enumerate all top-level windows and send WM_CLOSE to those belonging to our process
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumData* data = reinterpret_cast<EnumData*>(lParam);
        DWORD windowProcessId;
        GetWindowThreadProcessId(hwnd, &windowProcessId);
        
        if (windowProcessId == data->processId) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            data->foundWindow = true;
        }
        return TRUE;  // Continue enumeration
    }, reinterpret_cast<LPARAM>(&enumData));

    if (enumData.foundWindow) {
        vr::VRDriverLog()->Log("ProcessLauncher: Sent WM_CLOSE to process windows, waiting for graceful termination\n");
    }

    // Wait for the process to terminate gracefully
    DWORD waitResult = WaitForSingleObject(hProcess, timeoutMs);

    if (waitResult == WAIT_OBJECT_0) {
        vr::VRDriverLog()->Log("ProcessLauncher: Process terminated gracefully\n");
        handle.close();
        return true;
    }

    // Process didn't terminate gracefully, force terminate
    vr::VRDriverLog()->Log("ProcessLauncher: Process did not terminate gracefully, forcing termination\n");
    
    if (TerminateProcess(hProcess, 0)) {
        // Wait a bit for the termination to complete
        WaitForSingleObject(hProcess, 1000);
        vr::VRDriverLog()->Log("ProcessLauncher: Process forcefully terminated\n");
        handle.close();
        return true;
    } else {
        DWORD error = GetLastError();
        char errorMsg[256];
        snprintf(errorMsg, sizeof(errorMsg), 
                 "ProcessLauncher: Failed to terminate process. Error code: %lu\n", error);
        vr::VRDriverLog()->Log(errorMsg);
        return false;
    }
#else
    // Linux/macOS implementation would go here
    vr::VRDriverLog()->Log("ProcessLauncher: Process termination not implemented for this platform\n");
    return false;
#endif
}

bool ProcessLauncher::isProcessRunning(const ProcessHandle& handle) {
    if (!handle.isValid()) {
        return false;
    }

#ifdef _WIN32
    DWORD exitCode;
    if (GetExitCodeProcess(handle.getNativeHandle(), &exitCode)) {
        return exitCode == STILL_ACTIVE;
    }
    return false;
#else
    return false;
#endif
}

std::string ProcessLauncher::getDriverDirectory() {
#ifdef _WIN32
    // Get the path to the current DLL (the driver)
    HMODULE hModule = nullptr;
    
    // Get handle to this DLL by using an address within it
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&getDriverDirectory),
            &hModule)) {
        
        char dllPath[MAX_PATH];
        if (GetModuleFileNameA(hModule, dllPath, MAX_PATH) > 0) {
            std::filesystem::path path(dllPath);
            return path.parent_path().string();
        }
    }
    
    // Fallback: return current directory
    char currentDir[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, currentDir) > 0) {
        return std::string(currentDir);
    }
#endif
    
    return "";
}

std::string ProcessLauncher::resolveRelativePath(const std::string& relativePath) {
    std::string driverDir = getDriverDirectory();
    if (driverDir.empty()) {
        return relativePath;
    }

    std::filesystem::path basePath(driverDir);
    std::filesystem::path relPath(relativePath);
    
    // Combine and normalize the path
    std::filesystem::path fullPath = basePath / relPath;
    
    try {
        // Try to get the canonical (absolute) path
        if (std::filesystem::exists(fullPath)) {
            return std::filesystem::canonical(fullPath).string();
        }
        // If file doesn't exist yet, just return the combined path
        return fullPath.string();
    } catch (const std::filesystem::filesystem_error&) {
        return fullPath.string();
    }
}

} // namespace micmap::driver