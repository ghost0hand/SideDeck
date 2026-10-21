// GDI+ 绘制原语 / 主题配色 / 背景模糊 / 图标缓存
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <objbase.h>
#include <wtypes.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
// MinGW 的 libuuid 没有 IID_IImageList，手动定义
namespace detail {
    inline const GUID kIID_IImageList =
        {0x46EB5926, 0x582E, 0x4414, {0xAC, 0x61, 0x7E, 0x00, 0x6E, 0xBA, 0x47, 0x62}};
}
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus")
#pragma comment(lib, "shell32")
#pragma comment(lib, "shlwapi")
#pragma comment(lib, "user32")
#pragma comment(lib, "gdi32")
#endif

namespace ui {

using namespace Gdiplus;

// ---------------- 主题 ----------------
struct Color {
    unsigned char r, g, b, a;
    Color(unsigned char r_ = 0, unsigned char g_ = 0, unsigned char b_ = 0, unsigned char a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
    Gdiplus::Color gd() const { return Gdiplus::Color(a, r, g, b); }
    Color withA(unsigned char na) const { return Color(r, g, b, na); }
    Color lerp(const Color& o, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        return Color((unsigned char)std::lround(r + (o.r - r) * t),
                     (unsigned char)std::lround(g + (o.g - g) * t),
                     (unsigned char)std::lround(b + (o.b - b) * t),
                     (unsigned char)std::lround(a + (o.a - a) * t));
    }
};

struct Theme {
    Color panel       { 16, 16, 24, 216 };   // 面板底色（叠在模糊背景上）
    Color panelTop    { 24, 22, 38, 235 };
    Color text        { 240, 240, 250, 255 };
    Color textDim     { 158, 158, 184, 255 };
    Color textFaint   { 110, 110, 138, 255 };
    Color accentA     { 124, 92, 255, 255 }; // 紫
    Color accentB     { 34, 211, 238, 255 }; // 青
    Color selBg       { 255, 255, 255, 22 };
    Color selBar      { 124, 92, 255, 255 };
    Color sep         { 255, 255, 255, 18 };
    Color chip        { 255, 255, 255, 26 };
    Color shadow      { 0, 0, 0, 120 };
    Color glassTop    { 12, 26, 40, 108 };
    Color glassBottom { 13, 24, 35, 158 };
    Color rim         { 230, 248, 255, 92 };
    Color reader      { 8, 20, 32, 72 };
    float backdropOpacity = 0.38f;
};

inline Theme& theme() { static Theme t; return t; }
inline constexpr int THEME_COUNT = 4;
inline const wchar_t* themeName(int id) {
    const wchar_t* names[] = {L"冰川玻璃", L"暮紫玻璃", L"烟灰玻璃", L"浅色磨砂"};
    return names[std::clamp(id, 0, THEME_COUNT - 1)];
}
inline const wchar_t* themeDescription(int id) {
    const wchar_t* descriptions[] = {L"清透冷蓝 · 轻盈通透", L"柔和紫调 · 夜间氛围", L"低饱和灰 · 专注阅读", L"柔白雾面 · 明亮清爽"};
    return descriptions[std::clamp(id, 0, THEME_COUNT - 1)];
}
inline Theme themePreset(int id) {
    Theme t;
    t.text = Color(242, 249, 255); t.textDim = Color(199, 215, 230); t.textFaint = Color(171, 192, 212);
    t.accentA = Color(111, 205, 255); t.accentB = Color(164, 236, 245);
    t.selBg = Color(236, 249, 255, 34); t.sep = Color(230, 249, 255, 42);
    t.chip = Color(239, 249, 255, 18); t.panel = Color(13, 24, 35, 175); t.panelTop = Color(23, 43, 60, 210);
    if (id == 1) {
        t.panelTop = Color(48, 35, 66, 232); t.panel = Color(30, 22, 42, 210);
        t.glassTop = Color(35, 22, 52, 112); t.glassBottom = Color(29, 22, 43, 162);
        t.accentA = Color(194, 172, 255); t.accentB = Color(247, 180, 223);
        t.textDim = Color(220, 205, 233); t.textFaint = Color(191, 174, 211);
        t.selBg = Color(246, 224, 255, 36); t.rim = Color(246, 225, 255, 90); t.reader = Color(29, 17, 43, 78);
    } else if (id == 2) {
        t.panelTop = Color(33, 38, 45, 238); t.panel = Color(18, 22, 28, 225);
        t.glassTop = Color(22, 25, 31, 145); t.glassBottom = Color(16, 19, 25, 190);
        t.accentA = Color(205, 220, 234); t.accentB = Color(158, 192, 207);
        t.textDim = Color(206, 213, 221); t.textFaint = Color(168, 180, 191);
        t.selBg = Color(243, 248, 255, 26); t.rim = Color(237, 244, 255, 65); t.reader = Color(13, 17, 24, 100);
        t.backdropOpacity = 0.30f;
    } else if (id == 3) {
        t.glassTop = Color(244, 249, 255, 176); t.glassBottom = Color(230, 240, 251, 201);
        t.text = Color(23, 42, 62); t.textDim = Color(55, 78, 100); t.textFaint = Color(78, 100, 120);
        t.accentA = Color(35, 112, 165); t.accentB = Color(28, 130, 143);
        t.selBg = Color(255, 255, 255, 150); t.sep = Color(255, 255, 255, 160);
        t.chip = Color(255, 255, 255, 90); t.rim = Color(255, 255, 255, 210); t.reader = Color(249, 252, 255, 96);
        t.panel = Color(237, 245, 254, 230); t.panelTop = Color(242, 248, 255, 245);
        t.backdropOpacity = 0.28f;
    }
    return t;
}
inline Theme mixTheme(const Theme& a, const Theme& b, float f) {
    Theme t;
#define MIX_COLOR(field) t.field = a.field.lerp(b.field, f)
    MIX_COLOR(panel); MIX_COLOR(panelTop); MIX_COLOR(text); MIX_COLOR(textDim); MIX_COLOR(textFaint);
    MIX_COLOR(accentA); MIX_COLOR(accentB); MIX_COLOR(selBg); MIX_COLOR(selBar); MIX_COLOR(sep);
    MIX_COLOR(chip); MIX_COLOR(shadow); MIX_COLOR(glassTop); MIX_COLOR(glassBottom); MIX_COLOR(rim); MIX_COLOR(reader);
#undef MIX_COLOR
    t.backdropOpacity = a.backdropOpacity + (b.backdropOpacity - a.backdropOpacity) * f;
    return t;
}

// ---------------- 工具 ----------------
inline float easeOutCubic(float t) { t = std::clamp(t, 0.0f, 1.0f); float u = 1 - t; return 1 - u * u * u; }
inline float easeOutQuint(float t) { t = std::clamp(t, 0.0f, 1.0f); float u = 1 - t; return 1 - u * u * u * u * u; }
inline float lerpF(float a, float b, float t) { return a + (b - a) * t; }

inline void roundRectPath(GraphicsPath& p, REAL x, REAL y, REAL w, REAL h, REAL r) {
    if (w <= 0 || h <= 0) return;
    r = std::min(r, std::min(w, h) * 0.5f);
    if (r < 0.5f) { p.AddRectangle(RectF(x, y, w, h)); return; }
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

// GDI+ 的 DrawImage 有 INT / REAL 两套重载，混用会歧义，统一走这里
inline void drawImage(Graphics& g, Image* img, float x, float y, float w, float h) {
    g.DrawImage(img, (REAL)x, (REAL)y, (REAL)w, (REAL)h);
}
// Cached UI is already rasterized at the destination resolution. Re-sampling
// it with bicubic interpolation costs more than drawing the original text.
inline void drawCachedBitmap(Graphics& g,Bitmap* bitmap,float x,float y,bool copy=false) {
    auto saved=g.Save();
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);g.SetPixelOffsetMode(PixelOffsetModeNone);
    g.SetCompositingQuality(CompositingQualityHighSpeed);
    if(copy)g.SetCompositingMode(CompositingModeSourceCopy);
    int width=(int)bitmap->GetWidth(),height=(int)bitmap->GetHeight();
    g.DrawImage(bitmap,Rect((int)roundf(x),(int)roundf(y),width,height),0,0,width,height,UnitPixel);
    g.Restore(saved);
}
inline void drawImageOpacity(Graphics& g, Image* img, float x, float y, float w, float h, float opacity) {
    ColorMatrix matrix = {1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,opacity,0, 0,0,0,0,1};
    ImageAttributes attrs;
    attrs.SetColorMatrix(&matrix);
    g.DrawImage(img, RectF(x, y, w, h), 0, 0, (REAL)img->GetWidth(), (REAL)img->GetHeight(), UnitPixel, &attrs);
}

inline void fillRoundRect(Graphics& g, const Color& c, float x, float y, float w, float h, float r) {
    GraphicsPath p; roundRectPath(p, x, y, w, h, r);
    SolidBrush b(c.gd());
    g.FillPath(&b, &p);
}

inline void drawRoundRect(Graphics& g, const Color& c, float x, float y, float w, float h, float r, float wd = 1.0f) {
    GraphicsPath p; roundRectPath(p, x, y, w, h, r);
    Pen pen(c.gd(), wd);
    g.DrawPath(&pen, &p);
}

inline std::wstring utf8(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}
inline std::string narrow(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

// ---------------- 文本 ----------------
struct TextStyle {
    float size = 15.0f;
    bool bold = false;
    Color color = theme().text;
    bool ellipsis = true;
    bool wrap = false;
    StringAlignment halign = StringAlignmentNear;
    StringAlignment valign = StringAlignmentCenter;
};

// 手持/串流场景整体放大字号
inline float g_textScale = 1.0f;
inline std::map<std::pair<float,int>,std::unique_ptr<Font>> fontCache;
inline Font* cachedFont(float size,bool bold) {
    auto key=std::make_pair(size*g_textScale,bold?FontStyleBold:FontStyleRegular);
    auto found=fontCache.find(key);if(found!=fontCache.end())return found->second.get();
    FontFamily family(L"Microsoft YaHei UI");auto font=std::make_unique<Font>(&family,key.first,key.second,UnitPixel);
    auto result=font.get();fontCache.emplace(key,std::move(font));return result;
}

inline void drawText(Graphics& g, const std::wstring& text, float x, float y, float w, float h, const TextStyle& st) {
    if (text.empty()) return;
    Font* font=cachedFont(st.size,st.bold);
    StringFormat sf(st.wrap ? 0 : StringFormatFlagsNoWrap);
    sf.SetAlignment(st.halign);
    sf.SetLineAlignment(st.valign);
    if (st.ellipsis && !st.wrap) sf.SetTrimming(StringTrimmingEllipsisCharacter);
    RectF rc(x, y, w, h);
    SolidBrush br(st.color.gd());
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.DrawString(text.c_str(), (int)text.size(), font, rc, &sf, &br);
}

inline float measureText(Graphics& g, const std::wstring& text, float size, bool bold = false) {
    RectF rc;
    g.MeasureString(text.c_str(), (int)text.size(), cachedFont(size,bold), PointF(0, 0), &rc);
    return rc.Width;
}

// ---------------- 模糊背景 ----------------
// 对 32bpp 位图数据做水平/垂直盒式模糊（就地，需临时缓冲）
inline void boxBlur32(BYTE* data, int w, int h, int radius) {
    if (w < 1 || h < 1 || radius < 1) return;
    std::vector<unsigned> tmp((size_t)w * h * 4);
    int stride = w * 4;
    // 水平
    for (int y = 0; y < h; y++) {
        BYTE* row = data + (size_t)y * stride;
        unsigned* out = &tmp[(size_t)y * w * 4];
        unsigned sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
        for (int x = -radius; x <= radius; x++) {
            int xx = std::clamp(x, 0, w - 1);
            BYTE* p = row + xx * 4;
            sr += p[0]; sg += p[1]; sb += p[2]; sa += p[3]; cnt++;
        }
        for (int x = 0; x < w; x++) {
            out[x * 4 + 0] = sr / cnt; out[x * 4 + 1] = sg / cnt;
            out[x * 4 + 2] = sb / cnt; out[x * 4 + 3] = sa / cnt;
            int xin = std::clamp(x + radius + 1, 0, w - 1);
            int xout = std::clamp(x - radius, 0, w - 1);
            BYTE* pi = row + xin * 4; BYTE* po = row + xout * 4;
            sr += pi[0] - po[0]; sg += pi[1] - po[1]; sb += pi[2] - po[2]; sa += pi[3] - po[3];
        }
    }
    // 垂直
    for (int x = 0; x < w; x++) {
        unsigned sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
        for (int y = -radius; y <= radius; y++) {
            int yy = std::clamp(y, 0, h - 1);
            unsigned* p = &tmp[((size_t)yy * w + x) * 4];
            sr += p[0]; sg += p[1]; sb += p[2]; sa += p[3]; cnt++;
        }
        for (int y = 0; y < h; y++) {
            BYTE* o = data + (size_t)y * stride + x * 4;
            o[0] = (BYTE)(sr / cnt); o[1] = (BYTE)(sg / cnt);
            o[2] = (BYTE)(sb / cnt); o[3] = (BYTE)(sa / cnt);
            int yin = std::clamp(y + radius + 1, 0, h - 1);
            int yout = std::clamp(y - radius, 0, h - 1);
            unsigned* pi = &tmp[((size_t)yin * w + x) * 4];
            unsigned* po = &tmp[((size_t)yout * w + x) * 4];
            sr += pi[0] - po[0]; sg += pi[1] - po[1]; sb += pi[2] - po[2]; sa += pi[3] - po[3];
        }
    }
}

inline Bitmap* blurBackdrop(Image* source, int w, int h, int blurRadius = 20, int downscale = 4) {
    if (!source || w <= 0 || h <= 0) return nullptr;
    downscale = std::max(1, downscale);
    int dw = std::max(1, w / downscale);
    int dh = std::max(1, h / downscale);
    Bitmap* small = new Bitmap(dw, dh, PixelFormat32bppARGB);
    {
        Graphics g(small);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        drawImage(g, source, 0, 0, (float)dw, (float)dh);
    }
    BitmapData bd;
    Rect rc(0, 0, dw, dh);
    if (small->LockBits(&rc, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
        // 行序：GDI+ 自上而下；boxBlur 对行序不敏感
        boxBlur32((BYTE*)bd.Scan0, dw, dh, std::max(1, blurRadius / downscale));
        // BitBlt 不写 alpha 字节，强制不透明
        BYTE* q = (BYTE*)bd.Scan0;
        for (int i = 3; i < dw * dh * 4; i += 4) q[i] = 255;
        small->UnlockBits(&bd);
    }
    Bitmap* out = new Bitmap(w, h, PixelFormat32bppARGB);
    {
        Graphics g(out);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        drawImage(g, small, 0, 0, (float)w, (float)h);
    }
    delete small;
    return out;
}

// Capture to an explicit RGB DIB: BitBlt leaves alpha unset, so repair it before GDI+ resampling.
inline Bitmap* grabBackdrop(int sx, int sy, int w, int h, int blurRadius = 20, int downscale = 4) {
    if (w <= 0 || h <= 0) return nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* data = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    Bitmap* result = nullptr;
    if (bitmap && memory && data) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        BOOL captured = BitBlt(memory, 0, 0, w, h, screen, sx, sy, SRCCOPY);
        GdiFlush();
        SelectObject(memory, previous);
        if (captured) {
            BYTE* pixels = (BYTE*)data;
            for (size_t i = 3; i < (size_t)w * h * 4; i += 4) pixels[i] = 255;
            Bitmap source(w, h, w * 4, PixelFormat32bppRGB, pixels);
            result = blurBackdrop(&source, w, h, blurRadius, downscale);
        }
    }
    if (memory) DeleteDC(memory);
    if (bitmap) DeleteObject(bitmap);
    ReleaseDC(nullptr, screen);
    return result;
}

// ---------------- 图标缓存 ----------------
class IconCache {
public:
    ~IconCache() { clear(); }
    void clear() {
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_.clear();}
        wake_.notify_all();if(worker_.joinable())worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : map_) delete kv.second;
        map_.clear();
        async_=false;
    }
    unsigned revision() const {return revision_;}
    void enableAsync() {
        if(async_)return;
        async_=true;stopping_=false;
        worker_=std::thread([this] {
            HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            for(;;) {
                Job job;
                {std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stopping_||!pending_.empty();});
                 if(stopping_)break;
                 job=std::move(pending_.front());pending_.pop_front();}
                Bitmap* bitmap=load(job.path,job.size);
                {std::lock_guard<std::mutex> lock(mutex_);if(stopping_){delete bitmap;break;}map_[job.key]=bitmap;++revision_;}
            }
            if(SUCCEEDED(com))CoUninitialize();
        });
    }
    // 返回缓存中的位图（64x64 或原始尺寸缩放到 size），失败返回 nullptr
    // 失败返回 nullptr（调用方可回退到别的路径）；nullptr 也缓存避免反复读盘
    Bitmap* get(const std::wstring& path, int size = 48) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wstring key = path + L"|" + std::to_wstring(size);
        auto it = map_.find(key);
        if (it != map_.end()) return it->second;
        if(async_) {map_[key]=nullptr;pending_.push_back({key,path,size});wake_.notify_one();return nullptr;}
        Bitmap* bmp = load(path, size);
        map_[key] = bmp;
        return bmp;
    }
    static Bitmap* fallback(int size) {
        Bitmap* b = new Bitmap(size, size, PixelFormat32bppARGB);
        Graphics g(b);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        fillRoundRect(g, theme().accentA.withA(150), 0, 0, (float)size, (float)size, size * 0.28f);
        return b;
    }

