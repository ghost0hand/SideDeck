// SideDeck —— 手柄呼出的侧边栏（游戏库 / 攻略 / 快捷动作）
// 构建：见 build.sh（MinGW-w64, 静态链接, 单文件 exe）
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <objbase.h>
#include <wtypes.h>
#include <propidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cmath>
#include <thread>
#include <cstdio>
#include <sidedeck_version.hpp>
#ifdef DEBUG_LOG
#define LOG(m) do { fprintf(stderr, "[LOG] %s\n", m); fflush(stderr); } while(0)
#define LOGW(m) do { fwprintf(stderr, L"[LOG] %ls\n", m); fflush(stderr); } while(0)
#else
#define LOG(m) do{}while(0)
#define LOGW(m) do{}while(0)
#endif
#include "json.hpp"
#include "render.hpp"
#include "gamepad.hpp"
#include "library.hpp"
#include "focus_guard.hpp"
#include "input_bridge.hpp"
#include "library_index.hpp"
#include "touch_scroll.hpp"
#include "themes.hpp"
#include "video_wallpaper.hpp"
#include "frame_clock.hpp"
#include "launch_dispatch.hpp"

using namespace Gdiplus;
using lib::Item;
using lib::Tab;
using lib::Config;

#ifndef WM_TRAYICON
#define WM_TRAYICON (WM_USER + 100)
#endif

// ==================== 全局状态 ====================
namespace G {
Config cfg;
std::vector<Tab> tabs;
std::vector<Item> allGames;
bool favoritesOnly=false,jumpPicker=false;
int jumpIndex=0;
std::vector<RectF> libraryActions,jumpCells;
std::wstring jumpLetters=L"ABCDEFGHIJKLMNOPQRSTUVWXYZ#";
bool jumpAvailable[27]{};
std::map<std::wstring,std::unique_ptr<Bitmap>> rowCache;
std::deque<std::wstring> rowOrder;   // 插入顺序，按字节预算淘汰
size_t rowBytes=0;
constexpr size_t ROW_CACHE_BUDGET=24ull*1024*1024;  // 卡片缓存上限：4K 下 128 张会到 100MB 以上
std::unique_ptr<Bitmap> glassCache;      // 无动态壁纸时：背景+渐变合成成一张
std::unique_ptr<Bitmap> glassUnder;      // 动态壁纸时：背景模糊层（在视频下面）
std::unique_ptr<Bitmap> glassOver;       // 动态壁纸时：渐变+光泽（在视频上面）
int glassWidth=0,glassHeight=0,glassTheme=-1;
int glassUnderW=0,glassUnderH=0,glassUnderTheme=-1;
unsigned glassRebuilds=0;   // 玻璃层重建次数（性能诊断/回归用）
unsigned rowRevision=0;
bool renderCaching=true;
bool glassDirty=false;
touchscroll::Gesture touch;
bool freeScroll=false;
UINT touchPointer=0;
ULONGLONG nativeTouchTick=0;
bool cancelledMouseTap=false;
int tabIndex = 0;
std::vector<int> sel;
std::vector<float> scroll, scrollTarget;
ui::IconCache iconCache, imageCache;

HWND hwnd = nullptr;
HINSTANCE hInst = nullptr;
gp::Input pad;
isolation::Bridge bridge;

bool visible = false;
bool closing = false;
float anim = 0.0f;          // 0..1
ULONGLONG animTick = 0;
double animTime=0;
float pageAnim = 1, pageDirection = 1;
float focusY = 0, focusTarget = 0;
int focusTab = -1;
bool focusReady = false;
float tabPillX = 0, tabPillW = 0, tabPillTargetX = 0, tabPillTargetW = 0;
bool pillReady = false;
bool themePicker = false;
int themePick = 0;
bool glassPicker = false;      // 玻璃参数面板
int glassPick = 0;
std::vector<RectF> glassRows;
RectF glassSheet;
// 长按移除：手柄 Y 或键盘 Delete
bool removeConfirm = false;
Item removeItem;
int removeIndex = 0;
std::vector<RectF> removeActions;
RectF removeSheet;
bool holdArmed = false;
WORD holdButton = 0;
DWORD holdStart = 0;
float holdProgress = 0;
ULONGLONG deleteKeyDown = 0;
std::unique_ptr<Bitmap> themeImage,videoSurface;
bool paperActive=false;      // 动态壁纸已出帧（此时插画让位给视频）
// 背景抓屏在后台线程完成，结果通过消息回投，不阻塞呼出动画。
std::mutex backdropMutex;
std::unique_ptr<Bitmap> backdropResult;
int backdropResultW=0,backdropResultH=0;
std::atomic<bool> backdropBusy{false};
// 抓屏线程必须在退出前 join：析构时兜底，避免任何路径漏掉 join 触发 std::terminate。
struct BackdropWorker {
    std::thread thread;
    ~BackdropWorker() { if (thread.joinable()) thread.join(); }
    bool busy() const { return thread.joinable(); }
    void join() { if (thread.joinable()) thread.join(); }
    void start(std::thread&& worker) { join(); thread = std::move(worker); }
};
BackdropWorker backdropWorker;
wallpaper::Video video;
bool videoErrorShown=false;
ui::Theme themeFrom;
float themeProgress = 1;
bool animating = false;
ui::Bitmap* backdrop = nullptr;

int view = 0;               // 0=列表 1=阅读
std::wstring readerTitle;
std::wstring readerPath;
float readerScroll = 0, readerTarget = 0;
float readerMaxScroll=0;
bool captureHotkey = false; // 正在录制呼出键
WORD recordedMask = 0;
bool recordArmed = false;
bool waitForRelease = false;
HWND foregroundAtShow = nullptr;
HWND lastExternalForeground = nullptr;
focusguard::Guard inputFocus;
bool restoreOnClose = false, waitingForRelease = false, padNeutral = true, dialogOpen = false;
HANDLE launchProcess=nullptr;
lib::Item launchingItem;
std::vector<RectF> tabRects;
int htmlTabIndex = -1;
std::wstring toastMsg;
float toastT = 0;

int W = 460, H = 900, PX = 0, PY = 0;  // 面板几何
HBITMAP hDib = nullptr;
BYTE* dibBits = nullptr;
HDC hdcMem = nullptr;
int dibW = 0, dibH = 0;

HMENU hTrayMenu = nullptr;
NOTIFYICONDATAW nid{};
bool trayAdded = false;

// 手柄边沿检测
WORD prevBtn = 0;
WORD curBtn = 0;
DWORD backDownMs = 0;
DWORD lastRepeat = 0;
bool stickConsumed = false;
DWORD lastPoll = 0;
int padCount = 0;
int triggerDirection=0;
DWORD triggerRepeat=0;
bool rescanRequested = false;
float screenScale = 1.0f; // 分辨率/DPI 系数，由 computeGeometry 刷新；1080p 上为 1.0
bool dirty = false;
}

// ==================== 微小控件：Markdown 简化 ====================
enum MdType { MD_TEXT = 0, MD_H1, MD_H2, MD_H3, MD_BULLET, MD_QUOTE, MD_IMAGE, MD_CODE, MD_SPACER };
struct MdLine { int type = MD_TEXT; std::wstring text; float size = 14.0f; };

static std::wstring stripMarks(std::wstring line) {
    std::wstring s;
    for (wchar_t c : line) if (c != L'*' && c != L'`') s += c;
    return s;
}

static std::vector<MdLine> parseMarkdown(const std::string& raw) {
    std::vector<MdLine> out;
    std::wstring w = ui::utf8(raw);
    // 统一换行
    std::wstring t;
    for (wchar_t c : w) { if (c == L'\r') continue; t += c; }
    size_t i = 0;
    bool prevBlank = true;
    while (i <= t.size()) {
        size_t eol = t.find(L'\n', i);
        if (eol == std::wstring::npos) eol = t.size();
        std::wstring line = t.substr(i, eol - i);
        i = eol + 1;
        if (i > t.size() && eol == t.size()) { /* done after */ }

        // trim
        size_t a = line.find_first_not_of(L" \t");
        if (a == std::wstring::npos) { out.push_back({MD_SPACER, L"", 6.0f}); prevBlank = true; if (eol == t.size()) break; continue; }
        line = line.substr(a);
        size_t b = line.find_last_not_of(L" \t");
        if (b != std::wstring::npos) line = line.substr(0, b + 1);

        MdLine m;
        if (line.rfind(L"### ", 0) == 0) { m.type = MD_H3; m.text = line.substr(4); m.size = 15.0f; }
        else if (line.rfind(L"## ", 0) == 0) { m.type = MD_H2; m.text = line.substr(3); m.size = 18.0f; }
        else if (line.rfind(L"# ", 0) == 0) { m.type = MD_H1; m.text = line.substr(2); m.size = 22.0f; }
        else if (line.rfind(L"![", 0) == 0 && line.find(L"](") != std::wstring::npos && line.back() == L')') {
            m.type = MD_IMAGE;
            size_t p = line.find(L"](");
            if (p != std::wstring::npos) m.text = line.substr(p + 2, line.rfind(L')') - p - 2);
        }
        else if (line.rfind(L"> ", 0) == 0) { m.type = MD_QUOTE; m.text = stripMarks(line.substr(2)); m.size = 13.0f; }
        else if (line.rfind(L"- ", 0) == 0 || line.rfind(L"* ", 0) == 0) { m.type = MD_BULLET; m.text = stripMarks(line.substr(2)); m.size = 14.0f; }
        else if (line.rfind(L"```", 0) == 0) { m.type = MD_CODE; m.text = L""; m.size = 10.0f; }
        else {
            m.type = MD_TEXT;
            m.text = stripMarks(line); m.size = 14.0f;
        }
        out.push_back(m);
        prevBlank = false;
        if (eol == t.size()) break;
    }
    return out;
}
static std::vector<MdLine> g_doc;

// ==================== 工具 ====================
static void toast(const std::wstring& msg) { G::toastMsg = msg; G::toastT = 1.0f; G::dirty = true; }

static void releaseDib() {
    // 先销毁 DC，解除其对位图的选择，再删除位图。
    if (G::hdcMem) DeleteDC(G::hdcMem);
    if (G::hDib) DeleteObject(G::hDib);
    G::hdcMem = nullptr; G::hDib = nullptr; G::dibBits = nullptr;
    G::dibW = G::dibH = 0;
}

static void ensureDib(int w, int h) {
    if (G::hDib && G::dibW == w && G::dibH == h) return;
    if (w <= 0 || h <= 0) return;
    releaseDib();
    HDC scr = GetDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    G::hDib = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    G::dibBits = (BYTE*)bits;
    G::hdcMem = CreateCompatibleDC(scr);
    SelectObject(G::hdcMem, G::hDib);
    ReleaseDC(nullptr, scr);
    G::dibW = w; G::dibH = h;
}

static void showPanel();
static void tickGamepad();
static void hidePanel(bool restoreFocus = true);
static void rebuildTabs();
static void saveCfg();
static void applyTheme(int id);
static void importGames();
static void configureIsolation();
static void toggleFavorite();
static void toggleFavoritesOnly();
static void openJumpPicker();
static void jumpTo(wchar_t letter);
static void refreshGameView();
static void cancelTouch(HWND window=nullptr);
static void requestBackdrop();

// ==================== 绘制 ====================
// UI 缩放 = 用户倍率 × 屏幕系数。
// 屏幕系数以「1080p 可用高度」为基准，让面板始终占屏幕的同一比例：
// 1080p 上等于 1.0（外观和以前完全一致），1440p ≈ 1.35，4K ≈ 2.05。
// 系统缩放（比如 4K 笔记本开 300%）还能把系数继续抬高，尊重用户的无障碍设置。
static float S() { return G::cfg.scale * G::screenScale; }
// 把主题自带的透明度按玻璃参数缩放；1.0 时等于主题原样。
static ui::Theme glassAdjusted(ui::Theme palette) {
    auto scaleAlpha = [](unsigned char value, float factor) {
        return (unsigned char)std::clamp((int)lroundf(value * factor), 0, 255);
    };
    palette.backdropOpacity = std::clamp(palette.backdropOpacity * G::cfg.glassOpacity, 0.02f, 1.0f);
    palette.glassTop.a = scaleAlpha(palette.glassTop.a, G::cfg.glassTint);
    palette.glassBottom.a = scaleAlpha(palette.glassBottom.a, G::cfg.glassTint);
    palette.panel.a = scaleAlpha(palette.panel.a, G::cfg.glassTint);
    palette.rim.a = scaleAlpha(palette.rim.a, 0.5f + 0.5f * G::cfg.glassSheen);
    return palette;
}
static void setPalette(const ui::Theme& palette) { ui::theme() = glassAdjusted(palette); }
static float panelRadius() { return 22.0f * S() * G::cfg.glassRadius; }
#define PADX     (20.0f * S())
#define HEADER_H (80.0f * S())
#define TAB_H    (46.0f * S())
#define FOOTER_H (48.0f * S())
#define ITEM_H   (72.0f * S())
#define ITEM_GAP (10.0f * S())

static float listTop() { return HEADER_H + TAB_H + 12 * S() + (G::view==0 && G::tabIndex==0 ? 36*S() : 0); }
static float listBottom() { return G::H - FOOTER_H - 10 * S(); }

static void paintItemCard(Graphics& g, const Item& it, float x, float y, float w, float h, bool selected, float appear) {
    auto& T = ui::theme();
    float pad = 12.0f * S();
    float iconS = h - pad * 2;
    float ix = x + pad, iy = y + pad;
    ui::ToolGlyph glyph = ui::ToolGlyph::None;
    if (it.kind == lib::Command) {
        if (it.target == L"__theme__" || it.target==L"__importtheme__" || it.target==L"__themedir__") glyph = ui::ToolGlyph::Settings;
        else if (it.target == L"__isolation__") glyph = ui::ToolGlyph::Gamepad;
        else if (it.target == L"__hotkey__") glyph = ui::ToolGlyph::Gamepad;
        else if (it.target == L"__addgames__") glyph = ui::ToolGlyph::Folder;
        else if (it.target == L"__rescan__") glyph = ui::ToolGlyph::Refresh;
        else if (it.target == L"__opendir__") glyph = ui::ToolGlyph::Folder;
        else if (it.target == L"__openguides__") glyph = ui::ToolGlyph::Book;
        else if (it.target == L"__editconfig__") glyph = ui::ToolGlyph::Settings;
        else if (it.target == L"__uiscale__") glyph = ui::ToolGlyph::Scaling;
        else if (it.target == L"__glass__") glyph = ui::ToolGlyph::Glass;
    }
    auto& cache = G::iconCache;
    std::wstring ip = it.iconPath.empty() ? it.target : it.iconPath;
    Bitmap* bmp = glyph != ui::ToolGlyph::None ? nullptr : it.kind == lib::Guide ? (it.iconPath.empty() ? nullptr : cache.get(ip, -(int)(48.0f * S()))) : cache.get(ip, (int)(48.0f * S()));
    if (!bmp && it.kind != lib::Guide && !it.iconPath.empty() && it.iconPath != it.target)
        bmp = cache.get(it.target, (int)(48.0f * S()));   // 封面缺失 → 回退 exe 图标
    if (glyph != ui::ToolGlyph::None) {
        ui::drawToolGlyph(g, glyph, ix, iy, iconS, selected);
    } else if (it.kind == lib::Guide && !bmp) {
        // 攻略用一个文档图标色块
        ui::fillRoundRect(g, T.accentB.withA(24), ix, iy, iconS, iconS, 12 * S());
        ui::drawRoundRect(g, T.accentB.withA(120), ix, iy, iconS, iconS, 10.0f, 1.2f);
        ui::TextStyle ts; ts.size = 13; ts.bold = true; ts.color = T.accentB;
        ts.halign = StringAlignmentCenter;
        ui::drawText(g, L"攻略", ix, iy, iconS, iconS, ts);
    } else if (it.kind == lib::Guide) {
        ui::fillRoundRect(g, T.chip, ix, iy, iconS, iconS, 10 * S());
        float fit = std::min(iconS / bmp->GetWidth(), iconS / bmp->GetHeight());
        float bw = bmp->GetWidth() * fit, bh = bmp->GetHeight() * fit;
        ui::drawImage(g, bmp, ix + (iconS - bw) / 2, iy + (iconS - bh) / 2, bw, bh);
    } else {
        ui::drawIcon(g, bmp, ix, iy, iconS, 10.0f);
        ui::drawRoundRect(g, ui::Color(255, 255, 255, 26), ix, iy, iconS, iconS, 10.0f, 1.0f);
    }
    float tx = ix + iconS + 12 * S();
    float tw = (x + w) - tx - pad;
    if(it.pinned) {
        ui::TextStyle star;star.size=16;star.color=T.accentB;star.halign=StringAlignmentCenter;
        ui::drawText(g,L"★",x+w-pad-22*S(),y+pad,24*S(),28*S(),star);tw-=26*S();
    }
    float titleH = 24 * S(), subH = 18 * S(), gap = 4 * S();
    float textY = y + (h - titleH - gap - subH) / 2;
    ui::TextStyle t1; t1.size = 14.5f; t1.bold = selected;
    t1.color = selected ? T.text : T.text;
    ui::drawText(g, it.title, tx, textY, tw, titleH, t1);
    ui::TextStyle t2; t2.size = 11.5f; t2.color = T.textFaint;
    std::wstring sub = it.subtitle.empty() ? it.target : it.subtitle;
    ui::drawText(g, sub, tx, textY + titleH + gap, tw, subH, t2);
}
static void clearRowCache() {
    G::rowCache.clear();G::rowOrder.clear();G::rowBytes=0;
}
static void drawItemCard(Graphics& g,const Item& item,float x,float y,float w,float h,bool selected,float appear) {
    if(!G::renderCaching || G::themeProgress<1) {paintItemCard(g,item,x,y,w,h,selected,appear);return;}
    unsigned revision=G::iconCache.revision();
    if(revision!=G::rowRevision){clearRowCache();G::rowRevision=revision;}
    std::wstring key=item.title+L"\n"+item.subtitle+L"\n"+item.target+L"\n"+item.iconPath+L"|"+
        std::to_wstring(item.kind)+L"|"+std::to_wstring(item.pinned)+L"|"+std::to_wstring(selected)+L"|"+
        std::to_wstring(G::cfg.theme)+L"|"+std::to_wstring(S())+L"|"+std::to_wstring(w)+L"|"+std::to_wstring(h);
    auto found=G::rowCache.find(key);
    if(found==G::rowCache.end()) {
        size_t bytes=(size_t)ceilf(w)*(size_t)ceilf(h)*4;
        // 按字节而不是条目数淘汰：4K/高缩放下单张卡片就有几百 KB。
        while(!G::rowOrder.empty() && G::rowBytes+bytes>G::ROW_CACHE_BUDGET) {
            auto oldest=std::move(G::rowOrder.front());G::rowOrder.pop_front();
            auto victim=G::rowCache.find(oldest);
            if(victim==G::rowCache.end())continue;
            if(victim->second)G::rowBytes-=std::min(G::rowBytes,(size_t)victim->second->GetWidth()*victim->second->GetHeight()*4);
            G::rowCache.erase(victim);
        }
        auto bitmap=std::make_unique<Bitmap>((int)ceilf(w),(int)ceilf(h),PixelFormat32bppPARGB);
        {Graphics buffer(bitmap.get());buffer.Clear(Gdiplus::Color(0,0,0,0));buffer.SetSmoothingMode(SmoothingModeHighQuality);paintItemCard(buffer,item,0,0,w,h,selected,appear);}
        found=G::rowCache.emplace(key,std::move(bitmap)).first;
        G::rowOrder.push_back(key);G::rowBytes+=bytes;
    }
    ui::drawCachedBitmap(g,found->second.get(),x,y);
}

