#pragma once
#include <setupapi.h>
#include <cfgmgr32.h>
#include <mmsystem.h>
#include <winioctl.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include "gamepad.hpp"
#include "library.hpp"

// Driver configuration is leased to a separate watchdog process. Never clear
// another remapper's lists, and never hide the virtual output created below.
namespace isolation {
constexpr DWORD success = 0x20000000;
inline std::wstring exePath() { return lib::appDir() + L"\\SideDeck.exe"; }
inline std::wstring recoveryPath() { return lib::appDir() + L"\\input-recovery.json"; }
inline std::wstring eventName(const wchar_t* suffix, DWORD pid) { return L"Local\\SideDeckInput-" + std::to_wstring(pid) + suffix; }
inline bool equalPath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
inline bool contains(const std::vector<std::wstring>& list, const std::wstring& value) {
    return std::any_of(list.begin(), list.end(), [&](const auto& entry) { return equalPath(entry, value); });
}
class Driver {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    static DWORD code(int function) { return CTL_CODE(32769, function, METHOD_BUFFERED, FILE_READ_DATA); }
public:
    Driver() { handle_ = CreateFileW(L"\\\\.\\HidHide", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr); }
    ~Driver() { if (valid()) CloseHandle(handle_); }
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool flag(int function, bool& value) {
        BYTE byte = 0; DWORD returned = 0;
        bool ok = DeviceIoControl(handle_, code(function), nullptr, 0, &byte, 1, &returned, nullptr);
        value = byte != 0; return ok && returned == 1;
    }
    bool setFlag(int function, bool value) {
        BYTE byte = value; DWORD returned = 0;
        return DeviceIoControl(handle_, code(function), &byte, 1, nullptr, 0, &returned, nullptr);
    }
    bool list(int function, std::vector<std::wstring>& values) {
        std::vector<wchar_t> buffer(65536, 0); DWORD returned = 0;
        if (!DeviceIoControl(handle_, code(function), nullptr, 0, buffer.data(), (DWORD)(buffer.size() * sizeof(wchar_t)), &returned, nullptr)) return false;
        if (returned > buffer.size() * sizeof(wchar_t) || returned % sizeof(wchar_t)) return false;
        size_t end = returned / sizeof(wchar_t), offset = 0; values.clear();
        while (offset < end && buffer[offset]) {
            size_t next = offset; while (next < end && buffer[next]) ++next;
            if (next == end) return false;
            values.emplace_back(buffer.data() + offset, next - offset); offset = next + 1;
        }
        return true;
    }
    bool setList(int function, const std::vector<std::wstring>& values) {
        std::vector<wchar_t> buffer;
        for (auto& value : values) { buffer.insert(buffer.end(), value.begin(), value.end()); buffer.push_back(0); }
        buffer.push_back(0); if (values.empty()) buffer.push_back(0);
        DWORD returned = 0;
        return DeviceIoControl(handle_, code(function), buffer.data(), (DWORD)(buffer.size() * sizeof(wchar_t)), nullptr, 0, &returned, nullptr);
    }
};
inline std::wstring imageName() {
    auto path = exePath(); wchar_t device[32768]{};
    if (path.size() < 3 || !QueryDosDeviceW(path.substr(0, 2).c_str(), device, 32768)) return L"";
    return std::wstring(device) + path.substr(2);
}
inline sj::Value strings(const std::vector<std::wstring>& values) {
    sj::Value result(sj::Type::Array); for (auto& value : values) result.arr.push_back(sj::Value(ui::narrow(value))); return result;
}
inline std::vector<std::wstring> strings(const sj::Value& value) {
    std::vector<std::wstring> result; for (auto& entry : value.arr) result.push_back(ui::utf8(entry.strOr(""))); return result;
}
inline bool restore() {
    if (!lib::fileExists(recoveryPath())) return true;
    sj::Value saved;
    try { saved = sj::parse(lib::readTextFile(recoveryPath())); } catch (...) { return false; }
    if(saved.getInt("schema",0)!=1 || !saved["addedWhite"].isArr() || !saved["addedBlack"].isArr() || !saved["originalBlack"].isArr() || !saved["active"].isBool()) return false;
    Driver driver; if (!driver.valid()) return false;
    bool inverse = false; if (!driver.flag(2054, inverse) || inverse) return false;
    std::vector<std::wstring> white, black;
    if (!driver.list(2048, white) || !driver.list(2050, black)) return false;
    auto addedWhite = strings(saved["addedWhite"]), addedBlack = strings(saved["addedBlack"]);
    auto remove = [](auto& values, const auto& added) {
        values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& item) { return contains(added, item); }), values.end());
    };
    remove(white, addedWhite); remove(black, addedBlack);
    // Restore access to the real pad before removing SideDeck's exemption.
    if (!driver.setList(2051, black) || !driver.setList(2049, white)) return false;
    auto originalBlack = strings(saved["originalBlack"]);
    bool unchanged = black.size() == originalBlack.size();
    for (auto& item : black) unchanged = unchanged && contains(originalBlack, item);
    if (unchanged && !saved.getBool("active", true) && !driver.setFlag(2053, false)) return false;
    return DeleteFileW(recoveryPath().c_str()) != FALSE;
}
inline int watchdog(DWORD parentPid) {
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName(L"-ready", parentPid).c_str());
    HANDLE stop = OpenEventW(SYNCHRONIZE, FALSE, eventName(L"-stop", parentPid).c_str());
    if (!parent || !ready || !stop) { if(parent)CloseHandle(parent); if(ready)CloseHandle(ready); if(stop)CloseHandle(stop); return 2; }
    SetEvent(ready); HANDLE waits[] = {parent, stop}; WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    bool ok = false; for (int retry = 0; retry < 50 && !(ok = restore()); ++retry) Sleep(100);
    CloseHandle(parent); CloseHandle(ready); CloseHandle(stop); return ok ? 0 : 3;
}
struct Device { std::wstring name; std::vector<std::wstring> paths; };
inline void childPaths(DEVINST node, std::vector<std::wstring>& paths, int depth = 0) {
    if (depth > 12) return;
    wchar_t id[MAX_DEVICE_ID_LEN]{};
    if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS && (_wcsnicmp(id,L"HID\\",4)==0 || depth == 0)) paths.emplace_back(id);
    DEVINST child = 0; if (CM_Get_Child(&child, node, 0) != CR_SUCCESS) return;
    do { childPaths(child, paths, depth + 1); } while (CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);
}
inline std::vector<Device> devices() {
    const GUID xusb = {0xec87f1e3,0xc13b,0x4100,{0xb5,0xf7,0x8b,0x84,0xd5,0x42,0x60,0xcb}};
    HDEVINFO info = SetupDiGetClassDevsW(&xusb, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    std::vector<Device> result; if (info == INVALID_HANDLE_VALUE) return result;
    for (DWORD i = 0;; ++i) {
        SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &xusb, i, &iface)) break;
        DWORD needed = 0; SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &needed, nullptr);
        if (!needed) continue;
        std::vector<BYTE> buffer(needed); auto detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buffer.data(); detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA data{}; data.cbSize = sizeof(data);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, needed, nullptr, &data)) continue;
        Device device; wchar_t name[512]{};
        if (!SetupDiGetDeviceRegistryPropertyW(info, &data, SPDRP_FRIENDLYNAME, nullptr, (BYTE*)name, sizeof(name), nullptr))
            SetupDiGetDeviceRegistryPropertyW(info, &data, SPDRP_DEVICEDESC, nullptr, (BYTE*)name, sizeof(name), nullptr);
        device.name = name; childPaths(data.DevInst, device.paths);
        if (!device.paths.empty() && std::none_of(result.begin(), result.end(), [&](const auto& old) { return equalPath(old.paths[0],device.paths[0]); })) result.push_back(device);
    }
    SetupDiDestroyDeviceInfoList(info); return result;
}