private:
    struct Job {std::wstring key,path;int size=48;};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> pending_;
    std::thread worker_;
    bool async_=false,stopping_=false;
    std::atomic<unsigned> revision_{0};
    std::map<std::wstring, Bitmap*> map_;

    // 48px 图标列表。JUMBO 常把小图标放在 256px 透明画布的一角，缩小后近乎消失。
    static Bitmap* loadJumbo(const std::wstring& path, int size) {
        SHFILEINFOW sfi{};
        DWORD flags = SHGFI_SYSICONINDEX;
        if (!PathFileExistsW(path.c_str())) flags |= SHGFI_USEFILEATTRIBUTES;
        if (!SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags)) return nullptr;
        IImageList* il = nullptr;
        if (SHGetImageList(SHIL_EXTRALARGE, detail::kIID_IImageList, (void**)&il) != S_OK) return nullptr;
        HICON hico = nullptr;
        HRESULT hr = il->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &hico);
        il->Release();
        if (hr != S_OK || !hico) return nullptr;
        Bitmap* bmp = Bitmap::FromHICON(hico);
        DestroyIcon(hico);
        if (!bmp || bmp->GetLastStatus() != Ok) { delete bmp; return nullptr; }
        Bitmap* r = scale(bmp, size);
        delete bmp;
        return r;
    }

    // cover 模式：裁源图中心正方形再缩放（Steam 封面是 460x215 横条，直接缩放会压扁）
    static Bitmap* cover(Bitmap* src, int size) {
        int sw = (int)src->GetWidth(), sh = (int)src->GetHeight();
        int side = std::min(sw, sh);
        int sx = (sw - side) / 2, sy = (sh - side) / 2;
        Bitmap cropped(side, side, PixelFormat32bppARGB);
        {
            Graphics g(&cropped);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            drawImage(g, src, (float)-sx, (float)-sy, (float)sw, (float)sh);
        }
        Bitmap* r = scale(&cropped, size);
        return r;
    }

    static Bitmap* load(const std::wstring& path, int size) {
        if (path.empty()) return nullptr;
        // 直接是图片文件
        std::wstring lower = path;
        for (auto& c : lower) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (lower.size() > 4) {
            auto dot = lower.find_last_of(L'.');
            std::wstring ext = dot == std::wstring::npos ? L"" : lower.substr(dot);
            if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".gif") {
                Bitmap* b = Bitmap::FromFile(path.c_str());
                if (b && b->GetLastStatus() == Ok && b->GetWidth() && b->GetHeight()) {
                    // 阅读器的 size=0 表示原图，保留比例与地图边缘。
                    if (size == 0) return b;
                    Bitmap* r = nullptr;
                    if (size < 0) {
                        // Negative size requests a contained thumbnail without cropping portrait art.
                        int side = -size;
                        r = new Bitmap(side, side, PixelFormat32bppPARGB);
                        Graphics g(r);
                        g.Clear(Gdiplus::Color(0, 0, 0, 0));
                        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                        float fit = std::min((float)side / b->GetWidth(), (float)side / b->GetHeight());
                        float bw = b->GetWidth() * fit, bh = b->GetHeight() * fit;
                        drawImage(g, b, (side - bw) / 2, (side - bh) / 2, bw, bh);
                    } else r = cover(b, size);
                    delete b;
                    return r;
                }
                delete b;
                return nullptr;
            }
        }
        if (size <= 0) return nullptr;
        // URL：无图标
        if (lower.rfind(L"http://", 0) == 0 || lower.rfind(L"https://", 0) == 0) return nullptr;

        if(lower.size()>=4 && lower.substr(lower.size()-4)==L".lnk") {
            // Avoid loading third-party Shell icon extensions into the panel.
            IShellLinkW* link=nullptr;Bitmap* result=nullptr;
            if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))) {
                IPersistFile* file=nullptr;
                if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
                    if(SUCCEEDED(file->Load(path.c_str(),STGM_READ))) {
                        wchar_t resource[32768]{};int index=0;
                        link->GetIconLocation(resource,32768,&index);
                        if(!resource[0]){link->GetPath(resource,32768,nullptr,SLGP_RAWPATH);index=0;}
                        wchar_t expanded[32768]{};ExpandEnvironmentStringsW(resource,expanded,32768);
                        HICON icon=nullptr;if(expanded[0])ExtractIconExW(expanded,index,&icon,nullptr,1);
                        if(icon){auto source=Bitmap::FromHICON(icon);DestroyIcon(icon);
                            if(source && source->GetLastStatus()==Ok)result=scale(source,size);
                            delete source;}
                    }
                    file->Release();
                }
                link->Release();
            }
            return result;
        }

        // 某些游戏 exe 能读取图标资源，但 Shell 图标服务返回失败。直接提取后再回退到 Shell。
        if (lower.size() >= 4 && (lower.substr(lower.size() - 4) == L".exe" || lower.substr(lower.size() - 4) == L".ico")) {
            HICON icon = nullptr;
            ExtractIconExW(path.c_str(), 0, &icon, nullptr, 1);
            if (icon) {
                Bitmap* source = Bitmap::FromHICON(icon);
                DestroyIcon(icon);
                if (source && source->GetLastStatus() == Ok) {
                    Bitmap* result = scale(source, size);
                    delete source;
                    return result;
                }
                delete source;
            }
        }

        Bitmap* jb = loadJumbo(path, size);
        if (jb) return jb;
        SHFILEINFOW sfi{};
        DWORD flags = SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES;
        if (PathFileExistsW(path.c_str())) flags = SHGFI_ICON | SHGFI_LARGEICON;
        if (!SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), flags)) return nullptr;
        if (!sfi.hIcon) return nullptr;
        Bitmap* bmp = nullptr;
        ICONINFO ii{};
        if (GetIconInfo(sfi.hIcon, &ii)) {
            // 用 GDI+ 直接从 HICON 构造（透明通道保留）
            bmp = Bitmap::FromHICON(sfi.hIcon);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
        }
        DestroyIcon(sfi.hIcon);
        if (!bmp || bmp->GetLastStatus() != Ok) { delete bmp; return nullptr; }
        Bitmap* r = scale(bmp, size);
        delete bmp;
        return r;
    }
    static Bitmap* scale(Bitmap* src, int size) {
        Bitmap* out = new Bitmap(size, size, PixelFormat32bppARGB);
        Graphics g(out);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        drawImage(g, src, 0, 0, (float)size, (float)size);
        return out;
    }
};