static void drawReader(Graphics& g, float w, float h) {
    auto& T = ui::theme();
    float top = HEADER_H;
    // 标题栏
    ui::TextStyle tt; tt.size = 17; tt.bold = true; tt.color = T.text;
    ui::drawText(g, G::readerTitle, PADX, 18 * S(), w - PADX * 2, 40 * S(), tt);
    ui::fillRoundRect(g, T.chip, PADX, top - 6, 90 * S(), 22 * S(), 11);
    ui::TextStyle bt; bt.size = 11.5f; bt.color = T.textDim;
    bt.halign = StringAlignmentCenter;
    ui::drawText(g, L"← B 返回", PADX, top - 6, 90 * S(), 22 * S(), bt);

    // 内容区裁剪
    RectF clip(PADX * 0.5f, top + 32 * S(), w - PADX, h - top - 32 * S() - FOOTER_H);
    ui::fillRoundRect(g, T.reader, clip.X, clip.Y - 3 * S(), clip.Width, clip.Height + 3 * S(), 18 * S());
    auto saved = g.Save();
    g.SetClip(clip, CombineModeIntersect);
    g.TranslateTransform(0, (1 - ui::easeOutCubic(G::pageAnim)) * 10 * S());
    auto& imgCache = G::imageCache;
    float y = clip.Y + 8 * S() - G::readerScroll;
    float cw = clip.Width - PADX;
    for (auto& m : g_doc) {
        if (m.type == MD_SPACER) { y += m.size; continue; }
        if (m.type == MD_IMAGE) {
            std::wstring p = m.text;
            std::wstring full = (std::filesystem::path(G::readerPath).parent_path() / p).lexically_normal().wstring();
            if (lib::fileExists(full)) {
                Bitmap* b = imgCache.get(full, 0);
                if (b) {
                    float bw = std::min((float)b->GetWidth(), cw);
                    float bh = bw * (float)b->GetHeight() / (float)b->GetWidth();
                    float maxHeight = std::min(320 * S(), clip.Height * 0.7f);
                    if (b->GetHeight() > b->GetWidth() && bh > maxHeight) {
                        bw *= maxHeight / bh; bh = maxHeight;
                    }
                    if (y + bh >= clip.Y && y <= clip.GetBottom()) ui::drawImage(g, b, PADX + (cw - bw) / 2, y, bw, bh);
                    y += bh + 14;
                    continue;
                }
            }
        }
        ui::TextStyle ts;
        ts.size = m.size * S();
        ts.ellipsis = false;
        float indent = PADX;
        float tw = cw;
        switch (m.type) {
            case MD_H1: ts.bold = true; ts.color = T.text; indent = PADX; break;
            case MD_H2: ts.bold = true; ts.color = T.accentB; break;
            case MD_H3: ts.bold = true; ts.color = T.accentA; break;
            case MD_QUOTE: ts.color = T.textDim; indent = PADX + 10; tw -= 10; break;
            case MD_BULLET: ts.color = T.text; indent = PADX + 14; tw -= 14; break;
            case MD_CODE: ts.color = T.textDim; break;
            default: ts.color = T.text; break;
        }
        // 自动换行
        std::wstring s = m.text;
        if (m.type == MD_BULLET) s = L"• " + s;
        if (m.type == MD_QUOTE) {
            ui::fillRoundRect(g, T.accentA.withA(60), PADX, y, 3, ts.size * 1.6f, 1.5f);
        }
        FontFamily fam(L"Microsoft YaHei UI");
        Font font(&fam, ts.size, ts.bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        RectF rc(indent, y, tw, 1000);
        RectF needed;
        g.MeasureString(s.c_str(), (int)s.size(), &font, rc, &needed);
        SolidBrush br(ts.color.gd());
        g.DrawString(s.c_str(), (int)s.size(), &font, rc, nullptr, &br);
        y += needed.Height + (m.type == MD_H1 || m.type == MD_H2 ? 12.0f : 6.0f);
    }
    g.Restore(saved);
    G::readerMaxScroll=std::max(0.0f,y+G::readerScroll-clip.GetBottom());
    G::readerTarget = std::clamp(G::readerTarget, 0.0f, G::readerMaxScroll);
    G::readerScroll = std::clamp(G::readerScroll,0.0f,G::readerMaxScroll);
}

static std::filesystem::path themeDirectory(){return std::filesystem::path(lib::appDir())/L"themes";}
static int currentTheme(){return themes::selected(G::cfg.theme,G::cfg.themeFile);}
static const themes::Entry& currentThemeEntry(){return themes::get(currentTheme());}
static void loadThemeImage(){
    G::video.close();G::videoSurface.reset();G::paperActive=false;G::videoErrorShown=false;
    G::themeImage.reset();clearRowCache();G::glassCache.reset();
    G::glassUnder.reset();G::glassOver.reset();G::glassDirty=true;
    const auto& entry=currentThemeEntry();
    G::video.open(entry.video.wstring());
    // 缩放目标下发给解码线程，面板尺寸/主题变化都走这里。
    G::video.target(G::W,G::H,entry.videoFit,entry.imageOpacity);
    if(!entry.image.empty()){
        auto image=std::make_unique<Bitmap>(entry.image.c_str());
        if(image->GetLastStatus()==Ok)G::themeImage=std::move(image);
    }
}
static constexpr int THEME_PAGE_ROWS=4;
// ==================== 玻璃效果参数 ====================
static const float kGlassBlurSteps[]={0.4f,0.7f,1.0f,1.4f,1.8f,2.2f};
static const float kGlassOpacitySteps[]={0.5f,0.75f,1.0f,1.25f};
static const float kGlassTintSteps[]={0.6f,0.8f,1.0f,1.2f,1.4f};
static const float kGlassSheenSteps[]={0.4f,0.7f,1.0f,1.4f,1.8f};
static const float kGlassRadiusSteps[]={0.6f,0.8f,1.0f,1.3f,1.6f};
struct GlassRowDef {
    const wchar_t* name;
    const wchar_t* hint;
    float* value;
    const float* steps;
    int count;
    bool needsBackdrop;
};
static GlassRowDef gGlassRows[] = {
    {L"背景模糊", L"面板背后画面的虚化程度", &G::cfg.glassBlur, kGlassBlurSteps, 6, true},
    {L"背景可见", L"背景画面透过玻璃的强度", &G::cfg.glassOpacity, kGlassOpacitySteps, 4, false},
    {L"玻璃浓度", L"玻璃渐变与底色的厚度", &G::cfg.glassTint, kGlassTintSteps, 5, false},
    {L"顶部光泽", L"高光与边框的锐利程度", &G::cfg.glassSheen, kGlassSheenSteps, 5, false},
    {L"面板圆角", L"面板与裁切的圆润程度", &G::cfg.glassRadius, kGlassRadiusSteps, 5, false},
};
static constexpr int GLASS_PARAMS = (int)(sizeof(gGlassRows) / sizeof(gGlassRows[0]));
static constexpr int GLASS_ROWS = GLASS_PARAMS + 1;   // 末行是「恢复默认」
static int glassStepIndex(const GlassRowDef& row) {
    int best = 0; float distance = 1e9f;
    for (int i = 0; i < row.count; ++i) {
        float delta = fabsf(row.steps[i] - *row.value);
        if (delta < distance) { distance = delta; best = i; }
    }
    return best;
}
static std::wstring glassValueText(const GlassRowDef& row) {
    wchar_t buf[32]{};
    swprintf(buf, 32, L"%.2f", *row.value);
    return buf;
}
static std::wstring glassSummary() {
    wchar_t buf[128]{};
    swprintf(buf, 128, L"模糊 %.2f · 浓度 %.2f · 圆角 %.2f · A 调整",
        G::cfg.glassBlur, G::cfg.glassTint, G::cfg.glassRadius);
    return buf;
}
// 主题过渡动画也会写 ui::theme()，所以这里始终从「原始调色板」重新套用参数。
static void refreshGlassPalette() {
    setPalette(ui::mixTheme(G::themeFrom, currentThemeEntry().palette, ui::easeOutCubic(G::themeProgress)));
}
static void refreshGlassLayers() {
    G::glassCache.reset(); G::glassUnder.reset(); G::glassOver.reset(); G::glassDirty = true;
    G::dirty = true;
}
static void openGlassPicker() {
    if (G::view != 0 || G::themePicker || G::jumpPicker || G::removeConfirm) return;
    G::glassPicker = true; G::glassPick = 0; G::dirty = true;
}
static void closeGlassPicker() { G::glassPicker = false; G::dirty = true; }
static void resetGlass() {
    for (int i = 0; i < GLASS_PARAMS; ++i) *gGlassRows[i].value = 1.0f;
    saveCfg(); refreshGlassPalette(); refreshGlassLayers(); requestBackdrop();
    toast(L"玻璃效果已恢复默认");
}
static void adjustGlass(int delta) {
    if (G::glassPick < 0 || G::glassPick >= GLASS_PARAMS) return;
    auto& row = gGlassRows[G::glassPick];
    int next = std::clamp(glassStepIndex(row) + delta, 0, row.count - 1);
    *row.value = row.steps[next];
    saveCfg(); refreshGlassPalette(); refreshGlassLayers();
    if (row.needsBackdrop) requestBackdrop();
    G::dirty = true;
}
static void cycleGlass() {
    if (G::glassPick >= GLASS_PARAMS) { resetGlass(); return; }
    adjustGlass(1);
}
static void drawGlassPicker(Graphics& g) {
    auto& T = ui::theme();
    float width = G::W - PADX * 2, height = std::min(440 * S(), (float)G::H - 32 * S());
    float x = PADX, y = (G::H - height) / 2;
    G::glassSheet = RectF(x, y, width, height);
    ui::fillRoundRect(g, ui::Color(0, 0, 0, 150), 0, 0, (float)G::W, (float)G::H, panelRadius());
    ui::fillRoundRect(g, T.panelTop.withA(250), x, y, width, height, 20 * S());
    ui::drawRoundRect(g, T.rim, x, y, width, height, 20 * S());
    ui::TextStyle title; title.size = 18; title.bold = true; title.color = T.text;
    ui::drawText(g, L"玻璃效果", x + 18 * S(), y + 14 * S(), width - 36 * S(), 30 * S(), title);
    ui::TextStyle note; note.size = 10.5f; note.color = T.textDim;
    ui::drawText(g, L"越靠上越接近系统原生的毛玻璃质感", x + 18 * S(), y + 46 * S(), width - 36 * S(), 20 * S(), note);
    float rowH = (height - 120 * S()) / GLASS_ROWS;
    G::glassRows.clear();
    for (int i = 0; i < GLASS_ROWS; ++i) {
        float rowY = y + 74 * S() + i * rowH;
        RectF rowRect(x + 12 * S(), rowY, width - 24 * S(), rowH - 4 * S());
        G::glassRows.push_back(rowRect);
        bool selected = i == G::glassPick;
        if (selected) {
            ui::fillRoundRect(g, T.selBg, rowRect.X, rowRect.Y, rowRect.Width, rowRect.Height, 13 * S());
            ui::drawRoundRect(g, T.sep, rowRect.X, rowRect.Y, rowRect.Width, rowRect.Height, 13 * S());
        }
        if (i < GLASS_PARAMS) {
            auto& row = gGlassRows[i];
            ui::TextStyle name; name.size = 13.5f; name.bold = selected; name.color = T.text;
            ui::drawText(g, row.name, rowRect.X + 12 * S(), rowRect.Y + 3 * S(), rowRect.Width * 0.42f, 20 * S(), name);
            ui::TextStyle hint; hint.size = 10.0f; hint.color = T.textFaint;
            ui::drawText(g, row.hint, rowRect.X + 12 * S(), rowRect.Y + 22 * S(), rowRect.Width * 0.6f, 17 * S(), hint);
            // 档位条：当前档位用主色填充
            int index = glassStepIndex(row);
            float barW = rowRect.Width * 0.28f, barX = rowRect.X + rowRect.Width - barW - 12 * S();
            float barY = rowRect.Y + (rowRect.Height - 6 * S()) / 2;
            for (int s = 0; s < row.count; ++s) {
                float segW = (barW - (row.count - 1) * 3 * S()) / row.count;
                float segX = barX + s * (segW + 3 * S());
                ui::fillRoundRect(g, s <= index ? T.accentA.withA(selected ? 235 : 170) : T.sep, segX, barY, segW, 6 * S(), 3 * S());
            }
            ui::TextStyle value; value.size = 12.5f; value.bold = true; value.color = selected ? T.accentB : T.textDim;
            value.halign = StringAlignmentFar;
            ui::drawText(g, glassValueText(row), barX - 54 * S(), rowRect.Y + 6 * S(), 48 * S(), 20 * S(), value);
        } else {
            ui::TextStyle reset; reset.size = 13.0f; reset.bold = selected; reset.color = T.textDim;
            ui::drawText(g, L"恢复默认", rowRect.X + 12 * S(), rowRect.Y, rowRect.Width, rowRect.Height, reset);
        }
    }
    note.halign = StringAlignmentCenter;
    ui::drawText(g, L"↑↓ 选择参数 · ←→ 调整 · A 下一档 · B 返回", x, y + height - 34 * S(), width, 24 * S(), note);
}
static int themePageStart(){return G::themePick/THEME_PAGE_ROWS*THEME_PAGE_ROWS;}
static RectF themeSheetRect() {
    float height = std::min(410 * S(), (float)G::H - 32 * S());
    return RectF(PADX, (G::H - height) / 2, G::W - PADX * 2, height);
}

static void drawThemePicker(Graphics& g) {
    auto& T = ui::theme();
    ui::fillRoundRect(g, ui::Color(5, 12, 24, 120), 0, 0, (float)G::W, (float)G::H, 22 * S());
    RectF sheet = themeSheetRect();
    ui::fillRoundRect(g, T.panelTop.withA(248), sheet.X, sheet.Y, sheet.Width, sheet.Height, 22 * S());
    ui::drawRoundRect(g, T.rim, sheet.X, sheet.Y, sheet.Width, sheet.Height, 22 * S());
    ui::TextStyle title; title.size = 17; title.bold = true;
    ui::drawText(g, L"选择外观", sheet.X + 20 * S(), sheet.Y + 15 * S(), sheet.Width - 40 * S(), 30 * S(), title);
    float rowH = (sheet.Height - 100 * S()) / THEME_PAGE_ROWS;
    for (int i = themePageStart(); i < std::min(themePageStart()+THEME_PAGE_ROWS,themes::count()); ++i) {
        float y = sheet.Y + 58 * S() + (i-themePageStart()) * rowH;
        float x = sheet.X + 12 * S(), width = sheet.Width - 24 * S();
        if (i == G::themePick) {
            ui::fillRoundRect(g, T.selBg, x, y, width, rowH - 5 * S(), 14 * S());
            ui::drawRoundRect(g, T.sep, x, y, width, rowH - 5 * S(), 14 * S());
        }
        auto swatch = themes::get(i).palette;
        float side = std::min(42 * S(), rowH - 18 * S());
        float sx = x + 12 * S(), sy = y + (rowH - 5 * S() - side) / 2;
        ui::fillRoundRect(g, swatch.glassBottom.withA(255), sx, sy, side, side, 12 * S());
        ui::fillRoundRect(g, swatch.accentA.withA(150), sx + side * 0.18f, sy + side * 0.22f, side * 0.64f, side * 0.22f, 4 * S());
        ui::fillRoundRect(g, swatch.selBg.withA(120), sx + side * 0.18f, sy + side * 0.58f, side * 0.44f, side * 0.14f, 3 * S());
        ui::TextStyle name; name.size = 14; name.bold = i == G::themePick;
        ui::drawText(g, themes::get(i).name, sx + side + 14 * S(), y + 7 * S(), width - side - 86 * S(), 25 * S(), name);
        ui::TextStyle detail; detail.size = 10.5f; detail.color = T.textDim;
        ui::drawText(g, themes::get(i).description, sx + side + 14 * S(), y + 32 * S(), width - side - 40 * S(), 19 * S(), detail);
        if (i == currentTheme()) {
            ui::TextStyle current; current.size = 9; current.color = T.accentB; current.halign = StringAlignmentFar;
            ui::drawText(g, L"当前", x + width - 48 * S(), y + 9 * S(), 36 * S(), 20 * S(), current);
        }
    }
    ui::TextStyle hint; hint.size = 10.5f; hint.color = T.textDim; hint.halign = StringAlignmentCenter;
    ui::drawText(g, (L"‹ 上页    "+std::to_wstring(G::themePick/4+1)+L" / "+std::to_wstring((themes::count()+3)/4)+L"    下页 ›"), sheet.X, sheet.GetBottom() - 34 * S(), sheet.Width, 23 * S(), hint);
}

static void drawJumpPicker(Graphics& g) {
    auto& theme=ui::theme();float width=G::W-PADX*2,height=std::min(380*S(),G::H-48*S());
    float x=PADX,y=(G::H-height)/2;
    ui::fillRoundRect(g,ui::Color(0,0,0,155),0,0,(float)G::W,(float)G::H,22*S());
    ui::fillRoundRect(g,theme.panelTop.withA(250),x,y,width,height,20*S());
    ui::TextStyle title;title.size=18;title.bold=true;title.color=theme.text;
    ui::drawText(g,L"字母速跳",x+18*S(),y+16*S(),width-36*S(),32*S(),title);
    ui::TextStyle note;note.size=10.5f;note.color=theme.textDim;
    ui::drawText(g,L"中文按拼音首字母 · 数字 / 其他归入 #",x+18*S(),y+54*S(),width-36*S(),24*S(),note);
    float cellWidth=(width-28*S())/6,cellHeight=(height-128*S())/5;
    G::jumpCells.clear();auto& items=G::tabs[0].items;
    for(int i=0;i<(int)G::jumpLetters.size();++i) {
        RectF cell(x+14*S()+i%6*cellWidth,y+88*S()+i/6*cellHeight,cellWidth-6*S(),cellHeight-6*S());
        G::jumpCells.push_back(cell);bool enabled=G::jumpAvailable[i];
        bool selected=i==G::jumpIndex;
        ui::fillRoundRect(g,selected?theme.selBg:theme.chip,cell.X,cell.Y,cell.Width,cell.Height,9*S());
        if(selected)ui::drawRoundRect(g,theme.accentB,cell.X,cell.Y,cell.Width,cell.Height,9*S());
        ui::TextStyle text;text.size=15;text.bold=selected;text.halign=StringAlignmentCenter;text.color=enabled?theme.text:theme.textFaint.withA(80);
        ui::drawText(g,std::wstring(1,G::jumpLetters[i]),cell.X,cell.Y,cell.Width,cell.Height,text);
    }
    note.halign=StringAlignmentCenter;
    ui::drawText(g,L"十字键 / 左摇杆选择 · A 跳转 · B 返回",x,y+height-34*S(),width,24*S(),note);
}
// 移除操作用固定的警示色，避免被主题配色吞掉。
static const ui::Color removeAccent(196,64,74);
static RectF removeSheetRect() {
    float width=G::W-PADX*2,height=std::min(216*S(),(float)G::H-32*S());
    return RectF(PADX,(G::H-height)/2,width,height);
}
static void drawHoldOverlay(Graphics& g,float x,float y,float w,float h,float progress) {
    auto& T=ui::theme();
    ui::fillRoundRect(g,T.panelTop.withA(252),x,y,w,h,16*S());
    ui::drawRoundRect(g,removeAccent.withA(210),x,y,w,h,16*S(),1.4f);
    ui::TextStyle text;text.size=12.5f;text.bold=true;text.halign=StringAlignmentCenter;text.color=T.text;
    ui::drawText(g,G::deleteKeyDown?L"继续按住 Delete 以移除":L"继续按住 RS 以移除",x,y+h*0.22f,w,24*S(),text);
    float barW=w-36*S(),barH=5*S(),barX=x+18*S(),barY=y+h-18*S();
    ui::fillRoundRect(g,T.sep,barX,barY,barW,barH,barH*0.5f);
    ui::fillRoundRect(g,removeAccent,barX,barY,std::max(barH,barW*std::clamp(progress,0.0f,1.0f)),barH,barH*0.5f);
}
static void drawRemoveConfirm(Graphics& g) {
    auto& T=ui::theme();
    ui::fillRoundRect(g,ui::Color(0,0,0,130),0,0,(float)G::W,(float)G::H,22*S());
    G::removeSheet=removeSheetRect();
    RectF sheet=G::removeSheet;
    ui::fillRoundRect(g,T.panelTop.withA(250),sheet.X,sheet.Y,sheet.Width,sheet.Height,20*S());
    ui::drawRoundRect(g,T.rim,sheet.X,sheet.Y,sheet.Width,sheet.Height,20*S());
    float inner=sheet.X+18*S(),innerW=sheet.Width-36*S();
    ui::TextStyle title;title.size=17;title.bold=true;title.color=T.text;
    ui::drawText(g,L"移除这个条目？",inner,sheet.Y+16*S(),innerW,30*S(),title);
    ui::TextStyle name;name.size=13.5f;name.bold=true;name.color=T.text;
    ui::drawText(g,G::removeItem.title,inner,sheet.Y+52*S(),innerW,24*S(),name);
    ui::TextStyle path;path.size=10.5f;path.color=T.textFaint;
    ui::drawText(g,G::removeItem.target.empty()?L"(无路径)":G::removeItem.target,inner,sheet.Y+78*S(),innerW,20*S(),path);
    ui::TextStyle note;note.size=11;note.color=T.textDim;
    ui::drawText(g,L"只移除侧边栏里的这一条，不会删除或移动游戏文件",inner,sheet.Y+104*S(),innerW,22*S(),note);
    float buttonH=44*S(),gap=10*S(),buttonW=(innerW-gap)/2;
    float buttonY=sheet.GetBottom()-buttonH-18*S();
    G::removeActions.clear();
    for(int i=0;i<2;++i) {
        RectF button(inner+i*(buttonW+gap),buttonY,buttonW,buttonH);
        G::removeActions.push_back(button);
        bool danger=i==1;
        ui::fillRoundRect(g,danger?removeAccent:T.chip,button.X,button.Y,button.Width,button.Height,13*S());
        if(!danger)ui::drawRoundRect(g,T.sep,button.X,button.Y,button.Width,button.Height,13*S());
        ui::TextStyle label;label.size=12.5f;label.bold=danger;label.halign=StringAlignmentCenter;
        label.color=danger?ui::Color(255,246,247):T.textDim;
        ui::drawText(g,danger?L"移除  A":L"取消  B",button.X,button.Y,button.Width,button.Height,label);
    }
}
static void clipPanelShape(Graphics& g, float w, float h) {
    GraphicsPath silhouette; ui::roundRectPath(silhouette, 0, 0, w, h, panelRadius());
    g.SetClip(&silhouette, CombineModeIntersect);
}
// 背景模糊 + 主题插画：静态层，只随几何/主题/背景变化。
static void paintGlassBase(Graphics& g, float w, float h, bool withImage) {
    auto& T = ui::theme();
    auto state = g.Save();
    clipPanelShape(g, w, h);
    if (G::backdrop) ui::drawImageOpacity(g, G::backdrop, 0, 0, w, h, T.backdropOpacity);
    // 视频出帧后由视频层顶替插画，保持和旧版一致的优先级。
    if (withImage && G::themeImage) {
        float iw = (float)G::themeImage->GetWidth(), ih = (float)G::themeImage->GetHeight();
        float scale = std::max(w / iw, h / ih);
        ui::drawImageOpacity(g, G::themeImage.get(), (w - iw * scale) * 0.5f, (h - ih * scale) * 0.5f,
            iw * scale, ih * scale, currentThemeEntry().imageOpacity);
    }
    g.Restore(state);
}
// 玻璃渐变 + 顶部光泽 + 边框：静态层，压在动态壁纸之上。
static void paintGlassTint(Graphics& g, float w, float h) {
    auto& T = ui::theme();
    auto state = g.Save();
    clipPanelShape(g, w, h);
    LinearGradientBrush br(PointF(0, 0), PointF(0, h), T.glassTop.gd(), T.glassBottom.gd());
    g.FillRectangle(&br, 0.0f, 0.0f, w, h);
    BYTE sheenAlpha = (BYTE)std::clamp(24.0f * G::cfg.glassSheen, 0.0f, 120.0f);
    LinearGradientBrush sheen(PointF(0, 0), PointF(w, h * 0.55f), T.rim.withA(sheenAlpha).gd(), T.rim.withA(0).gd());
    sheen.SetWrapMode(WrapModeTileFlipXY);
    g.FillRectangle(&sheen, 0.0f, 0.0f, w, h);
    g.Restore(state);
    ui::drawRoundRect(g, T.rim, 0.75f, 0.75f, w - 1.5f, h - 1.5f, panelRadius(), 1.0f + 0.3f * G::cfg.glassSheen);
}
static void drawPanel(Graphics& g) {
    auto& T = ui::theme();
    float w = (float)G::W, h = (float)G::H;
    if (G::waitingForRelease) {
        ui::fillRoundRect(g, T.panelTop.withA(245), PADX, h - 110 * S(), w - PADX * 2, 46 * S(), 14 * S());
        ui::TextStyle hint; hint.size = 12; hint.halign = StringAlignmentCenter;
        ui::drawText(g, L"松开按键与摇杆，即返回游戏", PADX, h - 110 * S(), w - PADX * 2, 46 * S(), hint);
        return;
    }

    // 动态壁纸已经由解码线程合成成面板尺寸，这里只剩一层贴图（圆角仍要裁一次）。
    bool paper = G::paperActive && G::videoSurface && G::videoSurface->GetWidth() == (UINT)G::W && G::videoSurface->GetHeight() == (UINT)G::H;
    auto blitPaper = [&] {
        if (!paper) return;
        auto state = g.Save();
        clipPanelShape(g, w, h);
        ui::drawCachedBitmap(g, G::videoSurface.get(), 0, 0);
        g.Restore(state);
    };
    if (!G::renderCaching || G::themeProgress < 1) {
        paintGlassBase(g, w, h, !paper);
        blitPaper();
        paintGlassTint(g, w, h);
    } else if (!paper) {
        // 无动态壁纸：两层合成成一张，每帧一次拷贝。
        if (!G::glassCache || G::glassWidth != G::W || G::glassHeight != G::H || G::glassTheme != G::cfg.theme || G::glassDirty) {
            if (!G::glassCache || G::glassWidth != G::W || G::glassHeight != G::H) G::glassCache = std::make_unique<Bitmap>(G::W, G::H, PixelFormat32bppPARGB);
            Graphics background(G::glassCache.get());
            background.Clear(Gdiplus::Color(0, 0, 0, 0));
            background.SetSmoothingMode(SmoothingModeHighQuality);
            paintGlassBase(background, w, h, true);
            paintGlassTint(background, w, h);
            G::glassWidth = G::W; G::glassHeight = G::H; G::glassTheme = G::cfg.theme; G::glassDirty = false;
            ++G::glassRebuilds;
        }
        ui::drawCachedBitmap(g, G::glassCache.get(), 0, 0, true);
    } else {
        // 动态壁纸：背景层与渐变层各自缓存，视频帧每帧只贴一次图。
        if (!G::glassUnder || G::glassUnderW != G::W || G::glassUnderH != G::H || G::glassUnderTheme != G::cfg.theme || G::glassDirty) {
            if (!G::glassUnder || G::glassUnderW != G::W || G::glassUnderH != G::H) G::glassUnder = std::make_unique<Bitmap>(G::W, G::H, PixelFormat32bppPARGB);
            Graphics under(G::glassUnder.get());
            under.Clear(Gdiplus::Color(0, 0, 0, 0));
            under.SetSmoothingMode(SmoothingModeHighQuality);
            paintGlassBase(under, w, h, false);
            G::glassUnderW = G::W; G::glassUnderH = G::H; G::glassUnderTheme = G::cfg.theme;
            ++G::glassRebuilds;
        }
        if (!G::glassOver || G::glassOver->GetWidth() != (UINT)G::W || G::glassOver->GetHeight() != (UINT)G::H || G::glassDirty) {
            if (!G::glassOver || G::glassOver->GetWidth() != (UINT)G::W || G::glassOver->GetHeight() != (UINT)G::H) G::glassOver = std::make_unique<Bitmap>(G::W, G::H, PixelFormat32bppPARGB);
            Graphics over(G::glassOver.get());
            over.Clear(Gdiplus::Color(0, 0, 0, 0));
            over.SetSmoothingMode(SmoothingModeHighQuality);
            paintGlassTint(over, w, h);
            ++G::glassRebuilds;
        }
        G::glassDirty = false;
        ui::drawCachedBitmap(g, G::glassUnder.get(), 0, 0, true);
        blitPaper();
        ui::drawCachedBitmap(g, G::glassOver.get(), 0, 0);
    }

    // ---- 顶部 ----
    if (G::view == 0) {
        ui::TextStyle logo; logo.size = 13; logo.bold = true; logo.color = T.text;
        ui::drawText(g, L"SIDEDECK", PADX + 4 * S(), 18 * S(), 170 * S(), 28 * S(), logo);
        ui::TextStyle label; label.size = 9.5f; label.color = T.textDim;
        ui::drawText(g, std::wstring(L"v") + SIDEDECK_VERSION_W + L" · " + currentThemeEntry().name, PADX + 4 * S(), 46 * S(), 200 * S(), 20 * S(), label);
    }
    // 右侧：手柄状态
    std::wstring status;
    if (G::padCount > 0) {
        status = L"手柄已连接";
    } else status = L"未检测到手柄";
    if(G::bridge.running()) status=G::bridge.fault() ? L"隔离等待 · 请检查手柄" : L"手柄隔离已启用";
    ui::TextStyle st; st.size = 11; st.color = G::padCount > 0 ? T.textDim : T.textFaint;
    st.halign = StringAlignmentFar;
    if (G::view == 0) ui::drawText(g, status, w - PADX - 185 * S(), 20 * S(), 180 * S(), 28 * S(), st);

    if (G::view == 1) {
        drawReader(g, w, h);
    } else {
        // ---- 标签栏 ----
        float tx = PADX;
        float ty = HEADER_H + 4.0f * S();
        std::vector<float> tabWidths;
        float selectedRight = PADX;
        float totalWidth = 0;
        for (size_t i = 0; i < G::tabs.size(); ++i) {
            float tw = std::max(76 * S(), ui::measureText(g, G::tabs[i].name, 13.5f, (int)i == G::tabIndex) + 26 * S());
            tabWidths.push_back(tw);
            totalWidth += tw + 6 * S();
            if ((int)i <= G::tabIndex) selectedRight += tw + 6 * S();
        }
        totalWidth -= 6 * S();
        if (totalWidth <= w - PADX * 2) tx = (w - totalWidth) / 2;
        else tx -= std::max(0.0f, selectedRight - (w - PADX));
        G::tabRects.clear();
        auto tabClip = g.Save();
        g.SetClip(RectF(PADX - 2 * S(), ty - 6 * S(), w - PADX * 2 + 4 * S(), TAB_H + 8 * S()), CombineModeIntersect);
        float tabX = tx;
        for (size_t i = 0; i < G::tabs.size(); ++i) {
            G::tabRects.emplace_back(tabX, ty, tabWidths[i], 34 * S());
            tabX += tabWidths[i] + 6 * S();
        }
        const auto& activeTab = G::tabRects[G::tabIndex];
        G::tabPillTargetX = activeTab.X; G::tabPillTargetW = activeTab.Width;
        if (!G::pillReady) { G::tabPillX = activeTab.X; G::tabPillW = activeTab.Width; G::pillReady = true; }
        ui::fillRoundRect(g, T.selBg, G::tabPillX, ty - 1 * S(), G::tabPillW, 31 * S(), 15 * S());
        ui::drawRoundRect(g, T.sep, G::tabPillX, ty - 1 * S(), G::tabPillW, 31 * S(), 15 * S());
        for (size_t i = 0; i < G::tabs.size(); i++) {
            ui::TextStyle ts; ts.size = 13.5f; ts.bold = (int)i == G::tabIndex;
            ts.color = (int)i == G::tabIndex ? T.text : T.textFaint;
            ts.halign = StringAlignmentCenter;
            float tw = tabWidths[i];
            ui::drawText(g, G::tabs[i].name, tx, ty, tw, 26.0f * S(), ts);
            tx += tw + 6.0f * S();
        }
        g.Restore(tabClip);

        G::libraryActions.clear();
        if(G::tabIndex==0) {
            bool chosenFavorite=!G::tabs[0].items.empty() && G::tabs[0].items[G::sel[0]].pinned;
            const std::wstring labels[]={chosenFavorite?L"Y  取消收藏":L"Y  收藏",L"X  字母速跳",G::favoritesOnly?L"LS  全部游戏":L"LS  仅收藏"};
            float actionWidth=(w-PADX*2-12*S())/3;
            for(int i=0;i<3;++i) {
                RectF action(PADX+i*(actionWidth+6*S()),HEADER_H+TAB_H,actionWidth,28*S());G::libraryActions.push_back(action);
                ui::fillRoundRect(g,i==2&&G::favoritesOnly?T.selBg:T.chip,action.X,action.Y,action.Width,action.Height,11*S());
                ui::TextStyle text;text.size=10.5f;text.halign=StringAlignmentCenter;text.color=T.textDim;
                ui::drawText(g,labels[i],action.X,action.Y,action.Width,action.Height,text);
            }
        }

        // ---- 列表 ----
        auto& items = G::tabs[G::tabIndex].items;
        float listTop = ::listTop();
        float listH = listBottom() - listTop;
        RectF clip(0, listTop, w, listH);
        auto saved = g.Save();
        g.SetClip(clip, CombineModeIntersect);

        if(!G::freeScroll)G::scrollTarget[G::tabIndex] = std::max(0.0f,
            (float)G::sel[G::tabIndex] * (ITEM_H + ITEM_GAP) - listH / 2 + ITEM_H / 2);
        float maxScroll = std::max(0.0f, (float)items.size() * (ITEM_H + ITEM_GAP) - listH);
        G::scrollTarget[G::tabIndex] = std::clamp(G::scrollTarget[G::tabIndex],0.0f,maxScroll);
        G::scroll[G::tabIndex] = std::clamp(G::scroll[G::tabIndex],0.0f,maxScroll);

        float step = ITEM_H + ITEM_GAP;
        float pageOffset = (1 - ui::easeOutCubic(G::pageAnim)) * 16 * S() * G::pageDirection;
        G::focusTarget = listTop + G::sel[G::tabIndex] * step - G::scroll[G::tabIndex];
        if (!G::focusReady || G::focusTab != G::tabIndex) {
            G::focusY = G::focusTarget; G::focusReady = true; G::focusTab = G::tabIndex;
        }
        if (!items.empty()) {
            float fx = PADX - 3 * S() + pageOffset, fw = w - PADX * 2 + 6 * S();
            LinearGradientBrush focus(PointF(fx, G::focusY), PointF(fx + fw, G::focusY + ITEM_H), T.selBg.gd(), T.selBg.withA((BYTE)(T.selBg.a * 0.45f)).gd());
            GraphicsPath card; ui::roundRectPath(card, fx, G::focusY + S(), fw, ITEM_H - 2 * S(), 16 * S());
            g.FillPath(&focus, &card);
            ui::drawRoundRect(g, T.sep, fx, G::focusY + S(), fw, ITEM_H - 2 * S(), 16 * S());
        }
        int first = std::max(0, (int)(G::scroll[G::tabIndex] / step) - 1);
        int last = std::min((int)items.size(), (int)((G::scroll[G::tabIndex] + listH) / step) + 2);
        for (int i = first; i < last; i++) {
            float y = listTop + i * step - G::scroll[G::tabIndex];
            drawItemCard(g, items[i], PADX + pageOffset, y, w - PADX * 2, ITEM_H, i == G::sel[G::tabIndex], 1);
        }
        if((G::holdArmed || G::deleteKeyDown) && G::holdProgress>0.02f && !items.empty()) {
            float y = listTop + G::sel[G::tabIndex] * step - G::scroll[G::tabIndex];
            drawHoldOverlay(g, PADX + pageOffset, y, w - PADX * 2, ITEM_H, G::holdProgress);
        }
        g.Restore(saved);

        if (items.empty()) {
            ui::TextStyle et; et.size = 13; et.color = T.textFaint; et.halign = StringAlignmentCenter;
            ui::drawText(g, G::tabIndex==0&&G::favoritesOnly?L"暂无收藏 · 按 LS 返回全部游戏":L"空空如也 —— 拖个游戏进来，或托盘菜单添加", 0, listTop + 40.0f * S(), w, 30.0f * S(), et);
        }

        // 滚动条
        if (maxScroll > 1) {
            float trackH = listH;
            float th = std::max(30.0f, trackH * (listH / (listH + maxScroll)));
            float ty2 = listTop + (trackH - th) * (G::scroll[G::tabIndex] / maxScroll);
            ui::fillRoundRect(g, T.textDim.withA(65), w - 7, ty2, 3, th, 1.5f);
        }
    }

    // ---- 底部提示 ----
    float fy = h - FOOTER_H + 6.0f * S();
    ui::fillRoundRect(g, T.chip, PADX, fy, w - PADX * 2, FOOTER_H - 16.0f * S(), 14 * S());
    struct Hint { const wchar_t* k; const wchar_t* v; };
    Hint listHints[] = { { L"↑↓", L"选择" }, { L"A", L"打开" }, { L"LB·RB", L"换页" }, { L"B", L"收起" } };
    if (G::tabIndex == G::htmlTabIndex) listHints[3].v = L"返回";
    if(G::tabIndex==0) {listHints[2].k=L"RS";listHints[2].v=L"长按移除";}
    Hint readerHints[] = { { L"↑↓", L"滚动" }, { L"RS", L"滚动" }, { L"X", L"顶部" }, { L"B", L"返回" } };
    auto& hints = G::view == 1 ? readerHints : listHints;
    float hx = PADX + 12.0f * S();
    for (auto& hh : hints) {
        float cell = (w - PADX * 2 - 24 * S()) / 4;
        float kw = std::min(40.0f * S(), cell * 0.45f), kh = 20.0f * S();
        ui::fillRoundRect(g, T.selBg, hx, fy + 8, kw, kh, 7 * S());
        ui::TextStyle kt; kt.size = 10.0f; kt.bold = true; kt.color = T.text;
        kt.halign = StringAlignmentCenter;
        ui::drawText(g, hh.k, hx, fy + 8, kw, kh, kt);
        hx += kw + 4.0f * S();
        ui::TextStyle vt; vt.size = 10.5f; vt.color = T.textDim;
        ui::drawText(g, hh.v, hx, fy + 8, std::max(1.0f, cell - kw - 4 * S()), kh, vt);
        hx += cell - kw - 4 * S();
    }

    // ---- 录制热键提示 ----
    if (G::captureHotkey) {
        ui::fillRoundRect(g, ui::Color(0, 0, 0, 200), 0, 0, w, h, 18);
        ui::TextStyle t1; t1.size = 18; t1.bold = true; t1.color = T.text; t1.halign = StringAlignmentCenter;
        ui::drawText(g, L"按住新组合键，然后松开", 0, h / 2 - 40, w, 40, t1);
        ui::TextStyle t2; t2.size = 13; t2.color = T.accentB; t2.halign = StringAlignmentCenter;
        ui::drawText(g, L"当前按下：" + gp::Input::buttonNames(G::curBtn), 0, h / 2 + 6, w, 30, t2);
        ui::TextStyle t3; t3.size = 11.5f; t3.color = T.textFaint; t3.halign = StringAlignmentCenter;
        ui::drawText(g, L"（按 B 取消）", 0, h / 2 + 40, w, 26, t3);
    }

    // ---- toast ----
    if (G::toastT > 0.01f) {
        float a = std::min(1.0f, G::toastT * 2.0f);
        float ty3 = h - 110 - (1.0f - a) * 10;
        ui::fillRoundRect(g, T.panelTop.withA((BYTE)(235 * a)), PADX, ty3, w - PADX * 2, 36, 12);
        ui::TextStyle ts; ts.size = 12.5f; ts.color = T.text.withA((BYTE)(255 * a));
        ts.halign = StringAlignmentCenter;
        ui::drawText(g, G::toastMsg, PADX, ty3, w - PADX * 2, 36, ts);
    }
    if (G::themePicker) drawThemePicker(g);
    if (G::jumpPicker) drawJumpPicker(g);
    if (G::glassPicker) drawGlassPicker(g);
    if (G::removeConfirm) drawRemoveConfirm(g);
}

static void renderFrame() {
    if (!G::hwnd) return;
    ensureDib(G::W, G::H);
    if (!G::dibBits) return;
    memset(G::dibBits, 0, (size_t)G::W * G::H * 4);
    Bitmap bmp(G::W, G::H, G::W * 4, PixelFormat32bppPARGB, G::dibBits);
    {
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeHighQuality);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        float entrance = G::waitingForRelease ? 1 : ui::easeOutCubic(G::anim);
        g.TranslateTransform((1 - entrance) * 26 * S() * (G::cfg.edge == 1 ? -1 : 1), 0);
        drawPanel(g);
    }
    HDC scr = GetDC(nullptr);
    POINT pt{ G::PX, G::PY };
    SIZE sz{ G::W, G::H };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, (BYTE)(G::waitingForRelease ? 255 : 255 * ui::easeOutCubic(G::anim)), AC_SRC_ALPHA };
    BOOL ulwok = UpdateLayeredWindow(G::hwnd, scr, &pt, &sz, G::hdcMem, &src, 0, &bf, ULW_ALPHA);
