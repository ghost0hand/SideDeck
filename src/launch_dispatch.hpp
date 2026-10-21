#pragma once
#include "library.hpp"

namespace launchdispatch {
// CommandLineToArgvW / CRT escaping, including quotes and trailing backslashes.
inline std::wstring quote(const std::wstring& value) {
    std::wstring result=L"\"";size_t slashes=0;
    for(wchar_t c:value) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'"')result.append(slashes*2+1,L'\\');else result.append(slashes,L'\\');
        result+=c;slashes=0;
    }
    result.append(slashes*2,L'\\');return result+L'"';
}
inline DWORD start(const lib::Item& item,HANDLE& process) {
    process=nullptr;wchar_t executable[32768]{};
    if(!GetModuleFileNameW(nullptr,executable,32768))return GetLastError();
    std::wstring command=quote(executable)+L" --launch-item "+std::to_wstring(item.kind)+L" "+quote(item.target);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return GetLastError();
    CloseHandle(child.hThread);process=child.hProcess;return ERROR_SUCCESS;
}
inline int run(int count,wchar_t** arguments) {
    if(count!=4)return ERROR_INVALID_PARAMETER;
    int kind=-1;
    if(wcscmp(arguments[2],L"0")==0)kind=lib::App;
    else if(wcscmp(arguments[2],L"1")==0)kind=lib::Url;
    else if(wcscmp(arguments[2],L"3")==0)kind=lib::Command;
    if(kind<0)return ERROR_INVALID_PARAMETER;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    lib::Item item;item.kind=kind;item.target=arguments[3];DWORD result=lib::launch(item);
    if(SUCCEEDED(com))CoUninitialize();
    return (int)result;
}
}