// 圆角裁剪绘制图标（保存并恢复外层裁剪区域）
inline void drawIcon(Graphics& g, Bitmap* bmp, float x, float y, float size, float radius) {
    if (!bmp) {
        fillRoundRect(g, theme().chip, x, y, size, size, radius);
        return;
    }
    Region outer;
    g.GetClip(&outer);
    GraphicsPath p; roundRectPath(p, x, y, size, size, radius);
    g.SetClip(&p, CombineModeIntersect);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    drawImage(g, bmp, x, y, size, size);
    g.SetClip(&outer, CombineModeReplace);
}

enum class ToolGlyph { None, Gamepad, Refresh, Folder, Book, Settings };

// Built-in tools use vector glyphs, independent of Shell file associations and icon fonts.
inline void drawToolGlyph(Graphics& g, ToolGlyph glyph, float x, float y, float size, bool selected) {
    Color accent = theme().accentA;
    switch (glyph) {
        case ToolGlyph::Gamepad: accent = Color(167, 139, 250); break;
        case ToolGlyph::Refresh: accent = Color(52, 211, 153); break;
        case ToolGlyph::Folder: accent = Color(251, 191, 75); break;
        case ToolGlyph::Book: accent = theme().accentB; break;
        case ToolGlyph::Settings: accent = Color(244, 145, 190); break;
        default: return;
    }
    accent = accent.lerp(Color(35, 67, 95), std::clamp((128.0f - theme().text.r) / 128.0f, 0.0f, 0.65f));
    fillRoundRect(g, accent.withA(selected ? 42 : 24), x, y, size, size, size * 0.2f);
    drawRoundRect(g, accent.withA(65), x, y, size, size, size * 0.2f, 1);
    auto state = g.Save();
    g.TranslateTransform(x + size * 0.17f, y + size * 0.17f);
    g.ScaleTransform(size * 0.66f / 32, size * 0.66f / 32);
    Pen pen(accent.gd(), 2.0f);
    pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound); pen.SetLineJoin(LineJoinRound);
    auto line = [&](float x1, float y1, float x2, float y2) { g.DrawLine(&pen, x1, y1, x2, y2); };
    if (glyph == ToolGlyph::Gamepad) {
        GraphicsPath shape;
        shape.StartFigure();
        shape.AddBezier(9, 9, 5, 9, 4, 13, 3, 22);
        shape.AddBezier(3, 22, 2, 29, 7, 28, 11, 22);
        shape.AddLine(11, 22, 21, 22);
        shape.AddBezier(21, 22, 25, 28, 30, 29, 29, 22);
        shape.AddBezier(29, 22, 28, 13, 27, 9, 23, 9);
        shape.CloseFigure();
        g.DrawPath(&pen, &shape);
        line(8, 16, 14, 16); line(11, 13, 11, 19);
        SolidBrush brush(accent.gd());
        g.FillEllipse(&brush, 22.0f, 12.0f, 2.5f, 2.5f);
        g.FillEllipse(&brush, 25.0f, 16.0f, 2.5f, 2.5f);
        line(14, 5, 18, 5);
    } else if (glyph == ToolGlyph::Refresh) {
        g.DrawArc(&pen, 5.0f, 5.0f, 22.0f, 22.0f, 45.0f, 280.0f);
        line(25, 9, 20, 8); line(25, 9, 25, 14);
    } else if (glyph == ToolGlyph::Folder) {
        GraphicsPath shape;
        const PointF points[] = {{4, 11}, {4, 7}, {13, 7}, {16, 11}, {28, 11}, {28, 25}, {4, 25}, {4, 11}, {28, 11}};
        shape.AddLines(points, 9);
        g.DrawPath(&pen, &shape);
        line(9, 17, 23, 17);
    } else if (glyph == ToolGlyph::Book) {
        GraphicsPath book;
        book.StartFigure();
        book.AddBezier(16, 9, 12, 6, 8, 6, 4, 7);
        book.AddLine(4, 7, 4, 25);
        book.AddBezier(4, 25, 8, 24, 12, 24, 16, 27);
        book.AddBezier(16, 27, 20, 24, 24, 24, 28, 25);
        book.AddLine(28, 25, 28, 7);
        book.AddBezier(28, 7, 24, 6, 20, 6, 16, 9);
        book.CloseFigure();
        g.DrawPath(&pen, &book);
        line(16, 9, 16, 27); line(8, 12, 12, 13); line(20, 13, 24, 12);
    } else if (glyph == ToolGlyph::Settings) {
        PointF points[48];
        for (int i = 0; i < 48; ++i) {
            float angle = (float)i * 6.283185307f / 48;
            float radius = (i % 6 == 0 || i % 6 == 5) ? 10.0f : 12.5f;
            points[i] = PointF(16 + cosf(angle) * radius, 16 + sinf(angle) * radius);
        }
        g.DrawPolygon(&pen, points, 48);
        g.DrawEllipse(&pen, 11.5f, 11.5f, 9.0f, 9.0f);
    }
    g.Restore(state);
}

} // namespace ui
