// Opt-in hardware test: creates only a neutral virtual pad, never presses keys.
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include "../src/input_bridge.hpp"
static void check(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
int main() {
    HMODULE module=nullptr;void* client=nullptr;void* target=nullptr;bool added=false;int result=0;
    void (__cdecl* disconnect)(void*)=nullptr;void (__cdecl* release)(void*)=nullptr;
    void (__cdecl* targetFree)(void*)=nullptr;DWORD (__cdecl* remove)(void*,void*)=nullptr;
    try {
        auto candidates=isolation::devices();printf("Present XUSB device interfaces: %zu\n",candidates.size());
        module=LoadLibraryExW((lib::appDir()+L"\\ViGEmClient.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        check(module,"load pinned native client");
        auto alloc=(void*(__cdecl*)())GetProcAddress(module,"vigem_alloc");
        auto connect=(DWORD(__cdecl*)(void*))GetProcAddress(module,"vigem_connect");
        release=(void(__cdecl*)(void*))GetProcAddress(module,"vigem_free");
        disconnect=(void(__cdecl*)(void*))GetProcAddress(module,"vigem_disconnect");
        auto targetAlloc=(void*(__cdecl*)())GetProcAddress(module,"vigem_target_x360_alloc");
        targetFree=(void(__cdecl*)(void*))GetProcAddress(module,"vigem_target_free");
        auto add=(DWORD(__cdecl*)(void*,void*))GetProcAddress(module,"vigem_target_add");
        remove=(DWORD(__cdecl*)(void*,void*))GetProcAddress(module,"vigem_target_remove");
        auto update=(DWORD(__cdecl*)(void*,void*,gp::Pad))GetProcAddress(module,"vigem_target_x360_update");
        auto index=(DWORD(__cdecl*)(void*,void*,ULONG*))GetProcAddress(module,"vigem_target_x360_get_user_index");
        check(alloc&&connect&&release&&disconnect&&targetAlloc&&targetFree&&add&&remove&&update&&index,"client ABI exports");
        client=alloc();check(client&&connect(client)==isolation::success,"connect existing ViGEmBus");
        target=targetAlloc();check(target,"allocate output pad");
        check(add(client,target)==isolation::success,"add neutral output pad");added=true;
        ULONG user=4;check(index(client,target,&user)==isolation::success&&user<4,"retrieve output XInput slot");
        check(update(client,target,gp::Pad{})==isolation::success,"send neutral report");
        Sleep(100);gp::Input input;input.poll();
        check(input.connected((int)user)&&gp::isNeutral(input.state((int)user).Gamepad),"read neutral report through XInput");
        printf("PASS: native client ABI, existing bus, virtual output slot %lu, neutral report roundtrip\n",user);
    } catch(const std::exception& error) {fprintf(stderr,"FAIL: %s (Win32 %lu)\n",error.what(),GetLastError());result=1;}
    if(target) {if(added)remove(client,target);if(targetFree)targetFree(target);}
    if(client) {if(disconnect)disconnect(client);if(release)release(client);}
    if(module)FreeLibrary(module);return result;
}
