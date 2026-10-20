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
int tabIndex = 0;
std::vector<int> sel;
std::vector<float> scroll, scrollTarget;
ui::IconCache iconCache, imageCache;

HWND hwnd = nullptr;
HINSTANCE hInst = nullptr;
gp::Input pad;

bool visible = false;
bool closing = false;
float anim = 0.0f;          // 0..1
ULONGLONG animTick = 0;
float pageAnim = 1, pageDirection = 1;
float focusY = 0, focusTarget = 0;
int focusTab = -1;
bool focusReady = false;
float tabPillX = 0, tabPillW = 0, tabPillTargetX = 0, tabPillTargetW = 0;
bool pillReady = false;
bool themePicker = false;
int themePick = 0;
ui::Theme themeFrom;
float themeProgress = 1;
bool animating = false;
ui::Bitmap* backdrop = nullptr;

int view = 0;               // 0=列表 1=阅读
std::wstring readerTitle;
std::wstring readerPath;
float readerScroll = 0, readerTarget = 0;
bool captureHotkey = false; // 正在录制呼出键
WORD recordedMask = 0;
bool recordArmed = false;
bool waitForRelease = false;
HWND foregroundAtShow = nullptr;
HWND lastExternalForeground = nullptr;
focusguard::Guard inputFocus;
bool restoreOnClose = false, waitingForRelease = false, padNeutral = true, dialogOpen = false;
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
bool rescanRequested = false;
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

// ==================== 绘制 ====================
// UI 缩放：手持设备/电视串流场景整体放大
static float S() { return G::cfg.scale; }
#define PADX     (20.0f * S())
#define HEADER_H (80.0f * S())
#define TAB_H    (46.0f * S())
#define FOOTER_H (48.0f * S())
#define ITEM_H   (72.0f * S())
#define ITEM_GAP (10.0f * S())

static float listTop() { return HEADER_H + TAB_H + 12 * S(); }
static float listBottom() { return G::H - FOOTER_H - 10 * S(); }

