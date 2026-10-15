#include <windows.h>
#include <restartmanager.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>

namespace {
int failures = 0;
void check(bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    std::fflush(stdout);
    if (!condition) ++failures;
}
struct Child {
    PROCESS_INFORMATION process{};
    HWND window = nullptr;
    ~Child() {
        // Only clean up processes created by this test, retaining the original handle.
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(process.hProcess, 1);
                WaitForSingleObject(process.hProcess, 5000);
            }
            CloseHandle(process.hProcess); CloseHandle(process.hThread);
        }
    }
    bool start(const std::wstring& path, const wchar_t* className) {
        auto command = L"\"" + path + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &startup, &process)) return false;
        struct Search { DWORD pid; const wchar_t* cls; HWND hwnd; } search{process.dwProcessId, className, nullptr};
        for (int i = 0; i < 100 && !search.hwnd; ++i) {
            EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
                auto& s = *(Search*)data; DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
                wchar_t cls[128]{}; GetClassNameW(hwnd, cls, 128);
                if (pid == s.pid && wcscmp(cls, s.cls) == 0) { s.hwnd = hwnd; return FALSE; }
                return TRUE;
            }, (LPARAM)&search);
            if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) return false;
        }
        window = search.hwnd;
        return window != nullptr;
    }
    bool alive() const { return WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT; }
    bool exited() const { return WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0; }
};
bool send(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, DWORD_PTR& result) {
    return SendMessageTimeoutW(hwnd, message, wp, lp, SMTO_ABORTIFHUNG, 2000, &result) != 0;
}
DWORD shutdownWithRestartManager(const std::wstring& path, ULONG flags) {
    DWORD session = 0; wchar_t key[CCH_RM_SESSION_KEY + 1]{};
    DWORD error = RmStartSession(&session, 0, key);
    if (error) return error;
    LPCWSTR file = path.c_str();
    error = RmRegisterResources(session, 1, &file, 0, nullptr, 0, nullptr);
    if (!error) error = RmShutdown(session, flags, nullptr);
    RmEndSession(session);
    return error;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 1;
    const bool legacy = wcscmp(argv[2], L"legacy") == 0;
    const auto executable = std::filesystem::absolute(argv[1]).wstring();
    if (!legacy) {
        HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\TypeTide-WinSingleInstance");
        if (existing) {
            CloseHandle(existing);
            std::puts("SKIP: an interactive TypeTide-Win instance is running; leave it untouched.");
            return 77;
        }
    }
    const auto folder = std::filesystem::temp_directory_path() /
        (L"TypeTide-shutdown-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    SetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", folder.c_str());
    std::ofstream(folder / "settings.json") << R"({"enabled":false,"backend":"openai","hasCompletedFirstLaunch":true,"lastUpdateCheck":4102444800})";
    if (legacy) {
        // Register the exact installed file, as Inno does. A different copy stays running.
        const auto otherPath = folder / "UnrelatedTray.exe";
        std::filesystem::copy_file(executable, otherPath);
        {
            Child target, unrelated;
            if (!target.start(executable, L"TypeTide-LegacyTrayFixture") ||
                !unrelated.start(otherPath.wstring(), L"TypeTide-LegacyTrayFixture")) return 1;
            DWORD_PTR result = 0;
            send(target.window, WM_ENDSESSION, TRUE, ENDSESSION_CLOSEAPP, result);
            check(target.alive(), "legacy fixture reproduces ignored shutdown notification");
            const DWORD error = shutdownWithRestartManager(executable, RmForceShutdown);
            std::printf("Restart Manager result: %lu\n", error);
            check(error == ERROR_SUCCESS && target.exited(), "installer force policy releases legacy executable");
            check(unrelated.alive(), "another executable outside the install path is left running");
        }
        std::filesystem::remove(otherPath);
    } else {
        {
            Child app;
            if (!app.start(executable, L"TypeTide-WinApp")) return 1;
            DWORD_PTR result = 0;
            check(send(app.window, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP, result) && result == TRUE && app.alive(),
                "shutdown query acknowledges without exiting");
            send(app.window, WM_ENDSESSION, FALSE, ENDSESSION_CLOSEAPP, result);
            check(app.alive(), "cancelled shutdown keeps the app alive");
            PostMessageW(app.window, WM_ENDSESSION, TRUE, ENDSESSION_CLOSEAPP);
            check(app.exited(), "confirmed shutdown exits the tray process");
        }
        {
            Child app;
            if (!app.start(executable, L"TypeTide-WinApp")) return 1;
            PostMessageW(app.window, WM_CLOSE, 0, 0);
            check(app.exited(), "WM_CLOSE exits the process rather than just destroying the host");
        }
        {
            Child app;
            if (!app.start(executable, L"TypeTide-WinApp")) return 1;
            const DWORD error = shutdownWithRestartManager(executable, 0);
            std::printf("Restart Manager result: %lu\n", error);
            check(error == ERROR_SUCCESS && app.exited(), "Restart Manager closes the app without forcing it");
        }
    }
    HANDLE file = CreateFileW(executable.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(file != INVALID_HANDLE_VALUE, "executable is unlocked for the installer to replace");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    // Test data stays isolated; logs may be retained for diagnostics.
    return failures ? 1 : 0;
}
