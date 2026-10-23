#include "../src/video_wallpaper.hpp"
#include <cassert>
#include <cstdio>
int main(){
int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
if(argc!=2){puts("Usage: video_wallpaper.exe path-to-local.mp4");LocalFree(argv);return 1;}
std::wstring path=argv[1];LocalFree(argv);
ULONG_PTR token;Gdiplus::GdiplusStartupInput init;Gdiplus::GdiplusStartup(&token,&init,nullptr);
{wallpaper::Video video;video.open(path);video.playing(true);
std::unique_ptr<Gdiplus::Bitmap> frame;int count=0;unsigned long long first=0,last=0;
for(int i=0;i<6000 && (count<35 || !video.loops());++i){Sleep(10);if(FAILED(video.error())){printf("Decode error: %08lx\n",(unsigned long)video.error());return 2;}if(video.take(frame)){unsigned long long sum=0;for(UINT y=0;y<frame->GetHeight();y+=13)for(UINT x=0;x<frame->GetWidth();x+=13){Gdiplus::Color c;frame->GetPixel(x,y,&c);sum=sum*131+c.GetValue();}if(!count)first=sum;last=sum;++count;}}
assert(count>=35 && first!=last && video.loops()>0);printf("Completed %u loops. ",video.loops());printf("Decoded %d changing frames, %ux%u\n",count,frame->GetWidth(),frame->GetHeight());
const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};frame->Save(L"video-wallpaper-preview.png",&png,nullptr);
video.playing(false);Sleep(150);video.take(frame);Sleep(250);assert(!video.take(frame));video.playing(true);Sleep(200);assert(video.take(frame));video.close();puts("Pause/resume/close passed");}
Gdiplus::GdiplusShutdown(token);}