static void drawItemCard(Graphics& g, const Item& it, float x, float y, float w, float h, bool selected, float appear) {
    auto& T = ui::theme();
    float pad = 12.0f * S();
    float iconS = h - pad * 2;
    float ix = x + pad, iy = y + pad;
    ui::ToolGlyph glyph = ui::ToolGlyph::None;
    if (it.kind == lib::Command) {
        if (it.target == L"__theme__") glyph = ui::ToolGlyph::Settings;
        else if (it.target == L"__hotkey__") glyph = ui::ToolGlyph::Gamepad;
        else if (it.target == L"__addgames__") glyph = ui::ToolGlyph::Folder;
        else if (it.target == L"__rescan__") glyph = ui::ToolGlyph::Refresh;
        else if (it.target == L"__opendir__") glyph = ui::ToolGlyph::Folder;
        else if (it.target == L"__openguides__") glyph = ui::ToolGlyph::Book;
        else if (it.target == L"__editconfig__") glyph = ui::ToolGlyph::Settings;
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
    float titleH = 24 * S(), subH = 18 * S(), gap = 4 * S();
    float textY = y + (h - titleH - gap - subH) / 2;
    ui::TextStyle t1; t1.size = 14.5f; t1.bold = selected;
    t1.color = selected ? T.text : T.text;
    ui::drawText(g, it.title, tx, textY, tw, titleH, t1);
    ui::TextStyle t2; t2.size = 11.5f; t2.color = T.textFaint;
    std::wstring sub = it.subtitle.empty() ? it.target : it.subtitle;
    ui::drawText(g, sub, tx, textY + titleH + gap, tw, subH, t2);
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
    G::readerTarget = std::clamp(G::readerTarget, 0.0f, std::max(0.0f, y + G::readerScroll - clip.GetBottom()));
}

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
    float rowH = (sheet.Height - 100 * S()) / ui::THEME_COUNT;
    for (int i = 0; i < ui::THEME_COUNT; ++i) {
        float y = sheet.Y + 58 * S() + i * rowH;
        float x = sheet.X + 12 * S(), width = sheet.Width - 24 * S();
        if (i == G::themePick) {
            ui::fillRoundRect(g, T.selBg, x, y, width, rowH - 5 * S(), 14 * S());
            ui::drawRoundRect(g, T.sep, x, y, width, rowH - 5 * S(), 14 * S());
        }
        auto swatch = ui::themePreset(i);
        float side = std::min(42 * S(), rowH - 18 * S());
        float sx = x + 12 * S(), sy = y + (rowH - 5 * S() - side) / 2;
        ui::fillRoundRect(g, swatch.glassBottom.withA(255), sx, sy, side, side, 12 * S());
        ui::fillRoundRect(g, swatch.accentA.withA(150), sx + side * 0.18f, sy + side * 0.22f, side * 0.64f, side * 0.22f, 4 * S());
        ui::fillRoundRect(g, swatch.selBg.withA(120), sx + side * 0.18f, sy + side * 0.58f, side * 0.44f, side * 0.14f, 3 * S());
        ui::TextStyle name; name.size = 14; name.bold = i == G::themePick;
        ui::drawText(g, ui::themeName(i), sx + side + 14 * S(), y + 7 * S(), width - side - 86 * S(), 25 * S(), name);
        ui::TextStyle detail; detail.size = 10.5f; detail.color = T.textDim;
        ui::drawText(g, ui::themeDescription(i), sx + side + 14 * S(), y + 32 * S(), width - side - 40 * S(), 19 * S(), detail);
        if (i == G::cfg.theme) {
            ui::TextStyle current; current.size = 9; current.color = T.accentB; current.halign = StringAlignmentFar;
            ui::drawText(g, L"当前", x + width - 48 * S(), y + 9 * S(), 36 * S(), 20 * S(), current);
        }
    }
    ui::TextStyle hint; hint.size = 10.5f; hint.color = T.textDim; hint.halign = StringAlignmentCenter;
    ui::drawText(g, L"↑↓ 选择    A 应用    B 返回", sheet.X, sheet.GetBottom() - 34 * S(), sheet.Width, 23 * S(), hint);
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

    // 半透明模糊采样 + 分层玻璃色；保留真实的窗口透明度。
    auto glassState = g.Save();
    GraphicsPath silhouette; ui::roundRectPath(silhouette, 0, 0, w, h, 22 * S());
    g.SetClip(&silhouette, CombineModeIntersect);
    if (G::backdrop) {
        ui::drawImageOpacity(g, G::backdrop, 0, 0, w, h, T.backdropOpacity);
    }
    {
        LinearGradientBrush br(PointF(0, 0), PointF(0, h),
            T.glassTop.gd(), T.glassBottom.gd());
        g.FillRectangle(&br, 0.0f, 0.0f, w, h);
        LinearGradientBrush sheen(PointF(0, 0), PointF(w, h * 0.55f),
            T.rim.withA(24).gd(), T.rim.withA(0).gd());
        sheen.SetWrapMode(WrapModeTileFlipXY);
        g.FillRectangle(&sheen, 0.0f, 0.0f, w, h);
    }
    g.Restore(glassState);
    ui::drawRoundRect(g, T.rim, 0.75f, 0.75f, w - 1.5f, h - 1.5f, 22 * S(), 1.1f);

    // ---- 顶部 ----
    if (G::view == 0) {
        ui::TextStyle logo; logo.size = 13; logo.bold = true; logo.color = T.text;
        ui::drawText(g, L"SIDEDECK", PADX + 4 * S(), 18 * S(), 170 * S(), 28 * S(), logo);
        ui::TextStyle label; label.size = 9.5f; label.color = T.textDim;
        ui::drawText(g, std::wstring(L"v") + SIDEDECK_VERSION_W + L" · " + ui::themeName(G::cfg.theme), PADX + 4 * S(), 46 * S(), 200 * S(), 20 * S(), label);
    }
    // 右侧：手柄状态
    std::wstring status;
    if (G::padCount > 0) {
        status = L"手柄已连接";
    } else status = L"未检测到手柄";
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

        // ---- 列表 ----
        auto& items = G::tabs[G::tabIndex].items;
        float listTop = ::listTop();
        float listH = listBottom() - listTop;
        RectF clip(0, listTop, w, listH);
        auto saved = g.Save();
        g.SetClip(clip, CombineModeIntersect);

        G::scrollTarget[G::tabIndex] = std::max(0.0f,
            (float)G::sel[G::tabIndex] * (ITEM_H + ITEM_GAP) - listH / 2 + ITEM_H / 2);
        float maxScroll = std::max(0.0f, (float)items.size() * (ITEM_H + ITEM_GAP) - listH);
        G::scrollTarget[G::tabIndex] = std::min(G::scrollTarget[G::tabIndex], maxScroll);

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
        g.Restore(saved);

        if (items.empty()) {
            ui::TextStyle et; et.size = 13; et.color = T.textFaint; et.halign = StringAlignmentCenter;
            ui::drawText(g, L"空空如也 —— 拖个游戏进来，或托盘菜单添加", 0, listTop + 40.0f * S(), w, 30.0f * S(), et);
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
static void refreshBackdrop() {
    delete G::backdrop; G::backdrop = nullptr;
    G::backdrop = ui::grabBackdrop(G::PX, G::PY, G::W, G::H, 24, 4);
}

static void computeGeometry() {
    POINT pt{}; GetCursorPos(&pt);
    HMONITOR hm = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hm, &mi);
    int sw = mi.rcWork.right - mi.rcWork.left;
    int sh = mi.rcWork.bottom - mi.rcWork.top;
    int m = std::min(G::cfg.margin, std::min(sw, sh) / 8);
    G::H = std::max(1, sh - m * 2);
    G::W = std::clamp((int)(G::cfg.width * G::cfg.scale), 1, std::max(1, sw - m * 2));
    G::PY = mi.rcWork.top + m;
    G::PX = G::cfg.edge == 1 ? mi.rcWork.left + m : mi.rcWork.right - G::W - m;
}

static void showPanel() {
    if (G::visible) { G::closing = false; G::waitingForRelease = false; G::waitForRelease = true; return; }
    HWND previous = GetForegroundWindow();
    DWORD previousProcess = 0;
    if (previous) GetWindowThreadProcessId(previous, &previousProcess);
    if (previousProcess == GetCurrentProcessId()) previous = G::lastExternalForeground;
    G::foregroundAtShow = previous;
    computeGeometry();
    ensureDib(G::W, G::H);
    refreshBackdrop();
    G::visible = true;
    G::closing = false;
    G::waitForRelease = true;
    G::restoreOnClose = false; G::waitingForRelease = false;
    G::anim = 0.0f;
    G::animTick = GetTickCount64();
    G::focusReady = false; G::pillReady = false;
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
    if (!G::visible) return;
    G::closing = true;
    G::restoreOnClose = restoreFocus && G::inputFocus.ownsForeground();
    G::themePicker = false;
    G::captureHotkey = false;
    G::recordedMask = 0; G::recordArmed = false;
    ReleaseCapture();
}

// ==================== 数据 ====================
static void rebuildTabs() {
    G::tabs.clear();
    G::htmlTabIndex = -1;
    Tab games; games.name = L"游戏库";
    // 手动优先
    for (auto& it : G::cfg.manual) games.items.push_back(it);
    // 扫描（来自缓存或即时扫描）
    static std::vector<Item> scanned;
    if (scanned.empty() || G::rescanRequested) {
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
        bool dup = false;
        for (auto& m : games.items) if (m.target == it.target) { dup = true; break; }
        if (!dup) games.items.push_back(it);
    }
    G::tabs.push_back(games);

    Tab guides; guides.name = L"攻略";
    guides.items = lib::scanGuides();
    G::tabs.push_back(guides);

    Tab tools; tools.name = L"工具";
    Item appearance; appearance.title = L"外观主题";
    appearance.subtitle = std::wstring(ui::themeName(G::cfg.theme)) + L" · A 选择";
    appearance.target = L"__theme__"; appearance.kind = lib::Command;
    tools.items.push_back(appearance);
    Item hk; hk.title = L"呼出组合键"; hk.subtitle = L"当前：" + gp::Input::buttonNames((WORD)G::cfg.hotkeyMask);
    hk.target = L"__hotkey__"; hk.kind = lib::Command;
    tools.items.push_back(hk);
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
    G::tabs.push_back(tools);

    for (auto& t : G::cfg.extraTabs) G::tabs.push_back(t);

    if (G::tabIndex >= (int)G::tabs.size()) G::tabIndex = 0;
    G::sel.resize(G::tabs.size());
    G::scroll.resize(G::tabs.size());
    G::scrollTarget.resize(G::tabs.size());
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

static void applyTheme(int id) {
    id = std::clamp(id, 0, ui::THEME_COUNT - 1);
    G::themeFrom = ui::theme(); G::themeProgress = 0;
    G::cfg.theme = id;
    for (auto& tab : G::tabs) for (auto& item : tab.items) {
        if (item.target == L"__theme__") item.subtitle = std::wstring(ui::themeName(id)) + L" · A 选择";
    }
    G::themePicker = false;
    saveCfg(); toast(std::wstring(L"外观已切换 · ") + ui::themeName(id));
}

static void addGames(const std::vector<std::wstring>& paths) {
    auto result = lib::addManualFiles(G::cfg, paths);
    if (result.added) { saveCfg(); rebuildTabs(); G::tabIndex = 0; G::sel[0] = 0; }
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

// ==================== 动作 ====================
static void openSelected() {
    if (G::themePicker) { applyTheme(G::themePick); return; }
    if (G::view == 1) return;
    auto& items = G::tabs[G::tabIndex].items;
    LOG("openSelected called");
    if (items.empty()) return;
    Item it = items[G::sel[G::tabIndex]];
    if (it.target == L"__theme__") {
        G::themePicker = true; G::themePick = G::cfg.theme; G::dirty = true; return;
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
        G::readerScroll = 0; G::readerTarget = 0;
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
    lib::launch(it);
    hidePanel();
}

static void navigate(int delta) {
    if (G::themePicker) {
        G::themePick = std::clamp(G::themePick + delta, 0, ui::THEME_COUNT - 1);
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
    int before = G::sel[G::tabIndex];
    G::sel[G::tabIndex] = std::clamp(G::sel[G::tabIndex] + delta, 0, n - 1);
    if (G::sel[G::tabIndex] != before) G::dirty = true;   // 不重绘 = 看起来“翻不了页”
}

static void switchTab(int delta) {
    if (G::view == 1 || G::themePicker) return;
    int n = (int)G::tabs.size();
    if (n <= 1) return;
    G::tabIndex = (G::tabIndex + delta + n) % n;
    G::pageAnim = 0; G::pageDirection = delta >= 0 ? 1 : -1;
    G::dirty = true;
}

static void goBack() {
    if (G::themePicker) { G::themePicker = false; G::dirty = true; }
    else if (G::view == 1) { G::view = 0; G::dirty = true; G::pageAnim = 0; }
    else if (G::tabIndex == G::htmlTabIndex) { G::tabIndex = 1; G::dirty = true; }
    else hidePanel();
}

// ==================== 窗口过程 ====================
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (G::captureHotkey) break;
            if (G::themePicker) {
                RectF sheet = themeSheetRect();
                float rowH = (sheet.Height - 100 * S()) / ui::THEME_COUNT;
                float top = sheet.Y + 58 * S();
                if (x >= sheet.X + 12 * S() && x <= sheet.GetRight() - 12 * S() && y >= top && y < top + rowH * ui::THEME_COUNT) {
                    applyTheme((int)((y - top) / rowH));
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
            addGames(paths);
            break;
        }
        case WM_MOUSEWHEEL: {
            int d = ((short)HIWORD(wp)) > 0 ? -1 : 1;
            if (G::view == 1) {
                G::readerTarget = std::max(0.0f, G::readerTarget + d * 120.0f);
                G::dirty = true;
            } else if (G::visible) {
                navigate(d);
            }
            break;
        }
        case WM_KEYDOWN: {
            if (G::closing || G::dialogOpen) break;
            if (G::captureHotkey) {
                if (wp == VK_ESCAPE) { G::captureHotkey = false; G::recordedMask = 0; G::recordArmed = false; G::dirty = true; }
                break;
            }
            if (wp == VK_ESCAPE) { if (G::themePicker) goBack(); else hidePanel(); }
            else if (wp == VK_UP) navigate(-1);
            else if (wp == VK_DOWN) navigate(1);
            else if (wp == VK_LEFT) switchTab(-1);
            else if (wp == VK_RIGHT) switchTab(1);
            else if (wp == VK_RETURN) openSelected();
            else if (wp == VK_BACK) goBack();
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
    LOG("gdiplus ok");

    lib::loadConfig(G::cfg);
    ui::theme() = ui::themePreset(G::cfg.theme);
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
        // 自绘图标：32x32 渐变圆角块
        HDC scr = GetDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 32; bi.bmiHeader.biHeight = -32;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP hb = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        BYTE* p = (BYTE*)bits;
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            float t = (x + y) / 62.0f;
            int r = (int)(124 + (34 - 124) * t), g2 = (int)(92 + (211 - 92) * t), b = (int)(255 + (238 - 255) * t);
            BYTE a = 255;
            // 圆角
            int rad = 8; bool corner = false;
            if ((x < rad && y < rad) || (x > 31 - rad && y < rad) || (x < rad && y > 31 - rad) || (x > 31 - rad && y > 31 - rad)) {
                int cx = x < rad ? rad : 31 - rad, cy = y < rad ? rad : 31 - rad;
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > rad * rad) { a = 0; corner = true; }
            }
            // 中间三横条（列表感）
            if (!corner && y >= 10 && y <= 22 && (y % 6 != 5) && x >= 8 && x <= 24 && (y == 10 || y == 11 || y == 16 || y == 17 || y == 22)) {
                r = 255; g2 = 255; b = 255;
            }
            BYTE* q = p + (y * 32 + x) * 4;
            q[0] = (BYTE)b; q[1] = (BYTE)g2; q[2] = (BYTE)r; q[3] = a;
        }
        HBITMAP hmask = CreateBitmap(32, 32, 1, 1, nullptr);
        ICONINFO ii{};
        ii.fIcon = TRUE; ii.hbmColor = hb; ii.hbmMask = hmask;
        HICON icon = CreateIconIndirect(&ii);
        DeleteObject(hb); DeleteObject(hmask);
        ReleaseDC(nullptr, scr);

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
        AppendMenuW(G::hTrayMenu, MF_STRING, 1003, L"重新扫描游戏库");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1004, L"打开程序目录");
        AppendMenuW(G::hTrayMenu, MF_STRING, 1007, L"编辑配置");
        AppendMenuW(G::hTrayMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(G::hTrayMenu, MF_STRING, 1006, L"退出");
    }

    SetTimer(G::hwnd, 1, 16, nullptr);
    LOG("timer ok");
    RegisterHotKey(G::hwnd, 1, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE);   // Ctrl+空格 备用呼出

    if (!G::pad.available()) toast(L"未找到 XInput 运行时");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (G::trayAdded) Shell_NotifyIconW(NIM_DELETE, &G::nid);
    G::inputFocus.release(true);
    KillTimer(G::hwnd, 1);
    UnregisterHotKey(G::hwnd, 1);
    DestroyWindow(G::hwnd);
    if (G::hTrayMenu) DestroyMenu(G::hTrayMenu);
    if (G::nid.hIcon) DestroyIcon(G::nid.hIcon);
    delete G::backdrop;
    G::backdrop = nullptr;
    G::iconCache.clear(); G::imageCache.clear();
    releaseDib();
    GdiplusShutdown(token);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}

// ==================== 手柄轮询 + 动画 ====================
static void tickAnim(float elapsed = -1) {
    if (!G::visible) return;
    ULONGLONG now = GetTickCount64();
    float dt = elapsed >= 0 ? elapsed : (G::animTick ? std::min(0.05f, (now - G::animTick) / 1000.0f) : 0.016f);
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
            G::inputFocus.release(restore);
            G::restoreOnClose = false;
            G::visible = false; G::closing = false;
            ShowWindow(G::hwnd, SW_HIDE);
            delete G::backdrop; G::backdrop = nullptr;
            return;
        }
        need = true;
    } else if (G::anim < 1.0f) { G::anim = std::min(1.0f, G::anim + dt / 0.24f); need = true; }
    if (G::pageAnim < 1) { G::pageAnim = std::min(1.0f, G::pageAnim + dt / 0.20f); need = true; }
    if (G::themeProgress < 1) {
        G::themeProgress = std::min(1.0f, G::themeProgress + dt / 0.28f);
        ui::theme() = ui::mixTheme(G::themeFrom, ui::themePreset(G::cfg.theme), ui::easeOutCubic(G::themeProgress));
        need = true;
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
    HWND foreground = GetForegroundWindow();
    DWORD foregroundProcess = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundProcess);
    if (!G::visible && foreground && foregroundProcess != GetCurrentProcessId()) G::lastExternalForeground = foreground;
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
        if (b == 0 && fabsf(G::pad.stickY(false, idx)) < 0.55f) G::waitForRelease = false;
        G::prevBtn = b; tickAnim(); return;
    }

    // ---- 面板内导航 ----
    if (pressed & Btn::B) {
        goBack();
        G::prevBtn = b; tickAnim(); return;
    }
    if (pressed & Btn::A) { openSelected(); G::prevBtn = b; tickAnim(); return; }
    if (pressed & Btn::START) { hidePanel(); G::prevBtn = b; return; }
    if (pressed & Btn::LB) switchTab(-1);
    if (pressed & Btn::RB) switchTab(1);
    if (G::view == 1 && (pressed & Btn::X)) { G::readerTarget = 0; G::dirty = true; }

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
        if (fabsf(ry) > 0.12f) { G::readerTarget = std::max(0.0f, G::readerTarget - ry * 22.0f); G::dirty = true; }
    }

    G::prevBtn = b;
    tickAnim();
}
