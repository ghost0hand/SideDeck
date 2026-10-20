#pragma once
#include <windows.h>

namespace focusguard {
inline bool activate(HWND window) {
    if (!window || !IsWindow(window)) return false;
    HWND foreground = GetForegroundWindow();
    DWORD currentThread = GetCurrentThreadId();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attached = foregroundThread && foregroundThread != currentThread && AttachThreadInput(currentThread, foregroundThread, TRUE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    if (GetForegroundWindow() == window) SetFocus(window);
    if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);
    return GetForegroundWindow() == window;
}

class Guard {
public:
    bool acquire(HWND overlay, HWND previous) {
        overlay_ = overlay;
        previous_ = previous != overlay && IsWindow(previous) ? previous : nullptr;
        return activate(overlay_);
    }
    bool ownsForeground() const {
        HWND window = GetForegroundWindow();
        if (!overlay_ || !window) return false;
        DWORD process = 0; GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId()) return false;
        for (HWND owner = window; owner; owner = GetWindow(owner, GW_OWNER)) {
            if (owner == overlay_) return true;
        }
        return false;
    }
    void release(bool restore) {
        if (restore && ownsForeground() && previous_ && IsWindow(previous_)) activate(previous_);
        overlay_ = previous_ = nullptr;
    }
private:
    HWND overlay_ = nullptr, previous_ = nullptr;
};

inline bool keyboardNeutral() {
    for (int key = 1; key < 255; ++key) if (GetAsyncKeyState(key) & 0x8000) return false;
    return true;
}
} // namespace focusguard