#ifdef DEBUG_LOG
    {
        Gdiplus::BitmapData bd; Rect rc0(0, 0, 4, 4);
        if (bmp.LockBits(&rc0, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Ok) {
            BYTE* q = (BYTE*)bd.Scan0;
            fprintf(stderr, "[LOG] px0 BGRA=%d,%d,%d,%d  px1=%d,%d,%d,%d\n",
                q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]);
            fflush(stderr);
            bmp.UnlockBits(&bd);
        }
        fprintf(stderr, "[LOG] ULW ret=%d gle=%lu size=%dx%d\n", (int)ulwok, GetLastError(), G::W, G::H);
        fflush(stderr);
    }
#endif
    ReleaseDC(nullptr, scr);
}

// ==================== 显示 / 隐藏 ====================
constexpr UINT WM_SIDEDECK_BACKDROP = WM_APP + 73;
// 抓屏+模糊约 25ms，放到后台线程做，结果通过消息回投，不阻塞呼出动画。
static void requestBackdrop() {
    if (G::backdropBusy.exchange(true)) return;
    int x = G::PX, y = G::PY, w = G::W, h = G::H;
    G::backdropWorker.start(std::thread([x, y, w, h] {
        int blur = std::clamp((int)lroundf(24.0f * G::cfg.glassBlur), 4, 96);
        std::unique_ptr<Bitmap> result(ui::grabBackdrop(x, y, w, h, blur, 4));
        if (result) {
            std::lock_guard<std::mutex> lock(G::backdropMutex);
            G::backdropResult = std::move(result);
            G::backdropResultW = w; G::backdropResultH = h;
        }
        G::backdropBusy.store(false);
        if (G::hwnd) PostMessageW(G::hwnd, WM_SIDEDECK_BACKDROP, 0, 0);
    }));
}
static void applyBackdropResult() {
    std::unique_ptr<Bitmap> incoming;
    int w = 0, h = 0;
    {std::lock_guard<std::mutex> lock(G::backdropMutex);
     if (!G::backdropResult) return;
     incoming = std::move(G::backdropResult);
     w = G::backdropResultW; h = G::backdropResultH;}
    if (w != G::W || h != G::H) return;   // 几何已经变了，丢弃这次抓屏
    delete G::backdrop;
    G::backdrop = incoming.release();
    G::glassDirty = true; G::dirty = true;
}

