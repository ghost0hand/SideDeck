// Synthetic, offscreen benchmark. Never opens games, an overlay or a driver.
#define WinMain SideDeckEntry
#include "../src/main.cpp"
#undef WinMain
#include <chrono>
static void run(bool cached) {
    G::renderCaching=cached;G::rowCache.clear();G::glassCache.reset();
    Bitmap bitmap(G::W,G::H,PixelFormat32bppPARGB);Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);std::vector<double> times;
    for(int frame=0;frame<150;++frame) {
        G::scroll[0]=(frame%40)*4.0f;G::sel[0]=2+(frame/12)%4;
        auto start=std::chrono::steady_clock::now();graphics.Clear(Gdiplus::Color(0,0,0,0));drawPanel(graphics);
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
    G::rowCache.clear();G::glassCache.reset();G::iconCache.clear();G::imageCache.clear();ui::fontCache.clear();
    GdiplusShutdown(token);CoUninitialize();return 0;
}
