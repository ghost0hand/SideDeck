// Synthetic, offscreen benchmark. Never opens games, an overlay or a driver.
#define WinMain SideDeckEntry
#include "../src/main.cpp"
#undef WinMain
#include <chrono>
// 模拟 Video::take 每帧把合成好的面板帧拷进图层位图（真实路径里 UI 线程只剩这一步）。
static void simulatePaper(Bitmap* surface, std::vector<BYTE>& scratch) {
    if (!surface) return;
    UINT w = surface->GetWidth(), h = surface->GetHeight();
    Gdiplus::BitmapData data; Gdiplus::Rect rect(0, 0, w, h);
    if (surface->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return;
    for (UINT y = 0; y < h; ++y) memcpy((BYTE*)data.Scan0 + (ptrdiff_t)y * data.Stride, scratch.data() + (size_t)y * w * 4, w * 4);
    surface->UnlockBits(&data);
}
static void run(bool cached) {
    G::renderCaching=cached;G::rowCache.clear();G::glassCache.reset();G::glassUnder.reset();G::glassOver.reset();
    Bitmap bitmap(G::W,G::H,PixelFormat32bppPARGB);Graphics graphics(&bitmap);
    std::vector<BYTE> scratch((size_t)G::W * G::H * 4);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);std::vector<double> times;
    for(int frame=0;frame<150;++frame) {
        G::scroll[0]=(frame%40)*4.0f;G::sel[0]=2+(frame/12)%4;
        auto start=std::chrono::steady_clock::now();
        simulatePaper(G::videoSurface.get(),scratch);
        graphics.Clear(Gdiplus::Color(0,0,0,0));drawPanel(graphics);
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(frame>=30)times.push_back(elapsed);
    }
    std::sort(times.begin(),times.end());double sum=0;for(auto value:times)sum+=value;
    printf("%s: mean=%.3f ms, p95=%.3f ms, frames=%zu\n",cached?"cached":"uncached",sum/times.size(),times[times.size()*95/100],times.size());
}
int main() {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ULONG_PTR token=0;GdiplusStartupInput startup;GdiplusStartup(&token,&startup,nullptr);
    G::cfg.scale=1.35f;ui::g_textScale=G::cfg.scale;G::W=621;G::H=900;G::anim=G::pageAnim=G::themeProgress=1;
    G::tabs.resize(1);G::tabs[0].name=L"游戏库";G::sel.resize(1);G::scroll.resize(1);G::scrollTarget.resize(1);
    for(int i=0;i<40;++i) {Item item;item.kind=lib::Guide;item.title=L"性能测试示例 "+std::to_wstring(i);item.subtitle=L"合成内容 · 不读取个人资料";G::tabs[0].items.push_back(item);}
    run(false);run(true);
    // 动态壁纸：合成好的面板帧，UI 每帧只拷一次 + 三层贴图。
    G::videoSurface=std::make_unique<Bitmap>(G::W,G::H,PixelFormat32bppPARGB);G::paperActive=true;
    {Graphics g(G::videoSurface.get());g.Clear(Gdiplus::Color(224,30,60,90));}
    puts("Dynamic wallpaper: pre-composed panel frame + layered glass:");run(true);
    G::videoSurface.reset();G::paperActive=false;
    G::rowCache.clear();G::glassCache.reset();G::glassUnder.reset();G::glassOver.reset();G::iconCache.clear();G::imageCache.clear();ui::fontCache.clear();
    GdiplusShutdown(token);CoUninitialize();return 0;
}
