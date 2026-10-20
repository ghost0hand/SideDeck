// Opt-in desktop check: only synthetic windows/processes, never a real game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <stdexcept>
#include "../src/focus_guard.hpp"

static LRESULT CALLBACK fixtureProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
static HWND fixture(const wchar_t* name, HWND owner = nullptr) {
    WNDCLASSW cls{}; cls.lpfnWndProc = fixtureProc; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = name;
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&cls);
    return CreateWindowExW(WS_EX_TOOLWINDOW, name, L"SideDeck input handoff check (test window)", WS_OVERLAPPEDWINDOW,
        80, 80, 380, 160, owner, nullptr, cls.hInstance, nullptr);
}
static void require(bool value, const char* description) { if (!value) throw std::runtime_error(description); }

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--probe") {
        HWND window = fixture(L"SideDeckFocusProbe");
        if (!window) return 2;
        ShowWindow(window, SW_SHOW); UpdateWindow(window);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        return 0;
    }
    HWND original = GetForegroundWindow();
    HWND overlay = nullptr, other = nullptr, modal = nullptr, probe = nullptr;
    PROCESS_INFORMATION child{};
    int result = 0;
    try {
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
        std::wstring command = std::wstring(L"\"") + executable + L"\" --probe";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child), "start mock game process");
        WaitForInputIdle(child.hProcess, 3000);
        struct Search { DWORD process; HWND window; } search{child.dwProcessId, nullptr};
        for (int retry = 0; retry < 40 && !search.window; ++retry) {
            EnumWindows([](HWND window, LPARAM param) -> BOOL {
                auto data = (Search*)param; DWORD process = 0; GetWindowThreadProcessId(window, &process);
                if (process == data->process && IsWindowVisible(window)) { data->window = window; return FALSE; }
                return TRUE;
            }, (LPARAM)&search);
            if (!search.window) Sleep(50);
        }
        probe = search.window; require(probe != nullptr, "mock game window exists");
        require(focusguard::activate(probe), "activate cross-process mock game");
        overlay = fixture(L"SideDeckFocusOverlay"); require(overlay != nullptr, "create mock overlay");
        ShowWindow(overlay, SW_SHOWNOACTIVATE);
        focusguard::Guard guard;
        require(guard.acquire(overlay, probe), "overlay takes foreground from mock game");
        require(guard.ownsForeground(), "overlay owns keyboard focus");
        guard.release(true); require(GetForegroundWindow() == probe, "restore foreground to mock game");
        require(guard.acquire(overlay, probe), "reacquire overlay");
        modal = fixture(L"SideDeckFocusModal", overlay); require(modal != nullptr, "create owned file-dialog fixture");
        ShowWindow(modal, SW_SHOWNOACTIVATE); require(focusguard::activate(modal), "activate owned modal");
        require(guard.ownsForeground(), "owned modal retains overlay input ownership");
        DestroyWindow(modal); modal = nullptr;
        require(focusguard::activate(overlay), "reactivate overlay");
        other = fixture(L"SideDeckFocusOther"); require(other != nullptr, "create independent window fixture");
        ShowWindow(other, SW_SHOWNOACTIVATE); require(focusguard::activate(other), "simulate switching to another app window");
        require(!guard.ownsForeground(), "independent window is not overlay-owned");
        guard.release(true); require(GetForegroundWindow() == other, "do not steal focus back after external switch");
        focusguard::Guard invalid; require(!invalid.acquire(nullptr, probe), "reject invalid overlay instead of accepting input");
        puts("PASS: cross-process foreground acquisition/restore, owned modal, external focus loss, failed acquisition");
    } catch (const std::exception& error) {
        fprintf(stderr, "FAIL: %s (Win32 %lu)\n", error.what(), GetLastError()); result = 1;
    }
    if (modal) DestroyWindow(modal);
    if (other) DestroyWindow(other);
    if (overlay) DestroyWindow(overlay);
    if (probe) PostMessageW(probe, WM_CLOSE, 0, 0);
    if (child.hProcess) { WaitForSingleObject(child.hProcess, 3000); CloseHandle(child.hProcess); }
    if (child.hThread) CloseHandle(child.hThread);
    if (original && IsWindow(original)) focusguard::activate(original);
    return result;
}
