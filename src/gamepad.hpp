// XInput 手柄输入：动态加载 xinput1_4/1_3，轮询式，无链接依赖
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>

namespace gp {

// XInput 的 Y 正值表示向上；列表索引向下递增。
inline int verticalDirection(float y) { return y > 0 ? -1 : (y < 0 ? 1 : 0); }

// ---- 自给自足的结构定义（不依赖 xinput.h） ----
struct Pad {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
};
struct State {
    DWORD dwPacketNumber;
    Pad   Gamepad;
};
inline bool isNeutral(const Pad& pad) {
    const int deadzone = 9175;
    return pad.wButtons == 0 && pad.bLeftTrigger <= 30 && pad.bRightTrigger <= 30 &&
        abs((int)pad.sThumbLX) <= deadzone && abs((int)pad.sThumbLY) <= deadzone &&
        abs((int)pad.sThumbRX) <= deadzone && abs((int)pad.sThumbRY) <= deadzone;
}
struct Battery {
    BYTE BatteryType;
    BYTE BatteryLevel;
};

// 按键位
enum Btn : WORD {
    UP = 0x0001, DOWN = 0x0002, LEFT = 0x0004, RIGHT = 0x0008,
    START = 0x0010, BACK = 0x0020, LTHUMB = 0x0040, RTHUMB = 0x0080,
    LB = 0x0100, RB = 0x0200, GUIDE = 0x0400,
    A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000,
};
enum Trigger : int { LT = 0, RT = 1 };

static const int MAX_PADS = 4;

class Input {
public:
    Input() { load(); }
    ~Input() { if (dll_) FreeLibrary(dll_); }

    bool available() const { return getState_ != nullptr; }
    void ignoreIndex(int index) { ignoredIndex_ = index; }
    void vibration(int index, WORD large, WORD small) {
        struct Vibration { WORD large, small; } value{large,small};
        if(setState_)setState_(index,&value);
    }

    // 返回已连接手柄数量；state[i] 填充（未连接则 buttons=0）
    int poll() {
        if (!getState_) return 0;
        connected_ = 0;
        for (int i = 0; i < MAX_PADS; i++) {
            State s{};
            DWORD r = i == ignoredIndex_ ? ERROR_DEVICE_NOT_CONNECTED : getState_(i, &s);
            if (r == 0 /*ERROR_SUCCESS*/) {
                st_[i] = s;
                if (!wasConnected_[i]) { wasConnected_[i] = true; }
                connected_++;
            } else {
                st_[i] = State{};
                wasConnected_[i] = false;
            }
        }
        return connected_;
    }

    const State& state(int i = 0) const { return st_[i]; }
    bool connected(int i = 0) const { return wasConnected_[i]; }
    int count() const { return connected_; }

    WORD buttons(int i = 0) const { return st_[i].Gamepad.wButtons; }
    bool held(WORD mask, int i = 0) const { return (st_[i].Gamepad.wButtons & mask) != 0; }
    bool allHeld(WORD mask, int i = 0) const { return (st_[i].Gamepad.wButtons & mask) == mask && mask != 0; }
    BYTE trigger(Trigger t, int i = 0) const {
        return t == LT ? st_[i].Gamepad.bLeftTrigger : st_[i].Gamepad.bRightTrigger;
    }
    // 摇杆归一化 (-1..1)，死区已处理
    float stickX(bool right, int i = 0) const {
        return norm(right ? st_[i].Gamepad.sThumbRX : st_[i].Gamepad.sThumbLX);
    }
    float stickY(bool right, int i = 0) const {
        return norm(right ? st_[i].Gamepad.sThumbRY : st_[i].Gamepad.sThumbLY);
    }

    // 电池：0=断开 1=空 2=低 3=中 4=满；255=未知
    int battery(int i = 0) const {
        if (!getBattery_) return 255;
        Battery b{};
        if (getBattery_(i, 0 /*BATTERY_DEVTYPE_GAMEPAD*/, &b) != 0) return 255;
        return b.BatteryLevel;
    }
    int batteryType(int i = 0) const {
        if (!getBattery_) return 255;
        Battery b{};
        if (getBattery_(i, 0, &b) != 0) return 255;
        return b.BatteryType;
    }

    static float norm(SHORT v) {
        const float DEAD = 0.28f;
        float f = (float)v / (v < 0 ? 32768.0f : 32767.0f);
        if (f > -DEAD && f < DEAD) return 0.0f;
        // 死区重映射
        float s = (f > 0 ? 1.0f : -1.0f);
        f = (fabsf(f) - DEAD) / (1.0f - DEAD);
        return s * f;
    }

    // 按键名的可读文本（用于配置/UI 显示）
    static std::wstring buttonNames(WORD mask) {
        static const struct { WORD bit; const wchar_t* name; } tbl[] = {
            { UP, L"↑" }, { DOWN, L"↓" }, { LEFT, L"←" }, { RIGHT, L"→" },
            { START, L"START" }, { BACK, L"BACK" }, { LTHUMB, L"LS" }, { RTHUMB, L"RS" },
            { LB, L"LB" }, { RB, L"RB" }, { GUIDE, L"GUIDE" },
            { A, L"A" }, { B, L"B" }, { X, L"X" }, { Y, L"Y" },
        };
        std::wstring out;
        for (auto& t : tbl) {
            if (mask & t.bit) {
                if (!out.empty()) out += L"+";
                out += t.name;
            }
        }
        return out.empty() ? L"(无)" : out;
    }

private:
    typedef DWORD (WINAPI* GetStateT)(DWORD, State*);
    typedef DWORD (WINAPI* GetBatteryT)(DWORD, BYTE, Battery*);
    typedef DWORD (WINAPI* SetStateT)(DWORD, void*);

    HMODULE dll_ = nullptr;
    GetStateT getState_ = nullptr;
    GetBatteryT getBattery_ = nullptr;
    SetStateT setState_ = nullptr;
    State st_[MAX_PADS]{};
    bool wasConnected_[MAX_PADS]{};
    int connected_ = 0;
    int ignoredIndex_ = -1;

    void load() {
        const wchar_t* names[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput1_2.dll", L"xinput9_1_0.dll" };
        for (auto n : names) {
            HMODULE h = LoadLibraryExW(n, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!h) continue;
            FARPROC f = GetProcAddress(h, "XInputGetState");
            if (!f) { FreeLibrary(h); continue; }
            dll_ = h;
            getState_ = (GetStateT)f;
            getBattery_ = (GetBatteryT)GetProcAddress(h, "XInputGetBatteryInformation");
            setState_ = (SetStateT)GetProcAddress(h, "XInputSetState");
            break;
        }
    }
};

} // namespace gp