// Pure output policy, exercised without any driver or connected controller.
struct Gate {
    bool waiting = true;
    gp::Pad output(const gp::Pad& source, bool blocked, bool connected) {
        if (blocked || !connected) waiting = true;
        else if (gp::isNeutral(source)) waiting = false;
        return waiting ? gp::Pad{} : source;
    }
};
class Bridge {
    HMODULE dll_ = nullptr; void* client_ = nullptr; void* target_ = nullptr;
    void* (__cdecl* alloc_)() = nullptr;
    void (__cdecl* free_)(void*) = nullptr;
    DWORD (__cdecl* connect_)(void*) = nullptr;
    void (__cdecl* disconnect_)(void*) = nullptr;
    void* (__cdecl* targetAlloc_)() = nullptr;
    void (__cdecl* targetFree_)(void*) = nullptr;
    DWORD (__cdecl* add_)(void*,void*) = nullptr;
    DWORD (__cdecl* remove_)(void*,void*) = nullptr;
    DWORD (__cdecl* update_)(void*,void*,gp::Pad) = nullptr;
    DWORD (__cdecl* index_)(void*,void*,ULONG*) = nullptr;
    using Notification = void (CALLBACK*)(void*,void*,BYTE,BYTE,BYTE,void*);
    DWORD (__cdecl* subscribe_)(void*,void*,Notification,void*) = nullptr;
    void (__cdecl* unsubscribe_)(void*) = nullptr;
    std::thread worker_;
    std::atomic<bool> running_{false}, blocked_{true};
    std::atomic<int> virtualIndex_{-1}, fault_{0};
    std::atomic<unsigned> motors_{0};
    std::atomic<WORD> hotkey_{0};
    HANDLE stop_ = nullptr, guardian_ = nullptr;
    bool added_ = false;
    bool subscribed_ = false;
    static void CALLBACK rumble(void*,void*,BYTE large,BYTE small,BYTE,void* context) {
        ((Bridge*)context)->motors_=(unsigned)large | ((unsigned)small<<8);
    }
    template<class T> bool symbol(T& destination, const char* name) { destination = (T)GetProcAddress(dll_, name); return destination != nullptr; }
public:
    ~Bridge() { stop(); }
    bool running() const { return running_; }
    int virtualIndex() const { return virtualIndex_; }
    int fault() const { return fault_; }
    void block(bool value) { if(!value)fault_=0;blocked_ = value; }
    void hotkey(WORD value) { hotkey_ = value; }
    bool verify(std::wstring& error) {
        if (!running_) return false;
        std::wstring command=L"\""+lib::appDir()+L"\\SideDeck.InputProbe.exe\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION probe{};
        if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&probe)) {error=L"缺少独立隔离验证程序，请重新安装 SideDeck。";return false;}
        bool completed=WaitForSingleObject(probe.hProcess,3000)==WAIT_OBJECT_0;
        DWORD mask=128;if(completed)GetExitCodeProcess(probe.hProcess,&mask);
        CloseHandle(probe.hThread);CloseHandle(probe.hProcess);
        int index=virtualIndex_;
        gp::Input local;local.ignoreIndex(index);local.poll();
        if(!completed || index<0 || mask!=(1u<<index) || local.count()!=1) {error=L"隔离验证未通过：其他程序仍可看到输入手柄，虚拟输出不可用，或 SideDeck 无法读取输入。请重新插拔手柄后重试；本次不会启用隔离。";return false;}
        return true;
    }
    bool start(const Device& device, WORD hotkey, std::wstring& error) {
        stop(); fault_ = 0;
        if(device.paths.empty()) {error=L"未选择输入设备。";return false;}
        if (lib::fileExists(recoveryPath()) && !restore()) { error=L"上次隔离未恢复。请关闭 HidHide 配置工具，再点恢复手柄。"; return false; }
        dll_ = LoadLibraryExW((lib::appDir()+L"\\ViGEmClient.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll_ || !symbol(alloc_,"vigem_alloc") || !symbol(free_,"vigem_free") || !symbol(connect_,"vigem_connect") || !symbol(disconnect_,"vigem_disconnect") || !symbol(targetAlloc_,"vigem_target_x360_alloc") || !symbol(targetFree_,"vigem_target_free") || !symbol(add_,"vigem_target_add") || !symbol(remove_,"vigem_target_remove") || !symbol(update_,"vigem_target_x360_update") || !symbol(index_,"vigem_target_x360_get_user_index")) { error=L"缺少完整的 ViGEmClient.dll，请重新安装 SideDeck。"; stop(); return false; }
        client_ = alloc_();
        symbol(subscribe_,"vigem_target_x360_register_notification");
        symbol(unsubscribe_,"vigem_target_x360_unregister_notification");
        if (!client_ || connect_(client_) != success) { error=L"无法连接 ViGEmBus。请安装虚拟手柄驱动后重试。"; stop(); return false; }
        // Start recovery monitoring before making the physical pad invisible.
        DWORD pid = GetCurrentProcessId(); HANDLE ready = CreateEventW(nullptr,TRUE,FALSE,eventName(L"-ready",pid).c_str());
        stop_ = CreateEventW(nullptr,TRUE,FALSE,eventName(L"-stop",pid).c_str());
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION child{};
        std::wstring command=L"\""+exePath()+L"\" --input-watchdog "+std::to_wstring(pid);
        bool launched=ready && stop_ && CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child);
        if (launched) { guardian_=child.hProcess; CloseHandle(child.hThread); }
        bool watched=launched && WaitForSingleObject(ready,5000)==WAIT_OBJECT_0;
        if(ready)CloseHandle(ready);
        if(!watched) { error=L"无法启动输入恢复进程，已取消隔离。"; stop(); return false; }
        bool configured = false;
        {
            Driver driver; std::vector<std::wstring> white,black; bool active=false,inverse=false;
            if (driver.valid() && driver.flag(2052,active) && driver.flag(2054,inverse) && !inverse && driver.list(2048,white) && driver.list(2050,black)) {
                auto app=imageName(); auto originalBlack=black; std::vector<std::wstring> addedWhite,addedBlack;
                if(!app.empty() && !contains(white,app)) {white.push_back(app); addedWhite.push_back(app);}
                for(auto& path:device.paths) if(!contains(black,path)) {black.push_back(path); addedBlack.push_back(path);}
                sj::Value saved(sj::Type::Object); saved["schema"]=sj::Value(1); saved["active"]=sj::Value(active); saved["originalBlack"]=strings(originalBlack);
                saved["addedWhite"]=strings(addedWhite); saved["addedBlack"]=strings(addedBlack);
                auto text=sj::dump(saved);auto temporary=recoveryPath()+L".tmp";FILE* file=_wfopen(temporary.c_str(),L"wb");
                bool written=false; if(file) { written=fwrite(text.data(),1,text.size(),file)==text.size(); written=fclose(file)==0 && written; }
                if(written)written=MoveFileExW(temporary.c_str(),recoveryPath().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
                configured=written && !app.empty() && driver.setList(2049,white) && driver.setList(2051,black) && driver.setFlag(2053,true);
            }
        }
        if(!configured) { error=L"无法配置 HidHide；请先安装驱动并关闭其配置工具。反向隐藏模式暂不支持。"; stop(); return false; }
        target_=targetAlloc_();
        if(!target_ || add_(client_,target_)!=success) {error=L"创建虚拟 Xbox 手柄失败（可能没有空闲槽位）。";stop();return false;}
        added_=true; ULONG outputIndex=0;
        if(index_(client_,target_,&outputIndex)!=success || outputIndex>=4) {error=L"无法识别虚拟手柄编号，已取消隔离。";stop();return false;}
        virtualIndex_=(int)outputIndex; blocked_=true; hotkey_=hotkey; running_=true;
        motors_=0;
        if(subscribe_ && unsubscribe_)subscribed_=subscribe_(client_,target_,rumble,this)==success;
        worker_=std::thread([this,device] {
            gp::Input input; Gate gate; timeBeginPeriod(1);int lastSource=-1;unsigned lastMotors=~0u;
            while(running_) {
                ULONG currentIndex=0;
                bool indexKnown=index_(client_,target_,&currentIndex)==success && currentIndex<4;
                if(indexKnown) virtualIndex_=(int)currentIndex;
                input.poll(); int source=-1,count=0;
                for(int i=0;i<gp::MAX_PADS;++i) if(i!=virtualIndex_ && input.connected(i)) {source=i;++count;}
                DEVINST node=0;
                bool present=CM_Locate_DevNodeW(&node,const_cast<wchar_t*>(device.paths[0].c_str()),CM_LOCATE_DEVNODE_NORMAL)==CR_SUCCESS;
                gp::Pad report{}; bool valid=count==1 && present && indexKnown;
                if(valid) report=input.state(source).Gamepad;
                WORD combination=hotkey_;
                bool chord=valid && combination && (report.wButtons & combination)==combination;
                auto output=gate.output(report,blocked_ || chord,valid);
                unsigned motors=valid && !gate.waiting ? motors_.load() : 0;
                if(lastSource>=0 && (!valid || lastSource!=source))input.vibration(lastSource,0,0);
                if(valid && (lastSource!=source || lastMotors!=motors))input.vibration(source,(motors&255)*257,((motors>>8)&255)*257);
                lastSource=valid?source:-1;lastMotors=motors;
                if(update_(client_,target_,output)!=success) {fault_=2;blocked_=true;}
                else if(fault_!=2)fault_=!indexKnown || !present ? 4 : (count>1 ? 1 : (count==0 ? 3 : 0));
                Sleep(2);
            }
            if(lastSource>=0)input.vibration(lastSource,0,0);
            update_(client_,target_,gp::Pad{}); timeEndPeriod(1);
        });
        return true;
    }
    void stop() {
        running_=false; if(worker_.joinable())worker_.join();
        if(target_) {if(subscribed_)unsubscribe_(target_);if(added_)remove_(client_,target_);targetFree_(target_);target_=nullptr;} added_=false;subscribed_=false;
        if(client_) {disconnect_(client_);free_(client_);client_=nullptr;}
        if(dll_) {FreeLibrary(dll_);dll_=nullptr;} virtualIndex_=-1;
        if(stop_)SetEvent(stop_);
        if(guardian_) {WaitForSingleObject(guardian_,6000);CloseHandle(guardian_);guardian_=nullptr;}
        if(stop_) {CloseHandle(stop_);stop_=nullptr;}
    }
};
} // namespace isolation