// 屏幕系数：分辨率提供基准，系统缩放可以继续抬高（尊重无障碍设置）。
// 1080p / 96 DPI -> 1.0；1440p -> 1.35；4K -> 2.05。
static float screenScaleFor(int monitorHeight, UINT dpi) {
    float byResolution = (monitorHeight - 48) / 1032.0f;   // 1080p 可用高度为基准
    float byDpi = dpi / 96.0f * 0.8f;                      // 125% 恰好等于 1.0
    return std::clamp(std::max(byResolution, byDpi), 0.85f, 2.6f);
}
// 逐显示器 DPI：优先 shcore!GetDpiForMonitor，取不到再退回 GetDeviceCaps。
static UINT monitorDpi(HMONITOR monitor) {
    typedef HRESULT (WINAPI* GetDpiForMonitorT)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorT query = [] {
        HMODULE module = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module ? (GetDpiForMonitorT)GetProcAddress(module, "GetDpiForMonitor") : (GetDpiForMonitorT)nullptr;
    }();
    if (query) { UINT x = 96, y = 96; if (SUCCEEDED(query(monitor, 0 /*MDT_EFFECTIVE_DPI*/, &x, &y)) && x) return x; }
    HDC screen = GetDC(nullptr);
    UINT fallback = screen ? (UINT)GetDeviceCaps(screen, LOGPIXELSX) : 96u;
    if (screen) ReleaseDC(nullptr, screen);
    return fallback ? fallback : 96u;
}
static void computeGeometry() {
    POINT pt{}; GetCursorPos(&pt);
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hm, &mi);
    float previous = G::screenScale;
    G::screenScale = screenScaleFor(mi.rcMonitor.bottom - mi.rcMonitor.top, monitorDpi(hm));
    if (G::screenScale != previous || ui::g_textScale != S()) {
        // 字号变了，行卡片缓存和玻璃层都要按新尺寸重建。
        clearRowCache(); G::glassCache.reset(); G::glassUnder.reset(); G::glassOver.reset();
        ui::g_textScale = S();
    }
    int sw = mi.rcWork.right - mi.rcWork.left;
    int sh = mi.rcWork.bottom - mi.rcWork.top;
    int m = std::min((int)lroundf(G::cfg.margin * G::screenScale), std::min(sw, sh) / 8);
    G::H = std::max(1, sh - m * 2);
    G::W = std::clamp((int)lroundf(G::cfg.width * S()), 1, std::max(1, sw - m * 2));
    G::PY = mi.rcWork.top + m;
    G::PX = G::cfg.edge == 1 ? mi.rcWork.left + m : mi.rcWork.right - G::W - m;
}
static void applyUiScale(float value) {
    G::cfg.scale = std::clamp(value, 0.75f, 2.5f);
    saveCfg();
    computeGeometry();
    ensureDib(G::W, G::H);
    if (G::hwnd) SetWindowPos(G::hwnd, HWND_TOPMOST, G::PX, G::PY, G::W, G::H, SWP_SHOWWINDOW);
    G::focusReady = false; G::pillReady = false;
    rebuildTabs();
    wchar_t buf[96]{};
    swprintf(buf, 96, L"界面 %.2f 倍 · 屏幕系数 %.2f → 总缩放 %d%%",
        G::cfg.scale, G::screenScale, (int)lroundf(S() * 100));
    toast(buf);
}
static void cycleUiScale() {
    const float presets[] = {0.9f, 1.1f, 1.35f, 1.6f, 1.9f, 2.2f};
    const int count = (int)(sizeof(presets) / sizeof(presets[0]));
    int current = 0;
    for (int i = 1; i < count; ++i)
        if (fabsf(presets[i] - G::cfg.scale) < fabsf(presets[current] - G::cfg.scale)) current = i;
    // 正好落在某一档就取下一档；自定义值先归到最近的一档。
    bool exact = fabsf(presets[current] - G::cfg.scale) < 0.001f;
    applyUiScale(presets[(current + (exact ? 1 : 0)) % count]);
}
static std::wstring uiScaleLabel() {
    wchar_t buf[32]{};
    swprintf(buf, 32, L"%.2f 倍", G::cfg.scale);
    return buf;
}


