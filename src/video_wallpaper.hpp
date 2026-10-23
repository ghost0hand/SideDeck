#pragma once
#include "render.hpp"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <chrono>

namespace wallpaper {
template<class T> struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}
    T** out(){return &p;}T* operator->(){return p;}
};
// Decoder and pacing live on one worker. The UI owns its own frame copy.
class Video {
    std::thread worker;std::mutex mutex;std::condition_variable wake;
    bool stopping=false,active=false;std::vector<BYTE> pixels;
    UINT width=0,height=0;unsigned revision=0,shown=0;
    std::atomic<HRESULT> failure{S_OK};
    std::atomic<unsigned> repeats{0};
public:
    ~Video(){close();}
    void close(){
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_all();
        if(worker.joinable())worker.join();
        pixels.clear();width=height=0;revision=shown=0;
    }
    void playing(bool value){{std::lock_guard<std::mutex> lock(mutex);active=value;}wake.notify_all();}
    HRESULT error()const{return failure.load();}
    unsigned loops()const{return repeats.load();}
    void open(const std::wstring& path){
        close();stopping=false;active=false;failure=S_OK;repeats=0;
        if(path.empty())return;
        worker=std::thread([this,path]{
            HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            HRESULT hr=MFStartup(MF_VERSION);
            if(SUCCEEDED(hr)) {
                hr=decode(path);MFShutdown();
            }
            failure=hr;if(SUCCEEDED(com))CoUninitialize();
        });
    }
    bool take(std::unique_ptr<Gdiplus::Bitmap>& frame){
        std::lock_guard<std::mutex> lock(mutex);
        if(shown==revision || pixels.empty())return false;
        Gdiplus::Bitmap source(width,height,width*4,PixelFormat32bppRGB,pixels.data());
        frame.reset(source.Clone(0,0,width,height,PixelFormat32bppPARGB));shown=revision;return true;
    }
private:
    HRESULT decode(const std::wstring& path){
        Com<IMFAttributes> options;HRESULT hr=MFCreateAttributes(options.out(),3);if(FAILED(hr))return hr;
        options->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,TRUE);
        options->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE);
        Com<IMFSourceReader> reader;hr=MFCreateSourceReaderFromURL(path.c_str(),options.p,reader.out());if(FAILED(hr))return hr;
        reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
        reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
        Com<IMFMediaType> native;hr=reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,native.out());if(FAILED(hr))return hr;
        UINT w=0,h=0;MFGetAttributeSize(native.p,MF_MT_FRAME_SIZE,&w,&h);
        if(!w || !h || w>8192 || h>8192)return E_INVALIDARG;
        // Bound the wallpaper surface independently of the video's source size.
        float scale=std::min(1.0f,640.0f/std::max(w,h));
        w=std::max(2u,((UINT)(w*scale)/2)*2);h=std::max(2u,((UINT)(h*scale)/2)*2);
        Com<IMFMediaType> type;MFCreateMediaType(type.out());
        type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
        MFSetAttributeSize(type.p,MF_MT_FRAME_SIZE,w,h);
        hr=reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,type.p);if(FAILED(hr))return hr;
        Com<IMFMediaType> actual;hr=reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,actual.out());if(FAILED(hr))return hr;
        MFGetAttributeSize(actual.p,MF_MT_FRAME_SIZE,&w,&h);if(!w || !h || w>1920 || h>1920)return E_INVALIDARG;
        UINT strideValue=w*4;actual->GetUINT32(MF_MT_DEFAULT_STRIDE,&strideValue);LONG stride=(LONG)strideValue;
        LONGLONG previous=-1;
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||active;});if(stopping)return S_OK;}
            DWORD flags=0;LONGLONG stamp=0;Com<IMFSample> sample;
            hr=reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&stamp,sample.out());if(FAILED(hr))return hr;
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){PROPVARIANT start{};start.vt=VT_I8;start.hVal.QuadPart=0;
                hr=reader->SetCurrentPosition(GUID_NULL,start);if(FAILED(hr))return hr;++repeats;previous=-1;continue;}
            if(!sample.p)continue;
            Com<IMFMediaBuffer> buffer;hr=sample->ConvertToContiguousBuffer(buffer.out());if(FAILED(hr))return hr;
            BYTE* data=nullptr;DWORD length=0;hr=buffer->Lock(&data,nullptr,&length);if(FAILED(hr))return hr;
            std::vector<BYTE> next((size_t)w*h*4);
            if((size_t)std::abs(stride)*h>length){buffer->Unlock();return E_FAIL;}
            for(UINT y=0;y<h;++y)memcpy(next.data()+(size_t)y*w*4,data+(size_t)(stride<0?h-1-y:y)*std::abs(stride),w*4);
            buffer->Unlock();
            int delay=previous<0?0:(int)std::clamp<LONGLONG>((stamp-previous)/10000,1,100);previous=stamp;
            {std::unique_lock<std::mutex> lock(mutex);
             if(wake.wait_for(lock,std::chrono::milliseconds(delay),[&]{return stopping||!active;})){if(stopping)return S_OK;continue;}
             pixels=std::move(next);width=w;height=h;++revision;}
        }
    }
};
}
