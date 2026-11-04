#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace startup {

inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kValueName[] = L"SideDeck";

inline std::wstring commandFor(const std::wstring& exePath) {
    if (exePath.empty()) return {};
    return L"\"" + exePath + L"\" --startup";
}

inline std::wstring currentCommand() {
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());
    if (length == 0 || length >= buffer.size()) return {};
    return commandFor(std::wstring(buffer.data(), length));
}

inline std::wstring readValue(const wchar_t* name = kValueName) {
    DWORD bytes = 0;
    LONG result = RegGetValueW(HKEY_CURRENT_USER, kRunKey, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (result != ERROR_SUCCESS || bytes < sizeof(wchar_t)) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    result = RegGetValueW(HKEY_CURRENT_USER, kRunKey, name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes);
    if (result != ERROR_SUCCESS) return {};
    value.resize(wcslen(value.c_str()));
    return value;
}

inline bool enabled(const wchar_t* name = kValueName) {
    return !readValue(name).empty();
}

inline bool setEnabled(bool enable, const wchar_t* name = kValueName) {
    if (!enable) {
        HKEY key = nullptr;
        LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
        if (result == ERROR_FILE_NOT_FOUND) return true;
        if (result != ERROR_SUCCESS) return false;
        result = RegDeleteValueW(key, name);
        RegCloseKey(key);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    std::wstring command = currentCommand();
    if (command.empty()) return false;
    HKEY key = nullptr;
    DWORD disposition = 0;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE, nullptr, &key, &disposition);
    if (result != ERROR_SUCCESS) return false;
    result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
        (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

}  // namespace startup