static void showPanel() {
    cancelTouch(G::hwnd);
    G::bridge.block(true);
    if (G::visible) { G::closing = false; G::waitingForRelease = false; G::waitForRelease = true; return; }
    HWND previous = GetForegroundWindow();
    DWORD previousProcess = 0;
    if (previous) GetWindowThreadProcessId(previous, &previousProcess);
    if (previousProcess == GetCurrentProcessId()) previous = G::lastExternalForeground;
    G::foregroundAtShow = previous;
    computeGeometry();
    ensureDib(G::W, G::H);
    requestBackdrop();
    G::video.target(G::W, G::H, currentThemeEntry().videoFit, currentThemeEntry().imageOpacity);
    G::visible = true;
    G::closing = false;
    G::waitForRelease = true;
    G::restoreOnClose = false; G::waitingForRelease = false;
    G::anim = 0.0f;
    G::animTick = GetTickCount64();G::animTime=frameclock::seconds();
    G::focusReady = false; G::pillReady = false;
    G::triggerDirection=0;G::triggerRepeat=0;
    // 可选：给游戏发暂停键
    if (!G::cfg.pauseKey.empty()) {
        std::wstring k = G::cfg.pauseKey;
        std::transform(k.begin(), k.end(), k.begin(), ::towlower);
        WORD vk = 0;
        if (k == L"esc" || k == L"escape") vk = VK_ESCAPE;
        else if (k == L"p") vk = 'P';
        else if (k.size() == 1) vk = (WORD)towupper(k[0]);
        if (vk) {
            INPUT in[2]{};
            in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vk;
            in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
        }
    }
    SetWindowPos(G::hwnd, HWND_TOPMOST, G::PX, G::PY, G::W, G::H, SWP_SHOWWINDOW);
    ShowWindow(G::hwnd, SW_SHOW);
    if (!G::inputFocus.acquire(G::hwnd, previous)) {
        G::visible = false; G::closing = false;
        G::bridge.block(false);
        G::inputFocus.release(false);
        ShowWindow(G::hwnd, SW_HIDE);
        delete G::backdrop; G::backdrop = nullptr;
        if (G::trayAdded) {
            G::nid.uFlags = NIF_INFO;
            wcscpy_s(G::nid.szInfoTitle, L"SideDeck 未能接管输入");
            wcscpy_s(G::nid.szInfo, L"为避免误操作游戏，已取消呼出。请点击托盘显示面板后重试。");
            G::nid.dwInfoFlags = NIIF_WARNING;
            Shell_NotifyIconW(NIM_MODIFY, &G::nid);
            G::nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        }
        return;
    }
    renderFrame();
}

static void hidePanel(bool restoreFocus) {
    cancelTouch(G::hwnd);
    if (!G::visible) return;
    G::closing = true;
    G::restoreOnClose = restoreFocus && G::inputFocus.ownsForeground();
    G::themePicker = false;
    G::jumpPicker = false;
    G::glassPicker = false;
    G::removeConfirm = false; G::removeActions.clear();
    G::holdArmed = false; G::holdButton = 0; G::holdProgress = 0; G::deleteKeyDown = 0;
    G::captureHotkey = false;
    G::recordedMask = 0; G::recordArmed = false;
    ReleaseCapture();
}

// ==================== 数据 ====================
static void refreshGameView() {
    cancelTouch(G::hwnd);G::freeScroll=false;
    if(G::tabs.empty())return;
    std::wstring selected;
    if(!G::sel.empty() && G::sel[0]>=0 && G::sel[0]<(int)G::tabs[0].items.size())selected=libraryindex::identity(G::tabs[0].items[G::sel[0]]);
    auto& items=G::tabs[0].items;items.clear();
    for(auto item:G::allGames) {
        item.pinned=libraryindex::favorite(G::cfg,item);
        if(!G::favoritesOnly || item.pinned)items.push_back(std::move(item));
    }
    if(G::sel.empty())G::sel.resize(G::tabs.size());
    G::sel[0]=0;
    for(size_t i=0;i<items.size();++i)if(libraryindex::identity(items[i])==selected)G::sel[0]=(int)i;
    G::focusReady=false;G::dirty=true;
}
static void rebuildTabs() {
    cancelTouch(G::hwnd);G::freeScroll=false;
    G::tabs.clear();
    G::htmlTabIndex = -1;
    Tab games; games.name = L"游戏库";
    // 手动优先
    for (auto& it : G::cfg.manual) if (!libraryindex::removed(G::cfg, it)) games.items.push_back(it);
    // 扫描（来自缓存或即时扫描）
    static std::vector<Item> scanned;
    static bool scannedReady=false;
    if (!scannedReady || G::rescanRequested) {
        scannedReady=true;
        scanned.clear();
        G::rescanRequested = false;
        if (G::cfg.scanSteam) { auto v = lib::scanSteam(); scanned.insert(scanned.end(), v.begin(), v.end()); }
        if (G::cfg.scanEpic) { auto v = lib::scanEpic(); scanned.insert(scanned.end(), v.begin(), v.end()); }
        if (G::cfg.scanStartMenu) { auto v = lib::scanStartMenu(); scanned.insert(scanned.end(), v.begin(), v.end()); }
        // 去重：与手动项 target 相同则跳过
        std::sort(scanned.begin(), scanned.end(), [](const Item& a, const Item& b) { return a.target < b.target; });
        scanned.erase(std::unique(scanned.begin(), scanned.end(),
            [](const Item& a, const Item& b) { return a.target == b.target; }), scanned.end());
        std::sort(scanned.begin(), scanned.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    }
    for (auto& it : scanned) {
        if (libraryindex::removed(G::cfg, it)) continue;
        bool dup = false;
        for (auto& m : games.items) if (m.target == it.target) { dup = true; break; }
        if (!dup) games.items.push_back(it);
    }
    G::allGames=std::move(games.items);
    std::map<std::wstring,wchar_t> initials;
    for(auto& item:G::allGames)initials[item.title]=libraryindex::initial(item.title);
    std::stable_sort(G::allGames.begin(),G::allGames.end(),[&](const Item& a,const Item& b) {
        wchar_t left=initials[a.title],right=initials[b.title];
        if(left!=right)return left==L'#'?false:right==L'#'?true:left<right;
        return CompareStringEx(L"zh-CN",NORM_IGNORECASE,a.title.c_str(),-1,b.title.c_str(),-1,nullptr,nullptr,0)==CSTR_LESS_THAN;
    });
    G::tabs.push_back(games);

    Tab guides; guides.name = L"攻略";
    guides.items = lib::scanGuides();
    G::tabs.push_back(guides);

    Tab tools; tools.name = L"工具";
    Item appearance; appearance.title = L"外观主题";
    appearance.subtitle = currentThemeEntry().name + L" · A 选择";
    appearance.target = L"__theme__"; appearance.kind = lib::Command;
    tools.items.push_back(appearance);
    Item glass; glass.title = L"玻璃效果";
    glass.subtitle = glassSummary();
    glass.target = L"__glass__"; glass.kind = lib::Command;
    tools.items.push_back(glass);
    Item zoom; zoom.title = L"界面缩放";
    zoom.subtitle = L"字号与行高 · 当前 " + uiScaleLabel();
    zoom.target = L"__uiscale__"; zoom.kind = lib::Command;
    tools.items.push_back(zoom);
    Item hk; hk.title = L"呼出组合键"; hk.subtitle = L"当前：" + gp::Input::buttonNames((WORD)G::cfg.hotkeyMask);
    hk.target = L"__hotkey__"; hk.kind = lib::Command;
    tools.items.push_back(hk);
    Item iso; iso.title = L"手柄输入隔离"; iso.subtitle = G::bridge.running() ? L"已启用 · 打开面板时游戏只收到归零输入" : L"设置隔离、安装驱动或恢复真实手柄";
    iso.target = L"__isolation__"; iso.kind = lib::Command; tools.items.push_back(iso);
    Item add; add.title = L"批量添加游戏"; add.subtitle = L"支持 Ctrl / Shift 多选，也可一次拖入多个文件";
    add.target = L"__addgames__"; add.kind = lib::Command;
    tools.items.push_back(add);
    Item sc; sc.title = L"重新扫描游戏"; sc.subtitle = L"Steam / Epic"; sc.target = L"__rescan__"; sc.kind = lib::Command;
    tools.items.push_back(sc);
    Item dir; dir.title = L"打开程序目录"; dir.subtitle = lib::appDir(); dir.target = L"__opendir__"; dir.kind = lib::Command;
    tools.items.push_back(dir);
    Item gd; gd.title = L"打开攻略目录"; gd.subtitle = lib::guidesDir(); gd.target = L"__openguides__"; gd.kind = lib::Command;
    tools.items.push_back(gd);
    Item cf; cf.title = L"编辑配置文件"; cf.subtitle = lib::configPath(); cf.target = L"__editconfig__"; cf.kind = lib::Command;
    tools.items.push_back(cf);
    Item ti;ti.title=L"导入主题包";ti.subtitle=L"选择 JSON 或 MP4 · 配色和背景一并导入";ti.target=L"__importtheme__";ti.kind=lib::Command;tools.items.push_back(ti);
    Item td;td.title=L"打开主题目录";td.subtitle=L"放入主题包后，重新打开外观主题即可刷新";td.target=L"__themedir__";td.kind=lib::Command;tools.items.push_back(td);
    G::tabs.push_back(tools);

    for (auto& t : G::cfg.extraTabs) G::tabs.push_back(t);

    if (G::tabIndex >= (int)G::tabs.size()) G::tabIndex = 0;
    G::sel.resize(G::tabs.size());
    G::scroll.resize(G::tabs.size());
    G::scrollTarget.resize(G::tabs.size());
    refreshGameView();
    for (size_t i = 0; i < G::tabs.size(); i++) {
        G::sel[i] = std::clamp(G::sel[i], 0, std::max(0, (int)G::tabs[i].items.size() - 1));
    }
    G::dirty = true;
    G::focusReady = false; G::pillReady = false;
}

static void rescan() {
    G::rescanRequested = true;
    rebuildTabs();
    toast(L"已重新扫描游戏库");
}

static void saveCfg() { lib::saveConfig(G::cfg); }
static void toggleFavorite() {
    if(G::view!=0 || G::tabIndex!=0 || G::jumpPicker || G::tabs[0].items.empty())return;
    Item item=G::tabs[0].items[G::sel[0]];
    bool added=libraryindex::toggle(G::cfg,item);saveCfg();refreshGameView();
    toast(added?L"已收藏":L"已取消收藏");
}
static void toggleFavoritesOnly() {
    if(G::view!=0 || G::tabIndex!=0 || G::jumpPicker)return;
    G::favoritesOnly=!G::favoritesOnly;refreshGameView();G::sel[0]=0;G::scroll[0]=G::scrollTarget[0]=0;
}
// ---- 移除条目（只删侧边栏里的条目，不碰游戏文件） ----
static void snapSelection(int index) {
    cancelTouch(G::hwnd);G::freeScroll=false;
    auto& items=G::tabs[G::tabIndex].items;if(items.empty())return;
    G::sel[G::tabIndex]=std::clamp(index,0,(int)items.size()-1);
    float height=listBottom()-listTop(),step=ITEM_H+ITEM_GAP;
    float target=std::clamp(G::sel[G::tabIndex]*step-height/2+ITEM_H/2,0.0f,std::max(0.0f,(float)items.size()*step-height));
    G::scroll[G::tabIndex]=G::scrollTarget[G::tabIndex]=target;G::focusReady=false;G::dirty=true;
}
static bool removableSelection() {
    return G::view==0 && G::tabIndex==0 && !G::tabs.empty() && !G::sel.empty() &&
        G::sel[0]>=0 && G::sel[0]<(int)G::tabs[0].items.size();
}
static void cancelRemove() {
    if(!G::removeConfirm)return;
    G::removeConfirm=false;G::removeActions.clear();G::holdProgress=0;G::dirty=true;
}
static void openRemoveConfirm() {
    G::holdArmed=false;G::holdButton=0;G::holdProgress=0;G::deleteKeyDown=0;
    if(G::themePicker || G::jumpPicker || !removableSelection() || G::removeConfirm){G::dirty=true;return;}
    G::removeItem=G::tabs[0].items[G::sel[0]];
    G::removeIndex=G::sel[0];
    G::removeConfirm=true;G::removeActions.clear();G::dirty=true;
}
static void performRemove() {
    if(!G::removeConfirm)return;
    Item item=G::removeItem;
    int index=G::removeIndex;
    auto key=libraryindex::identity(item);
    for(auto it=G::cfg.manual.begin();it!=G::cfg.manual.end();++it)
        if(libraryindex::identity(*it)==key){G::cfg.manual.erase(it);break;}
    // 扫描来源的条目靠移除名单挡住，否则下次扫描又会出现。
    libraryindex::remember(G::cfg.removed,key);
    libraryindex::forget(G::cfg.favorites,key);
    G::removeConfirm=false;G::removeActions.clear();G::holdProgress=0;
    saveCfg();rebuildTabs();
    G::tabIndex=0;G::view=0;
    int count=G::tabs.empty()?0:(int)G::tabs[0].items.size();
    if(count>0)snapSelection(std::clamp(index,0,count-1));
    else G::dirty=true;
    toast(L"已移除 · "+item.title);
}
static void openJumpPicker() {
    cancelTouch(G::hwnd);
    if(G::view!=0 || G::tabIndex!=0 || G::themePicker)return;
    std::fill(std::begin(G::jumpAvailable),std::end(G::jumpAvailable),false);
    for(auto& item:G::tabs[0].items) {
        auto position=G::jumpLetters.find(libraryindex::initial(item.title));
        if(position!=std::wstring::npos)G::jumpAvailable[position]=true;
    }
    G::jumpIndex=0;
    if(!G::tabs[0].items.empty())G::jumpIndex=(int)G::jumpLetters.find(libraryindex::initial(G::tabs[0].items[G::sel[0]].title));
    G::jumpPicker=true;G::dirty=true;
}
static void jumpTo(wchar_t letter) {
    if(G::tabIndex!=0 || G::view!=0)return;
    int index=libraryindex::find(G::tabs[0].items,letter);
    if(index<0){toast(L"这个字母下暂无游戏");return;}
    G::jumpPicker=false;snapSelection(index);toast(std::wstring(L"已跳到 ")+letter);
}

static void applyTheme(int id) {
    id = std::clamp(id, 0, themes::count() - 1);
    G::themeFrom = currentThemeEntry().palette; G::themeProgress = 0;
    G::cfg.theme = themes::get(id).base;
    G::cfg.themeFile = themes::get(id).key;
    loadThemeImage();
    for (auto& tab : G::tabs) for (auto& item : tab.items) {
        if (item.target == L"__theme__") item.subtitle = themes::get(id).name + L" · A 选择";
    }
    G::themePicker = false;
    saveCfg(); toast(std::wstring(L"外观已切换 · ") + themes::get(id).name);
}

static void importThemes(const std::vector<std::wstring>& paths){
    int added=0;std::wstring last;
    for(auto& path:paths){std::wstring key;if(themes::import(path,themeDirectory(),key)){++added;last=key;}}
    themes::reload(themeDirectory());
    if(added){applyTheme(themes::selected(0,last));G::themePick=currentTheme();}
    toast(L"已导入 "+std::to_wstring(added)+L" 个主题"+(added<(int)paths.size()?L" · 部分文件无效或无法复制":L""));
}
static void chooseThemes(){
    if(G::dialogOpen)return;
    G::dialogOpen=true;std::vector<std::wstring> paths;
    auto result=lib::chooseGameFiles(G::hwnd,paths,true);G::dialogOpen=false;G::waitForRelease=true;
    if(SUCCEEDED(result)&&!paths.empty())importThemes(paths);
    else if(result!=HRESULT_FROM_WIN32(ERROR_CANCELLED)&&FAILED(result))toast(L"无法打开主题选择器");
}

static void addGames(const std::vector<std::wstring>& paths) {
    auto result = lib::addManualFiles(G::cfg, paths);
    // 重新添加同一个文件等于撤销之前的移除。
    int restored = 0;
    for (auto& path : paths) {
        auto key = libraryindex::identity(lib::App, path);
        if (std::find(G::cfg.removed.begin(), G::cfg.removed.end(), key) != G::cfg.removed.end()) ++restored;
        libraryindex::forget(G::cfg.removed, key);
    }
    if (result.added || restored) { saveCfg(); rebuildTabs(); G::tabIndex = 0; G::sel[0] = 0; }
    int skipped = result.duplicates + result.unsupported + result.missing;
    std::wstring message = L"已添加 " + std::to_wstring(result.added) + L" 个游戏";
    if (result.duplicates) message += L" · 重复 " + std::to_wstring(result.duplicates);
    if (skipped > result.duplicates) message += L" · 无效 " + std::to_wstring(skipped - result.duplicates);
    toast(message);
}

static void importGames() {
    if (G::dialogOpen) return;
    G::dialogOpen = true;
    std::vector<std::wstring> paths;
    HRESULT result = lib::chooseGameFiles(G::hwnd, paths);
    G::dialogOpen = false;
    G::waitForRelease = true;
    if (SUCCEEDED(result) && !paths.empty()) addGames(paths);
    else if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) toast(L"无法打开文件选择器，请重试或直接拖入文件");
}

