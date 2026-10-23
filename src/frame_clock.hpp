#pragma once
#include <windows.h>
namespace frameclock {
inline double seconds(){LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);return (double)now.QuadPart/frequency.QuadPart;}
inline HANDLE create(){
    // High-resolution waitable timers do not depend on low-priority WM_TIMER.
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,2,TIMER_ALL_ACCESS);
    if(!timer)return nullptr;
    LARGE_INTEGER due;due.QuadPart=-160000;
    if(!SetWaitableTimer(timer,&due,16,nullptr,nullptr,FALSE)){CloseHandle(timer);return nullptr;}
    return timer;
}
}
