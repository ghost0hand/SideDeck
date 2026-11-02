#include "../src/video_wallpaper.hpp"
#include <cassert>
#include <cstdio>
int main(){
int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
if(argc!=2){puts("Usage: video_wallpaper.exe path-to-local.mp4");LocalFree(argv);return 1;}
std::wstring path=argv[1];LocalFree(argv);
ULONG_PTR token;Gdiplus::GdiplusStartupInput init;Gdiplus::GdiplusStartup(&token,&init,nullptr);
{wallpaper::Video video;video.open(path);Sleep(200);assert(!video.loaded());video.playing(true);
// 缩放目标由 UI 下发：解码线程产出面板尺寸的预乘帧，take 只做行拷贝。
video.target(621,900,"auto",0.75f);
std::unique_ptr<Gdiplus::Bitmap> frame;int count=0;unsigned long long first=0,last=0;
for(int i=0;i<6000 && (count<35 || !video.loops());++i){Sleep(10);if(FAILED(video.error())){printf("Decode error: %08lx\n",(unsigned long)video.error());return 2;}if(video.take(frame)){unsigned long long sum=0;for(UINT y=0;y<frame->GetHeight();y+=13)for(UINT x=0;x<frame->GetWidth();x+=13){Gdiplus::Color c;frame->GetPixel(x,y,&c);sum=sum*131+c.GetValue();}if(!count)first=sum;last=sum;++count;}}
assert(count>=35 && first!=last && video.loops()>0);
assert(frame->GetWidth()==621 && frame->GetHeight()==900); // 面板尺寸由解码线程产出
{Gdiplus::Color corner;frame->GetPixel(0,0,&corner);assert(corner.GetA()>180 && corner.GetA()<=192);} // 0.75 不透明度预乘
printf("Completed %u loops. ",video.loops());printf("Decoded %d changing frames, %ux%u\n",count,frame->GetWidth(),frame->GetHeight());
// 目标尺寸变化后必须重新合成，不能把旧尺寸的帧交出去。
video.target(400,700,"cover",1.0f);
for(int i=0;i<100;++i){Sleep(10);if(video.take(frame) && frame->GetWidth()==400 && frame->GetHeight()==700)break;}
assert(frame->GetWidth()==400 && frame->GetHeight()==700);
const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};frame->Save(L"video-wallpaper-preview.png",&png,nullptr);
video.playing(false);Sleep(150);video.take(frame);Sleep(250);assert(!video.take(frame));video.playing(true);Sleep(200);assert(video.take(frame));video.playing(false);Sleep(2400);assert(!video.loaded());video.playing(true);
for(int i=0;i<100 && !video.take(frame);++i)Sleep(20);assert(video.loaded());video.close();puts("Pause/resume/close passed");}
Gdiplus::GdiplusShutdown(token);}