static bool driverRegistered(const wchar_t* name) {
    HKEY key=nullptr; std::wstring path=L"SYSTEM\\CurrentControlSet\\Services\\"+std::wstring(name);
    bool exists=RegOpenKeyExW(HKEY_LOCAL_MACHINE,path.c_str(),0,KEY_READ,&key)==ERROR_SUCCESS;
    if(key)RegCloseKey(key);
    return exists;
}
static void installInputDriver(const wchar_t* file, const wchar_t* description) {
    std::wstring path=lib::appDir()+L"\\drivers\\"+file;
    if(!lib::fileExists(path)) {MessageBoxW(G::hwnd,L"驱动安装文件缺失，请使用完整安装包。",L"SideDeck",MB_OK|MB_ICONERROR);return;}
    std::wstring message=std::wstring(L"将打开 ")+description+L" 官方安装程序，需要管理员确认。\n安装完成后请按提示手动重启，再回到工具 → 手柄输入隔离。\nSideDeck 不会自动重启电脑。";
    if(MessageBoxW(G::hwnd,message.c_str(),L"安装手柄隔离依赖",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK)return;
    auto result=ShellExecuteW(G::hwnd,L"runas",path.c_str(),L"/norestart",nullptr,SW_SHOWNORMAL);
    if((INT_PTR)result<=32)MessageBoxW(G::hwnd,L"安装程序未启动或管理员授权被取消。",L"SideDeck",MB_OK|MB_ICONWARNING);
}
static void configureIsolation() {
    if(G::dialogOpen)return;
    G::dialogOpen=true;G::bridge.block(true);
    auto finish=[] {G::dialogOpen=false;G::waitForRelease=true;G::bridge.block(G::visible);rebuildTabs();};
    if(G::bridge.running() || lib::fileExists(isolation::recoveryPath())) {
        if(MessageBoxW(G::hwnd,L"停止输入转发并恢复真实手柄？\n恢复后可能需要重新插拔手柄，或重新启动游戏。",L"恢复手柄",MB_OKCANCEL|MB_ICONQUESTION)==IDOK) {
            G::bridge.stop();G::cfg.inputIsolation=false;saveCfg();
            if(!isolation::restore())MessageBoxW(G::hwnd,L"恢复尚未完成。请关闭 HidHide 配置工具后重试；保留了恢复记录。",L"SideDeck",MB_OK|MB_ICONWARNING);
            G::pad.ignoreIndex(-1);
        }
        finish();return;
    }
    if(!driverRegistered(L"HidHide")) {installInputDriver(L"HidHide_1.5.230_x64.exe",L"HidHide");finish();return;}
    if(!driverRegistered(L"ViGEmBus")) {installInputDriver(L"ViGEmBus_1.22.0_x64_x86_arm64.exe",L"ViGEmBus");finish();return;}
    auto candidates=isolation::devices();G::pad.ignoreIndex(-1);G::pad.poll();
    if(candidates.size()!=1 || G::pad.count()!=1) {
        MessageBoxW(G::hwnd,L"第一版一次接管一只 XInput 手柄。\n请只连接需要使用的那只手柄，关闭其他手柄映射工具，再重试。\n串流时只保留 Sunshine 生成的输入手柄。",L"选择输入手柄",MB_OK|MB_ICONINFORMATION);finish();return;
    }
    std::wstring message=L"接管："+candidates[0].name+L"\n\n首次设置请先退出游戏。真实手柄将仅对 SideDeck 可见，游戏使用虚拟 Xbox 手柄。\n\n完成后需要重新连接一次手柄，再启动游戏。";
    if(MessageBoxW(G::hwnd,message.c_str(),L"启用手柄隔离",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK) {finish();return;}
    std::wstring error;
    if(G::bridge.start(candidates[0],(WORD)G::cfg.hotkeyMask,error)) {
        MessageBoxW(G::hwnd,L"现在请关闭再打开手柄，或重新插拔 2.4G 接收器。\n如果是串流输入，请重建串流手柄连接。\n完成后点确定，SideDeck 将检查游戏侧是否仍能看到输入手柄。",L"重新连接手柄",MB_OK|MB_ICONINFORMATION);
        if(G::bridge.verify(error)) {G::cfg.inputIsolation=true;saveCfg();toast(L"手柄隔离已通过独立验证");}
        else G::bridge.stop();
    }
    if(!error.empty())MessageBoxW(G::hwnd,error.c_str(),L"未启用手柄隔离",MB_OK|MB_ICONWARNING);
    finish();
}

// ==================== 动作 ====================
static void openSelected() {
    cancelTouch(G::hwnd);
    if(G::glassPicker){cycleGlass();return;}
    if(G::jumpPicker){jumpTo(G::jumpLetters[G::jumpIndex]);return;}
    if (G::themePicker) { applyTheme(G::themePick); return; }
    if (G::view == 1) return;
    auto& items = G::tabs[G::tabIndex].items;
    LOG("openSelected called");
    if (items.empty()) return;
    Item it = items[G::sel[G::tabIndex]];
    if(it.target==L"__importtheme__"){chooseThemes();return;}
    if(it.target==L"__themedir__"){
        std::error_code ec;std::filesystem::create_directories(themeDirectory(),ec);
        ShellExecuteW(G::hwnd,L"open",themeDirectory().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return;
    }
    if (it.target == L"__theme__") {
        G::themePicker = true; themes::reload(themeDirectory());loadThemeImage();
        setPalette(currentThemeEntry().palette);G::themeProgress=1;
        G::themePick = currentTheme(); G::dirty = true; return;
    }
#ifdef DEBUG_LOG
    fwprintf(stderr, L"[LOG] open: %ls kind=%d\n", it.title.c_str(), it.kind); fflush(stderr);
#endif
    if (it.kind == lib::Guide) {
        auto extension = lib::toLowerCp(std::filesystem::path(it.target).extension().wstring());
        if (it.guideText.empty() && (extension == L".html" || extension == L".htm")) {
            auto document = htmlguide::parse(lib::readTextFile(it.target));
            if (!document.entries.empty()) {
                Tab catalog; catalog.name = L"服装图鉴";
                Item overview; overview.kind = lib::Guide; overview.target = it.target;
                overview.title = L"图鉴说明与分组";
                overview.subtitle = L"完整导入 " + std::to_wstring(document.entries.size()) + L" 条服装记录";
                overview.guideText = "# 导入说明\n\n原 HTML 共 " + std::to_string(document.entries.size()) + " 条服装记录。下方保留原文汇总数字、进度和分组，汇总数字可能与实际条目数不同。\n\n" + document.overview;
                catalog.items.push_back(overview);
                for (auto& entry : document.entries) {
                    Item suit; suit.kind = lib::Guide; suit.target = it.target;
                    suit.title = ui::utf8(entry.title); suit.subtitle = ui::utf8(entry.subtitle);
                    suit.guideText = entry.body;
                    if (!entry.image.empty()) suit.iconPath = (std::filesystem::path(it.target).parent_path() / ui::utf8(entry.image)).lexically_normal().wstring();
                    catalog.items.push_back(suit);
                }
                if (G::htmlTabIndex < 0) {
                    G::htmlTabIndex = (int)G::tabs.size(); G::tabs.push_back(catalog);
                    G::sel.push_back(0); G::scroll.push_back(0); G::scrollTarget.push_back(0);
                } else {
                    G::tabs[G::htmlTabIndex] = catalog;
                    G::sel[G::htmlTabIndex] = 0; G::scroll[G::htmlTabIndex] = G::scrollTarget[G::htmlTabIndex] = 0;
                }
                G::tabIndex = G::htmlTabIndex; G::dirty = true; G::pageAnim = 0;
                return;
            }
            it.guideText = document.overview;
        }
        // 阅读
        LOG("enter reader");
        G::imageCache.clear();
        g_doc = parseMarkdown(it.guideText.empty() ? lib::readTextFile(it.target) : it.guideText);
        LOG("md parsed");
        G::readerTitle = it.title;
        G::readerPath = it.target;
        G::readerScroll = 0; G::readerTarget = 0;G::readerMaxScroll=0;
        G::view = 1;
        G::pageAnim = 0;
        G::dirty = true;
        return;
    }
    if (it.target == L"__hotkey__") {
        G::captureHotkey = true; G::recordedMask = 0; G::recordArmed = false;
        G::dirty = true; return;
    }
    if (it.target == L"__rescan__") { rescan(); return; }
    if (it.target == L"__addgames__") { importGames(); return; }
    if (it.target == L"__uiscale__") { cycleUiScale(); return; }
    if (it.target == L"__glass__") { openGlassPicker(); return; }
    if (it.target == L"__isolation__") { configureIsolation(); return; }
    if (it.target == L"__opendir__") { ShellExecuteW(nullptr, L"open", lib::appDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); return; }
    if (it.target == L"__openguides__") {
        std::wstring d = lib::guidesDir();
        std::error_code ec; std::filesystem::create_directories(d, ec);
        ShellExecuteW(nullptr, L"open", d.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (it.target == L"__editconfig__") {
        ShellExecuteW(nullptr, L"open", lib::configPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if(G::launchProcess){toast(L"正在启动，请稍候");return;}
    DWORD error=launchdispatch::start(it,G::launchProcess);
    G::launchingItem=it;
    if(!error){toast(L"正在启动 · "+it.title);return;}
    if(error) {
        wchar_t* systemMessage=nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,error,0,(LPWSTR)&systemMessage,0,nullptr);
        std::wstring message=L"无法启动："+it.title+L"\n\n"+(systemMessage?std::wstring(systemMessage):L"Windows 未提供错误说明。")+
            L"\n错误代码："+std::to_wstring(error)+L"\n\n启动项："+it.target;
        if(systemMessage)LocalFree(systemMessage);
        G::dialogOpen=true;
        MessageBoxW(G::hwnd,message.c_str(),L"SideDeck · 启动失败",MB_OK|MB_ICONERROR);
        G::dialogOpen=false;G::waitForRelease=true;G::dirty=true;return;
    }
    hidePanel();
}

static void navigate(int delta) {
    cancelTouch(G::hwnd);
    if(G::glassPicker){G::glassPick=std::clamp(G::glassPick+delta,0,GLASS_ROWS-1);G::dirty=true;return;}
    if(G::jumpPicker){G::jumpIndex=std::clamp(G::jumpIndex+delta*6,0,26);G::dirty=true;return;}
    if (G::themePicker) {
        G::themePick = std::clamp(G::themePick + delta, 0, themes::count() - 1);
        G::dirty = true; return;
    }
    if (G::view == 1) {
        G::readerTarget = std::max(0.0f, G::readerTarget + delta * 120.0f);
        G::dirty = true;
        return;
    }
    auto& items = G::tabs[G::tabIndex].items;
    if (items.empty()) return;
    int n = (int)items.size();
    if(G::freeScroll) {
        float step=ITEM_H+ITEM_GAP;
        int first=std::clamp((int)ceilf(G::scroll[G::tabIndex]/step),0,n-1);
        int last=std::clamp((int)floorf((G::scroll[G::tabIndex]+listBottom()-listTop()-ITEM_H)/step),first,n-1);
        if(G::sel[G::tabIndex]<first || G::sel[G::tabIndex]>last)G::sel[G::tabIndex]=delta>=0?first-1:last+1;
    }
    G::freeScroll=false;
    int before = G::sel[G::tabIndex];
    G::sel[G::tabIndex] = std::clamp(G::sel[G::tabIndex] + delta, 0, n - 1);
    if(abs(delta)>1)snapSelection(G::sel[G::tabIndex]);
    else {
        float height=listBottom()-listTop(),step=ITEM_H+ITEM_GAP;
        G::scrollTarget[G::tabIndex]=std::clamp(G::sel[G::tabIndex]*step-height/2+ITEM_H/2,0.0f,std::max(0.0f,n*step-height));
        G::focusTarget=listTop()+G::sel[G::tabIndex]*step-G::scroll[G::tabIndex];
    }
    if (G::sel[G::tabIndex] != before) G::dirty = true;   // 不重绘 = 看起来“翻不了页”
}

static void switchTab(int delta) {
    cancelTouch(G::hwnd);
    if(G::glassPicker){adjustGlass(delta>0?1:-1);return;}
    if(G::jumpPicker){G::jumpIndex=std::clamp(G::jumpIndex+delta,0,26);G::dirty=true;return;}
    if (G::view == 1 || G::themePicker) return;
    int n = (int)G::tabs.size();
    if (n <= 1) return;
    G::freeScroll=false;
    G::tabIndex = (G::tabIndex + delta + n) % n;
    G::pageAnim = 0; G::pageDirection = delta >= 0 ? 1 : -1;
    G::dirty = true;
}

static void goBack() {
    cancelTouch(G::hwnd);
    if(G::glassPicker){closeGlassPicker();return;}
    if(G::jumpPicker){G::jumpPicker=false;G::dirty=true;return;}
    if (G::themePicker) { G::themePicker = false; G::dirty = true; }
    else if (G::view == 1) { G::view = 0; G::dirty = true; G::pageAnim = 0; }
    else if (G::tabIndex == G::htmlTabIndex) { G::tabIndex = 1; G::dirty = true; }
    else hidePanel();
}

// ==================== 窗口过程 ====================
static double touchTime() {
    static LARGE_INTEGER frequency=[] {LARGE_INTEGER value{};QueryPerformanceFrequency(&value);return value;}();
    LARGE_INTEGER counter{};
    if(frequency.QuadPart>0 && QueryPerformanceCounter(&counter))return (double)counter.QuadPart/frequency.QuadPart;
    return GetTickCount64()/1000.0;
}
static void cancelTouch(HWND window) {
    if(G::touch.active && G::touchPointer==0)G::cancelledMouseTap=true;
    G::touch.cancel();G::touchPointer=0;
    if(window && GetCapture()==window)ReleaseCapture();
}
static float listMaximum() {
    if(G::tabIndex<0 || G::tabIndex>=(int)G::tabs.size())return 0;
    return std::max(0.0f,(float)G::tabs[G::tabIndex].items.size()*(ITEM_H+ITEM_GAP)-(listBottom()-listTop()));
}
static bool scrollPixels(float distance,bool direct) {
    if(!G::visible || G::closing || G::dialogOpen || G::captureHotkey || G::themePicker || G::jumpPicker || G::glassPicker || G::removeConfirm)return false;
    float previous=0,next=0;
    if(G::view==1) {
        previous=direct?G::readerScroll:G::readerTarget;
        next=std::clamp(previous+distance,0.0f,G::readerMaxScroll);G::readerTarget=next;
        if(direct)G::readerScroll=next;
    } else {
        if(G::tabIndex<0 || G::tabIndex>=(int)G::tabs.size())return false;
        G::freeScroll=true;previous=direct?G::scroll[G::tabIndex]:G::scrollTarget[G::tabIndex];
        next=std::clamp(previous+distance,0.0f,listMaximum());G::scrollTarget[G::tabIndex]=next;
        if(direct)G::scroll[G::tabIndex]=next;
        G::focusReady=false;
    }
    if(direct && std::fabs(next-previous-distance)>0.1f)G::touch.velocity=0;
    if(next!=previous)G::dirty=true;
    return next!=previous;
}
static bool beginTouch(HWND window,float x,float y) {
    if(!G::visible || G::closing || G::dialogOpen || G::captureHotkey)return false;
    bool eligible=!G::themePicker && !G::jumpPicker && !G::glassPicker && !G::removeConfirm && x>=PADX*0.5f && x<=G::W-PADX*0.5f &&
        y>=(G::view==1 ? HEADER_H+32*S() : listTop()) && y<listBottom();
    bool moving=false;
    if(eligible) {
        if(G::view==1) {moving=fabsf(G::readerTarget-G::readerScroll)>2;G::readerTarget=G::readerScroll;}
        else {moving=fabsf(G::scrollTarget[G::tabIndex]-G::scroll[G::tabIndex])>2;G::scrollTarget[G::tabIndex]=G::scroll[G::tabIndex];G::freeScroll=true;}
    }
    G::touch.begin(x,y,touchTime(),eligible);G::touch.stopTap=G::touch.stopTap || (eligible&&moving);
    G::cancelledMouseTap=false;
    (void)window;return true;
}
static void moveTouch(float x,float y) {
    float distance=G::touch.move(x,y,touchTime(),8*S(),3200*S());
    if(distance!=0)scrollPixels(distance,true);
}
static bool promotedTouchMouse() {
    DWORD extra=(DWORD)(ULONG_PTR)GetMessageExtraInfo();
    return (extra&0xffffff00u)==0xff515700u && G::nativeTouchTick && GetTickCount64()-G::nativeTouchTick<1000;
}
constexpr UINT WM_SIDEDECK_TAP=WM_APP+72;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_DPICHANGED: {
            // 换显示器或改缩放后重新按 DPI 排版；收起状态下下次呼出会自己重算。
            if (!G::visible || G::closing) break;
            computeGeometry();
            ensureDib(G::W, G::H);
            SetWindowPos(h, HWND_TOPMOST, G::PX, G::PY, G::W, G::H, SWP_SHOWWINDOW);
            G::video.target(G::W, G::H, currentThemeEntry().videoFit, currentThemeEntry().imageOpacity);
            G::focusReady = false; G::pillReady = false;
            requestBackdrop();
            G::dirty = true;
            break;
        }
        case WM_SIDEDECK_BACKDROP:
            applyBackdropResult();
            return 0;
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP: {
            UINT id=GET_POINTERID_WPARAM(wp);POINTER_INPUT_TYPE type=PT_POINTER;
            if(!GetPointerType(id,&type) || (type!=PT_TOUCH && type!=PT_PEN))break;
            G::nativeTouchTick=GetTickCount64();
            POINTER_INFO info{};
            if(!GetPointerInfo(id,&info)) {if(G::touchPointer==id)cancelTouch(h);return 0;}
            if(info.pointerFlags&POINTER_FLAG_CANCELED) {if(G::touchPointer==id)cancelTouch(h);return 0;}
            POINT point=info.ptPixelLocation;ScreenToClient(h,&point);
            if(m==WM_POINTERDOWN) {
                if(!(info.pointerFlags&POINTER_FLAG_PRIMARY) || G::touch.active)return 0;
                if(beginTouch(h,(float)point.x,(float)point.y))G::touchPointer=id;
            } else if(id==G::touchPointer && G::touch.active) {
                moveTouch((float)point.x,(float)point.y);
                if(m==WM_POINTERUP) {
                    bool consumed=G::touch.end(touchTime());G::touchPointer=0;
                    if(!consumed)WndProc(h,WM_SIDEDECK_TAP,0,MAKELPARAM(point.x,point.y));
                }
            }
            return 0;
        }
        case WM_POINTERCAPTURECHANGED:
            if(G::touchPointer && GET_POINTERID_WPARAM(wp)==G::touchPointer)cancelTouch(h);
            return 0;
        case WM_LBUTTONDOWN: {
            if(promotedTouchMouse())return 0;
            if(G::touchPointer)return 0;
            if(beginTouch(h,(float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp)) && h)SetCapture(h);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if(promotedTouchMouse())return 0;
            if(G::touch.active && G::touchPointer==0) {
                if(!(wp&MK_LBUTTON)){cancelTouch(h);return 0;}
                moveTouch((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp));
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if(G::touch.active && G::touchPointer==0)cancelTouch(nullptr);
            break;
        case WM_CANCELMODE:
            cancelTouch(h);break;
        case WM_LBUTTONUP: {
            if(promotedTouchMouse())return 0;
            if(G::cancelledMouseTap){G::cancelledMouseTap=false;return 0;}
            if(G::touchPointer)return 0;
            bool consumed=false;
            if(G::touch.active) {moveTouch((float)GET_X_LPARAM(lp),(float)GET_Y_LPARAM(lp));consumed=G::touch.end(touchTime());}
            if(h && GetCapture()==h)ReleaseCapture();
            if(consumed)return 0;
            return WndProc(h,WM_SIDEDECK_TAP,wp,lp);
        }
        case WM_SIDEDECK_TAP: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (G::captureHotkey) break;
            if(G::closing || G::dialogOpen)break;
            if(G::glassPicker) {
                bool hit=false;
                for(size_t i=0;i<G::glassRows.size();++i)if(G::glassRows[i].Contains((float)x,(float)y)) {
                    G::glassPick=(int)i;cycleGlass();hit=true;break;
                }
                if(!hit && !G::glassSheet.Contains((float)x,(float)y))closeGlassPicker();
                break;
            }
            if(G::removeConfirm) {
                bool hit=false;
                for(size_t i=0;i<G::removeActions.size();++i)if(G::removeActions[i].Contains((float)x,(float)y)) {
                    if(i==1)performRemove();else cancelRemove();
                    hit=true;break;
                }
                if(!hit && !G::removeSheet.Contains((float)x,(float)y))cancelRemove();
                break;
            }
            if(G::jumpPicker) {
                for(size_t i=0;i<G::jumpCells.size();++i)if(G::jumpCells[i].Contains((float)x,(float)y)){jumpTo(G::jumpLetters[i]);break;}
                break;
            }
            if(G::tabIndex==0 && G::view==0) {
                bool handled=false;
                for(size_t i=0;i<G::libraryActions.size();++i)if(G::libraryActions[i].Contains((float)x,(float)y)) {
                    if(i==0)toggleFavorite();else if(i==1)openJumpPicker();else toggleFavoritesOnly();handled=true;break;
                }
                if(handled)break;
            }
            if (G::themePicker) {
                RectF sheet = themeSheetRect();
                float rowH = (sheet.Height - 100 * S()) / THEME_PAGE_ROWS;
                float top = sheet.Y + 58 * S();
                if (x >= sheet.X + 12 * S() && x <= sheet.GetRight() - 12 * S() && y >= top && y < top + rowH * THEME_PAGE_ROWS) {
                    int index=themePageStart()+(int)((y-top)/rowH);
                    if(index<themes::count())applyTheme(index);
                } else if(sheet.Contains((float)x,(float)y) && y>=sheet.GetBottom()-38*S()){
                    G::themePick=std::clamp(themePageStart()+(x<sheet.X+sheet.Width/2?-4:4),0,themes::count()-1);G::dirty=true;
                } else if (!sheet.Contains((float)x, (float)y)) { G::themePicker = false; G::dirty = true; }
                break;
            }
            if (x < 0 || y < 0 || x > G::W || y > G::H) { hidePanel(); break; }
            if (G::view == 1) {
                // 点击返回区
                if (y >= HEADER_H - 6 && y < HEADER_H + 20 * S() && x >= PADX && x < PADX + 90 * S()) goBack();
                break;
            }
            // 命中测试列表项
            float listTop = ::listTop();
            float step = ITEM_H + ITEM_GAP;
            if (y >= listTop && y < listBottom() && G::tabIndex < (int)G::tabs.size()) {
                float rowOffset = fmodf(y - listTop + G::scroll[G::tabIndex], step);
                if (rowOffset >= ITEM_H) break;
                int idx = (int)((y - listTop + G::scroll[G::tabIndex]) / step);
                auto& items = G::tabs[G::tabIndex].items;
                if (idx >= 0 && idx < (int)items.size()) {
                    if (idx == G::sel[G::tabIndex]) openSelected();
                    else { G::sel[G::tabIndex] = idx; G::dirty = true; }
                }
            } else {
                for (size_t i = 0; i < G::tabRects.size(); ++i) {
                    if (x >= PADX && x <= G::W - PADX && G::tabRects[i].Contains((float)x, (float)y)) {
                        if (G::tabIndex != (int)i) switchTab((int)i - G::tabIndex);
                        break;
                    }
                }
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP hd = (HDROP)wp;
            std::vector<std::wstring> paths;
            UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; i++) {
                UINT length = DragQueryFileW(hd, i, nullptr, 0);
                std::vector<wchar_t> buffer(length + 1);
                if (DragQueryFileW(hd, i, buffer.data(), (UINT)buffer.size())) paths.emplace_back(buffer.data());
            }
            DragFinish(hd);
            std::vector<std::wstring> themePaths,gamePaths;
            for(auto& path:paths){auto ext=lib::toLowerCp(std::filesystem::path(path).extension().wstring());((ext==L".json" || ext==L".mp4")?themePaths:gamePaths).push_back(path);}
            if(!gamePaths.empty())addGames(gamePaths);
            if(!themePaths.empty())importThemes(themePaths);
            break;
        }
        case WM_MOUSEWHEEL: {
            if(!G::visible || G::closing || G::dialogOpen || G::captureHotkey)break;
            int d = ((short)HIWORD(wp)) > 0 ? -1 : 1;
            if(G::themePicker || G::jumpPicker || G::glassPicker){navigate(d);break;}
            cancelTouch(h);
            scrollPixels(-(float)(short)HIWORD(wp)/WHEEL_DELTA*72*S(),false);
            break;
        }
        case WM_KEYDOWN: {
            if (G::closing || G::dialogOpen) break;
            cancelTouch(h);
            if (G::captureHotkey) {
                if (wp == VK_ESCAPE) { G::captureHotkey = false; G::recordedMask = 0; G::recordArmed = false; G::dirty = true; }
                break;
            }
            if (G::removeConfirm) {
                if (wp == VK_RETURN) performRemove();
                else if (wp == VK_ESCAPE || wp == VK_BACK) cancelRemove();
                break;
            }
            if (wp == VK_DELETE) {
                // 长按 Delete 进入移除确认，避免误触；首帧只记时间。
                if (!(lp & (1 << 30))) G::deleteKeyDown = GetTickCount64();
                else if (G::deleteKeyDown && GetTickCount64() - G::deleteKeyDown >= (ULONGLONG)std::clamp(G::cfg.holdRemoveMs, 400, 3000))
                    openRemoveConfirm();
                break;
            }
            if (wp == VK_ESCAPE) { if (G::themePicker || G::jumpPicker) goBack(); else hidePanel(); }
            else if(wp==VK_F2)toggleFavorite();
            else if(wp==VK_F3)openJumpPicker();
            else if(wp==VK_F4)toggleFavoritesOnly();
            else if(wp>=L'A' && wp<=L'Z' && G::view==0 && G::tabIndex==0)jumpTo((wchar_t)wp);
            else if (wp == VK_UP) navigate(-1);
            else if (wp == VK_DOWN) navigate(1);
            else if (wp == VK_LEFT) switchTab(-1);
            else if (wp == VK_RIGHT) switchTab(1);
            else if (wp == VK_RETURN) openSelected();
            else if (wp == VK_BACK) goBack();
            else if(wp==VK_NEXT)navigate(std::max(1,(int)((listBottom()-listTop())/(ITEM_H+ITEM_GAP))));
            else if(wp==VK_PRIOR)navigate(-std::max(1,(int)((listBottom()-listTop())/(ITEM_H+ITEM_GAP))));
            break;
        }
        case WM_KEYUP: {
            if (wp == VK_DELETE) { G::deleteKeyDown = 0; G::holdProgress = 0; G::dirty = true; }
            break;
        }
        case WM_HOTKEY: {
            if (wp == 1) { if (G::visible && !G::closing) hidePanel(); else showPanel(); }
            break;
        }
        case WM_TIMER: {
            if (wp == 1) tickGamepad();
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            switch (id) {
                case 1001: showPanel(); break;
                case 1002: hidePanel(); break;
                case 1003: rescan(); break;
                case 1004: ShellExecuteW(nullptr, L"open", lib::appDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
                case 1005: importGames(); break;
                case 1008: configureIsolation(); break;
                case 1009: {
                    G::bridge.stop();G::cfg.inputIsolation=false;saveCfg();G::pad.ignoreIndex(-1);
                    toast(isolation::restore()?L"已恢复真实手柄":L"恢复未完成，请关闭 HidHide 配置工具后重试");rebuildTabs();break;
                }
                case 1006: PostQuitMessage(0); break;
                case 1007: {
                    ShellExecuteW(nullptr, L"open", lib::configPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
            }
            break;
        }
        case WM_TRAYICON: {
            if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) {
                POINT p{}; GetCursorPos(&p);
                SetForegroundWindow(h);
                TrackPopupMenu(G::hTrayMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
                PostMessage(h, WM_NULL, 0, 0);
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcW(h, m, wp, lp);
}

// ---- 崩溃转储：任何未处理异常都写 MiniDump，方便事后分析 ----
#include <dbghelp.h>
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path);
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash + 1);
    std::string out = dir + "crash_" + std::to_string(GetTickCount()) + ".dmp";
    HANDLE f = CreateFileA(out.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                          (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory), &mei, nullptr, nullptr);
        CloseHandle(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    int argumentCount=0;LPWSTR* arguments=CommandLineToArgvW(GetCommandLineW(),&argumentCount);
    if(arguments && argumentCount>=2 && wcscmp(arguments[1],L"--launch-item")==0) {
        int result=launchdispatch::run(argumentCount,arguments);LocalFree(arguments);return result;
    }
    if(arguments && argumentCount==3 && wcscmp(arguments[1],L"--input-watchdog")==0) {
        DWORD parent=(DWORD)wcstoul(arguments[2],nullptr,10);LocalFree(arguments);return isolation::watchdog(parent);
    }
    if(arguments && argumentCount==2 && wcscmp(arguments[1],L"--shutdown")==0) {
        HWND window=FindWindowW(L"SideDeckOverlay",nullptr);DWORD pid=0;if(window)GetWindowThreadProcessId(window,&pid);
        HANDLE process=pid?OpenProcess(SYNCHRONIZE,FALSE,pid):nullptr;
        if(window)PostMessageW(window,WM_COMMAND,1006,0);
        DWORD result=process?WaitForSingleObject(process,10000):WAIT_OBJECT_0;
        if(process)CloseHandle(process);
        LocalFree(arguments);return result==WAIT_OBJECT_0?0:1;
    }
    if(arguments && argumentCount==2 && wcscmp(arguments[1],L"--recover-input")==0) {
        LocalFree(arguments);HWND window=FindWindowW(L"SideDeckOverlay",nullptr);
        if(window) {DWORD_PTR result=0;return SendMessageTimeoutW(window,WM_COMMAND,1009,0,SMTO_ABORTIFHUNG,10000,&result)?0:2;}
        return isolation::restore()?0:3;
    }
    if(arguments)LocalFree(arguments);
    G::hInst = hInstance;
    SetUnhandledExceptionFilter(CrashFilter);
    // DPI aware
    {
        typedef BOOL (WINAPI* SPDAC)(DPI_AWARENESS_CONTEXT);
        HMODULE hU = GetModuleHandleW(L"user32.dll");
        SPDAC f = (SPDAC)GetProcAddress(hU, "SetProcessDpiAwarenessContext");
        if (f) f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else { typedef BOOL (WINAPI* SPDA)(); SPDA g2 = (SPDA)GetProcAddress(hU, "SetProcessDPIAware"); if (g2) g2(); }
    }

    // 单实例
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"SideDeck_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { if (mutex) CloseHandle(mutex); return 0; }
    LOG("start");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LOG("coinit ok");

    // GDI+
    ULONG_PTR token = 0;
    GdiplusStartupInput gsi{};
    GdiplusStartup(&token, &gsi, nullptr);
    G::iconCache.enableAsync();
    LOG("gdiplus ok");

    lib::loadConfig(G::cfg);
    themes::reload(themeDirectory());loadThemeImage();
    setPalette(currentThemeEntry().palette);
    ui::g_textScale = G::cfg.scale;
    LOG("config loaded");
    // 首次运行：写一份默认配置 + 示例攻略
    LOG("first-run block");
    {
        std::error_code ec;
        std::filesystem::create_directories(lib::guidesDir(), ec);
        LOG("guides dir ok");
        if (!lib::fileExists(lib::configPath())) {
            LOGW(lib::configPath().c_str());
            saveCfg();
            LOG("config saved");
            std::wstring sample = lib::guidesDir() + L"\\使用说明.md";
            if (!lib::fileExists(sample)) {
                FILE* f = _wfopen(sample.c_str(), L"wb");
                if (f) {
                    const char* txt =
                        "# SideDeck 使用说明\n\n"
                        "用手柄组合键随时呼出这个侧边栏。\n\n"
                        "## 按键\n\n"
                        "- 上下：选择项目\n"
                        "- A：打开 / 进入\n"
                        "- B：返回 / 收起面板\n"
                        "- LB / RB：切换标签页\n\n"
                        "## 添加游戏\n\n"
                        "1. 托盘图标右键 → 添加游戏\n"
                        "2. 或直接把 exe / 快捷方式拖到面板上\n\n"
                        "> 手动添加的游戏会排在游戏库最前面，并写入 config.json。\n\n"
                        "## 攻略\n\n"
                        "把任意 .md 文件放进 guides 目录，重启后出现在「攻略」页。\n"
                        "支持 # 标题、- 列表、> 引用、![](相对路径) 图片。\n";
                    fwrite(txt, 1, strlen(txt), f);
                    fclose(f);
                }
            }
        }
    }
    computeGeometry();
    LOG("rebuilding tabs");
    rebuildTabs();
    LOG("tabs ok");

    // 窗口
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SideDeckOverlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    RegisterClassExW(&wc);
    LOG("class ok");
    computeGeometry();
    G::hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_ACCEPTFILES,
        L"SideDeckOverlay", L"SideDeck", WS_POPUP,
        G::PX, G::PY, G::W, G::H, nullptr, nullptr, hInstance, nullptr);
    if (!G::hwnd) { LOG("CREATEWINDOW FAILED"); return 1; }
    LOG("window ok");
    ensureDib(G::W, G::H);

    LOG("tray block");
    // 托盘
    {
        // 图标来自 exe 资源（src/SideDeck.ico），与任务栏、安装器保持同一套
        HICON icon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        HICON iconLarge = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
            GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
        G::nid.hIcon = icon ? icon : iconLarge;

        G::nid.cbSize = sizeof(G::nid);
        G::nid.hWnd = G::hwnd;
        G::nid.uID = 1;
        G::nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        G::nid.uCallbackMessage = WM_TRAYICON;
        G::nid.hIcon = icon;
        std::wstring tip = std::wstring(L"SideDeck v") + SIDEDECK_VERSION_W + L" 手柄侧栏";
        wcsncpy_s(G::nid.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &G::nid);
        G::trayAdded = true;

        G::hTrayMenu = CreatePopupMenu();
        AppendMenuW(G::hTrayMenu, MF_STRING, 1001, L"显示面板");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1002, L"收起面板");
        AppendMenuW(G::hTrayMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(G::hTrayMenu, MF_STRING, 1005, L"批量添加游戏…");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1008, L"手柄输入隔离 / 恢复…");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1003, L"重新扫描游戏库");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1004, L"打开程序目录");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1007, L"编辑配置");
        AppendMenuW(G::hTrayMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(G::hTrayMenu, MF_STRING, 1006, L"退出");
    }

    HANDLE frameTimer=frameclock::create();
    if(!frameTimer)SetTimer(G::hwnd,1,10,nullptr);
    LOG("timer ok");
    RegisterHotKey(G::hwnd, 1, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE);   // Ctrl+空格 备用呼出

    if (!G::pad.available()) toast(L"未找到 XInput 运行时");
    if(G::cfg.inputIsolation) {
        auto candidates=isolation::devices();std::wstring error;
        if(candidates.size()!=1 || !G::bridge.start(candidates[0],(WORD)G::cfg.hotkeyMask,error) || !G::bridge.verify(error)) {
            G::bridge.stop();toast(L"手柄隔离未启用，请到工具页重新设置");
        } else {G::bridge.block(false);rebuildTabs();}
    }

    MSG msg{};
    bool running=true;
    while(running){
        DWORD result=MsgWaitForMultipleObjectsEx(frameTimer?1:0,frameTimer?&frameTimer:nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if(result==WAIT_FAILED)break;
        if(frameTimer && result==WAIT_OBJECT_0)tickGamepad();
        // Bound message draining so pointer floods cannot starve animation.
        for(int n=0;n<64 && PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++n){
            if(msg.message==WM_QUIT){running=false;break;}
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
    }
    if(frameTimer){CancelWaitableTimer(frameTimer);CloseHandle(frameTimer);}

    if (G::trayAdded) Shell_NotifyIconW(NIM_DELETE, &G::nid);
    G::bridge.stop();
    G::inputFocus.release(true);
    if(G::launchProcess){CloseHandle(G::launchProcess);G::launchProcess=nullptr;}
    KillTimer(G::hwnd, 1);
    UnregisterHotKey(G::hwnd, 1);
    DestroyWindow(G::hwnd);
    if (G::hTrayMenu) DestroyMenu(G::hTrayMenu);
    // G::nid.hIcon 由 LoadImage(LR_SHARED) 返回，由系统管理，不手动销毁
    delete G::backdrop;
    G::backdrop = nullptr;
    // 抓屏线程必须在 GDI+ 关闭前收尾，避免它在 GdiplusShutdown 之后还在建位图。
    G::backdropWorker.join();
    {std::lock_guard<std::mutex> lock(G::backdropMutex);G::backdropResult.reset();}
    G::iconCache.clear(); G::imageCache.clear();
    clearRowCache();G::glassCache.reset();G::glassUnder.reset();G::glassOver.reset();
    G::video.close();G::videoSurface.reset();G::themeImage.reset();ui::fontCache.clear();
    releaseDib();
    GdiplusShutdown(token);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}

// ==================== 手柄轮询 + 动画 ====================
static void tickAnim(float elapsed = -1) {
    if(G::launchProcess && WaitForSingleObject(G::launchProcess,0)==WAIT_OBJECT_0) {
        DWORD result=ERROR_GEN_FAILURE;GetExitCodeProcess(G::launchProcess,&result);
        CloseHandle(G::launchProcess);G::launchProcess=nullptr;
        if(result==ERROR_SUCCESS){if(G::visible && !G::closing)hidePanel(false);}
        else {
            if(!G::visible || G::closing)showPanel();
            std::wstring message=L"未能启动："+G::launchingItem.title+L"\n\n";
            wchar_t* detail=nullptr;
            FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,result,0,(LPWSTR)&detail,0,nullptr);
            message+=detail?detail:L"启动子进程异常退出，可能由 Windows 外壳扩展引起。面板仍可继续使用。";
            if(detail)LocalFree(detail);
            message+=L"\n错误代码："+std::to_wstring(result)+L"\n\n启动项："+G::launchingItem.target;
            G::dialogOpen=true;MessageBoxW(G::hwnd,message.c_str(),L"SideDeck · 启动失败",MB_OK|MB_ICONERROR);
            G::dialogOpen=false;G::waitForRelease=true;G::dirty=true;
        }
    }
    G::video.playing(G::visible && !G::closing);
    if (!G::visible) return;
    // 键盘长按 Delete：靠按键重复推进进度，到达阈值后弹确认框。
    if (G::deleteKeyDown && !G::closing) {
        DWORD threshold = (DWORD)std::clamp(G::cfg.holdRemoveMs, 400, 3000);
        ULONGLONG held = GetTickCount64() - G::deleteKeyDown;
        G::holdProgress = std::min(1.0f, (float)held / (float)threshold);
        G::dirty = true;
        if (held >= threshold) openRemoveConfirm();
    }
    // 解码线程已经把帧合成成面板尺寸，这里只做一次行拷贝；不再让玻璃层失效。
    if(G::video.take(G::videoSurface)){if(!G::paperActive){G::paperActive=true;G::glassDirty=true;}G::dirty=true;}
    if(FAILED(G::video.error()) && !G::videoErrorShown){G::videoErrorShown=true;toast(L"视频壁纸无法播放，请检查文件或系统媒体组件");}
    ULONGLONG now = GetTickCount64();
    double precise=frameclock::seconds();
    float dt=elapsed>=0?elapsed:(G::animTime?std::min(0.05f,(float)(precise-G::animTime)):0.016f);
    G::animTime=precise;
    G::animTick = now;
    bool need = false;
    if (G::closing) {
        G::anim = std::max(0.0f, G::anim - dt / 0.18f);
        if (G::anim <= 0) {
            bool restore = G::restoreOnClose && G::inputFocus.ownsForeground();
            if (restore && (!G::padNeutral || !focusguard::keyboardNeutral())) {
                G::waitingForRelease = true;
                renderFrame();
                return;
            }
            G::waitingForRelease = false;
            G::bridge.block(false);
            G::inputFocus.release(restore);
            G::restoreOnClose = false;
            G::visible = false; G::closing = false;
            ShowWindow(G::hwnd, SW_HIDE);
            G::glassCache.reset();G::glassUnder.reset();G::glassOver.reset();
            clearRowCache();G::imageCache.clear();
            delete G::backdrop; G::backdrop = nullptr;
            return;
        }
        need = true;
    } else if (G::anim < 1.0f) { G::anim = std::min(1.0f, G::anim + dt / 0.24f); need = true; }
    if (G::pageAnim < 1) { G::pageAnim = std::min(1.0f, G::pageAnim + dt / 0.20f); need = true; }
    if (G::themeProgress < 1) {
        G::themeProgress = std::min(1.0f, G::themeProgress + dt / 0.28f);
        setPalette(ui::mixTheme(G::themeFrom, currentThemeEntry().palette, ui::easeOutCubic(G::themeProgress)));
        need = true;
    }
    if(!G::closing) {
        float distance=G::touch.coast(dt);
        if(distance!=0 && scrollPixels(distance,true))need=true;
    }
    float smoothing = 1 - expf(-18 * dt);
    auto follow = [&](float& value, float target) {
        if (fabsf(value - target) > 0.15f) { value += (target - value) * smoothing; need = true; }
        else if (value != target) { value = target; need = true; }
    };
    if (G::focusReady) follow(G::focusY, G::focusTarget);
    if (G::pillReady) { follow(G::tabPillX, G::tabPillTargetX); follow(G::tabPillW, G::tabPillTargetW); }
    for (size_t i = 0; i < G::tabs.size(); i++) {
        follow(G::scroll[i], G::scrollTarget[i]);
    }
    follow(G::readerScroll, G::readerTarget);
    if (G::toastT > 0.0f) { G::toastT = std::max(0.0f, G::toastT - dt * 0.5f); need = true; }
    if (G::captureHotkey) need = true;
    if (G::dirty) { G::dirty = false; need = true; }
    if (need) renderFrame();
}

static void tickGamepad() {
    using gp::Btn;
    G::bridge.hotkey((WORD)G::cfg.hotkeyMask);
    static unsigned lastIconRevision=0;
    unsigned iconRevision=G::iconCache.revision();
    if(iconRevision!=lastIconRevision){lastIconRevision=iconRevision;G::dirty=true;}
    static int lastInputFault=-1;
    int inputFault=G::bridge.fault();
    if(inputFault!=lastInputFault) {G::dirty=true;lastInputFault=inputFault;}
    HWND foreground = GetForegroundWindow();
    DWORD foregroundProcess = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundProcess);
    if (!G::visible && foreground && foregroundProcess != GetCurrentProcessId()) G::lastExternalForeground = foreground;
    G::pad.ignoreIndex(G::bridge.virtualIndex());
    G::pad.poll();
    G::padCount = G::pad.count();

    int idx = -1;
    for (int i = 0; i < gp::MAX_PADS; i++) if (G::pad.connected(i)) { idx = i; break; }
    G::padNeutral = true;
    for (int i = 0; i < gp::MAX_PADS; ++i) {
        if (G::pad.connected(i) && !gp::isNeutral(G::pad.state(i).Gamepad)) G::padNeutral = false;
    }
    if (G::closing || G::dialogOpen) {
        G::prevBtn = G::curBtn = idx < 0 ? 0 : G::pad.buttons(idx);
        tickAnim(); return;
    }
    if (G::visible && !G::inputFocus.ownsForeground()) {
        hidePanel(false); tickAnim(); return;
    }
    if(G::touch.active) {
        G::prevBtn=G::curBtn=idx<0?0:G::pad.buttons(idx);
        tickAnim();return;
    }

    if (idx < 0) {
        G::prevBtn = 0; G::curBtn = 0; G::padCount = 0;
        G::backDownMs = 0; G::stickConsumed = false;
        tickAnim();
        return;
    }

    WORD b = G::pad.buttons(idx);
    WORD pressed = (WORD)(b & ~G::prevBtn);
    G::curBtn = b;
    DWORD now = GetTickCount();

    // ---- 录制呼出键 ----
    // 修复：进入录制瞬间 A 键还处于按下状态，会被误录进组合键
    //       （录出“仅 A”，之后按 A 呼出+打开互相打架）
    // 方案：armed 前先等所有键松开；禁止单独 A/B 作为呼出键
    if (G::captureHotkey) {
        WORD& recMask = G::recordedMask;
        bool& armed = G::recordArmed;
        if (pressed & Btn::B) {
            G::captureHotkey = false; recMask = 0; armed = false;
            toast(L"已取消");
        } else if (!armed) {
            if (b == 0) armed = true;      // 等进入录制时按住的键（A）松开
        } else if (b != 0) {
            recMask = (WORD)(recMask | b);
        } else if (recMask != 0) {
            if (recMask == Btn::A || recMask == Btn::B) {
                toast(L"不能只按 A / B 单键，请用组合键");
            } else {
                G::cfg.hotkeyMask = recMask;
                saveCfg(); rebuildTabs();
                toast(L"已设为 " + gp::Input::buttonNames(recMask));
            }
            G::captureHotkey = false; recMask = 0; armed = false;
        }
        G::dirty = true;
        G::prevBtn = b;
        tickAnim();
        return;
    }

    // ---- 呼出 ----
    if (!G::visible) {
        if (G::cfg.hotkeyMask && G::pad.allHeld((WORD)G::cfg.hotkeyMask, idx) &&
            (G::prevBtn & G::cfg.hotkeyMask) != G::cfg.hotkeyMask) {
            G::prevBtn = b;
            showPanel();
            tickAnim();
            return;
        }
        if (G::cfg.longBack && (b & Btn::BACK)) {
            if (!G::backDownMs) G::backDownMs = now;
            else if (now - G::backDownMs > (DWORD)G::cfg.longPressMs) {
                G::backDownMs = 0;
                G::prevBtn = b;
                showPanel();
                tickAnim();
                return;
            }
        } else G::backDownMs = 0;
        G::prevBtn = b;
        return;
    }

    if (G::waitForRelease) {
        if (gp::isNeutral(G::pad.state(idx).Gamepad)) G::waitForRelease = false;
        G::prevBtn = b; tickAnim(); return;
    }

    // ---- 移除确认（A 确认 / B 取消） ----
    if (G::removeConfirm) {
        if (pressed & Btn::A) performRemove();
        else if (pressed & (Btn::B | Btn::BACK | Btn::START)) cancelRemove();
        G::prevBtn = b; tickAnim(); return;
    }

    // ---- 长按 RS 移除条目（仅游戏库） ----
    // RS 短按原本无作用，所以长按不会和别的操作抢键。
    bool canRemove = G::view == 0 && G::tabIndex == 0 && !G::themePicker && !G::jumpPicker;
    if (G::holdArmed) {
        if (!(b & Btn::RTHUMB)) {
            G::holdArmed = false; G::holdButton = 0; G::holdProgress = 0; G::dirty = true;
            G::prevBtn = b; tickAnim(); return;
        }
        DWORD threshold = (DWORD)std::clamp(G::cfg.holdRemoveMs, 400, 3000);
        DWORD held = now - G::holdStart;
        G::holdProgress = std::min(1.0f, (float)held / (float)threshold);
        G::dirty = true;
        if (held >= threshold) openRemoveConfirm();
        G::prevBtn = b; tickAnim(); return;
    }
    if ((pressed & Btn::RTHUMB) && canRemove) {
        G::holdArmed = true; G::holdButton = Btn::RTHUMB; G::holdStart = now; G::holdProgress = 0;
        G::prevBtn = b; tickAnim(); return;
    }

    // ---- 面板内导航 ----
    if (pressed & Btn::B) {
        goBack();
        G::prevBtn = b; tickAnim(); return;
    }
    if (pressed & Btn::A) { openSelected(); G::prevBtn = b; tickAnim(); return; }
    if (pressed & Btn::START) { hidePanel(); G::prevBtn = b; return; }
    if(G::view==0 && G::tabIndex==0 && !G::themePicker && !G::jumpPicker) {
        if(pressed & Btn::Y){toggleFavorite();G::prevBtn=b;tickAnim();return;}
        if(pressed & Btn::X){openJumpPicker();G::prevBtn=b;tickAnim();return;}
        if(pressed & Btn::LTHUMB){toggleFavoritesOnly();G::prevBtn=b;tickAnim();return;}
    }
    if (pressed & Btn::LB) switchTab(-1);
    if (pressed & Btn::RB) switchTab(1);
    if (G::view == 1 && (pressed & Btn::X)) { cancelTouch(G::hwnd);G::readerTarget = 0; G::dirty = true; }
    if(G::view==0 && !G::jumpPicker && !G::themePicker && !G::glassPicker) {
        int direction=G::pad.trigger(gp::RT,idx)>100?1:G::pad.trigger(gp::LT,idx)>100?-1:0;
        if(direction && (direction!=G::triggerDirection || now>=G::triggerRepeat)) {
            navigate(direction*std::max(1,(int)((listBottom()-listTop())/(ITEM_H+ITEM_GAP))));
            G::triggerRepeat=now+(direction!=G::triggerDirection?300u:150u);
        }
        G::triggerDirection=direction;
    }

    // 十字键（带重复）
    int d = 0;
    if (b & Btn::DOWN) d = 1; else if (b & Btn::UP) d = -1;
    if (d != 0) {
        if ((pressed & (Btn::UP | Btn::DOWN)) || now > G::lastRepeat) {
            navigate(d);
            G::lastRepeat = now + (pressed & (Btn::UP | Btn::DOWN) ? 300u : 90u);
        }
    }
    int dh = 0;
    if (b & Btn::RIGHT) dh = 1; else if (b & Btn::LEFT) dh = -1;
    if (dh != 0 && (pressed & (Btn::LEFT | Btn::RIGHT))) switchTab(dh);

    // 左摇杆
    float sy = G::pad.stickY(false, idx);
    float sx = G::pad.stickX(false,idx);
    if(G::jumpPicker && fabsf(sx)>0.55f && fabsf(sx)>fabsf(sy)) {
        if(!G::stickConsumed || now>G::lastRepeat){switchTab(sx>0?1:-1);G::lastRepeat=now+(!G::stickConsumed?300u:110u);G::stickConsumed=true;}
        G::prevBtn=b;tickAnim();return;
    }
    if (fabsf(sy) > 0.55f) {
        if (!G::stickConsumed || now > G::lastRepeat) {
            navigate(gp::verticalDirection(sy));
            bool first = !G::stickConsumed;
            G::stickConsumed = true;
            G::lastRepeat = now + (first ? 300u : 110u);
        }
    } else G::stickConsumed = false;

    // 右摇杆：阅读视图滚动
    if (G::view == 1) {
        float ry = G::pad.stickY(true, idx);
        if (fabsf(ry) > 0.12f) { cancelTouch(G::hwnd);G::readerTarget = std::max(0.0f, G::readerTarget - ry * 22.0f); G::dirty = true; }
    }

    G::prevBtn = b;
    tickAnim();
}
