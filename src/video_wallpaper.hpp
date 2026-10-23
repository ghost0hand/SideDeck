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
    std::atomic<bool> resident{false};
    std::vector<BYTE> display;
public:
    ~Video(){close();}
    void close(){
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_all();
        if(worker.joinable())worker.join();
        std::vector<BYTE>().swap(pixels);std::vector<BYTE>().swap(display);width=height=0;revision=shown=0;
    }
    void playing(bool value){{std::lock_guard<std::mutex> lock(mutex);active=value;}wake.notify_all();}
    HRESULT error()const{return failure.load();}
    bool loaded()const{return resident.load();}
    unsigned loops()const{return repeats.load();}
    void open(const std::wstring& path){
        close();stopping=false;active=false;failure=S_OK;repeats=0;
        if(path.empty())return;
        worker=std::thread([this,path]{
            HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            HRESULT hr=S_OK;
            for(;;) {
                {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||active;});if(stopping)break;}
                hr=MFStartup(MF_VERSION);
                if(SUCCEEDED(hr)) {resident=true;hr=decode(path);resident=false;MFShutdown();}
                {std::lock_guard<std::mutex> lock(mutex);std::vector<BYTE>().swap(pixels);}
                if(FAILED(hr))break;
            }
            failure=hr;if(SUCCEEDED(com))CoUninitialize();
        });
    }
    bool take(std::unique_ptr<Gdiplus::Bitmap>& frame){
        UINT w=0,h=0;
        {std::lock_guard<std::mutex> lock(mutex);
         if(shown==revision || pixels.empty())return false;
         display.swap(pixels);w=width;h=height;shown=revision;}
        if(!frame || frame->GetWidth()!=w || frame->GetHeight()!=h)frame=std::make_unique<Gdiplus::Bitmap>(w,h,PixelFormat32bppRGB);
        Gdiplus::BitmapData data;Gdiplus::Rect rect(0,0,w,h);
        if(frame->LockBits(&rect,Gdiplus::ImageLockModeWrite,PixelFormat32bppRGB,&data)!=Gdiplus::Ok)return false;
        for(UINT y=0;y<h;++y)memcpy((BYTE*)data.Scan0+(ptrdiff_t)y*data.Stride,display.data()+(size_t)y*w*4,w*4);
        frame->UnlockBits(&data);return true;
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
        LONGLONG previous=-1;std::vector<BYTE> next((size_t)w*h*4);
        auto due=std::chrono::steady_clock::now();
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex);
             if(!active){if(!wake.wait_for(lock,std::chrono::seconds(2),[&]{return stopping||active;}))return S_FALSE;due=std::chrono::steady_clock::now();}
             if(stopping)return S_OK;}
            DWORD flags=0;LONGLONG stamp=0;Com<IMFSample> sample;
            hr=reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&stamp,sample.out());if(FAILED(hr))return hr;
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){PROPVARIANT start{};start.vt=VT_I8;start.hVal.QuadPart=0;
                hr=reader->SetCurrentPosition(GUID_NULL,start);if(FAILED(hr))return hr;++repeats;previous=-1;continue;}
            if(!sample.p)continue;
            Com<IMFMediaBuffer> buffer;hr=sample->ConvertToContiguousBuffer(buffer.out());if(FAILED(hr))return hr;
            BYTE* data=nullptr;DWORD length=0;hr=buffer->Lock(&data,nullptr,&length);if(FAILED(hr))return hr;
            next.resize((size_t)w*h*4);
            if((size_t)std::abs(stride)*h>length){buffer->Unlock();return E_FAIL;}
            for(UINT y=0;y<h;++y)memcpy(next.data()+(size_t)y*w*4,data+(size_t)(stride<0?h-1-y:y)*std::abs(stride),w*4);
            buffer->Unlock();
            int delay=previous<0?0:(int)std::clamp<LONGLONG>((stamp-previous)/10000,1,100);previous=stamp;
            {std::unique_lock<std::mutex> lock(mutex);
             due+=std::chrono::milliseconds(delay);
             if(due<std::chrono::steady_clock::now()-std::chrono::milliseconds(100))due=std::chrono::steady_clock::now();
             if(wake.wait_until(lock,due,[&]{return stopping||!active;})){if(stopping)return S_OK;continue;}
             pixels.swap(next);width=w;height=h;++revision;}
        }
    }
};
}
