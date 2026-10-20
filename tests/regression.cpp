// Self-contained fixtures only. No installed games, personal guides or screenshots are read.
#define WinMain SideDeckEntry
#include "../src/main.cpp"
#undef WinMain
#include <cassert>

static void writeText(const std::wstring& path, const std::string& text) {
    FILE* file = _wfopen(path.c_str(), L"wb"); assert(file);
    assert(fwrite(text.data(), 1, text.size(), file) == text.size()); fclose(file);
}
static void savePng(Bitmap& bitmap, const std::wstring& path) {
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0, 0, 0xf8, 0x1e, 0xf3, 0x2e}};
    assert(bitmap.Save(path.c_str(), &png, nullptr) == Ok);
}
static void snapshot(const std::wstring& path) {
    Bitmap bitmap(G::W, G::H, PixelFormat32bppPARGB);
    {
        Graphics graphics(&bitmap); graphics.SetSmoothingMode(SmoothingModeHighQuality);
        drawPanel(graphics);
    }
    Gdiplus::Color corner, glass;
    assert(bitmap.GetPixel(0, 0, &corner) == Ok && corner.GetA() == 0);
    assert(bitmap.GetPixel(G::W - 15, G::H / 2, &glass) == Ok && glass.GetA() > 70 && glass.GetA() < 250);
    savePng(bitmap, path);
}
int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0; GdiplusStartupInput startup;
    assert(GdiplusStartup(&token, &startup, nullptr) == Ok);
    {
        const auto root = lib::appDir();
        std::filesystem::create_directories(root + L"/guides");
        // Test patterns, deliberately unrelated to any game material.
        Bitmap landscape(512, 288, PixelFormat32bppARGB);
        {
            Graphics graphics(&landscape);
            LinearGradientBrush pattern(Point(0, 0), Point(512, 288), Gdiplus::Color(255, 30, 80, 120), Gdiplus::Color(255, 140, 180, 200));
            graphics.FillRectangle(&pattern, 0, 0, 512, 288);
        }
        savePng(landscape, root + L"/landscape.png");
        Bitmap portrait(100, 200, PixelFormat32bppARGB);
        { Graphics graphics(&portrait); graphics.Clear(Gdiplus::Color(255, 100, 150, 190)); }
        savePng(portrait, root + L"/portrait.png");
        const std::string html = R"HTML(<html><head><title>示例图鉴</title><style>hidden-style</style></head><body>
<h1>示例图鉴</h1><p>本文件是测试样例。</p><h2>示例分组</h2><table><tbody>
<tr><td><img src="portrait.png"></td><td>样例甲<span>未获取</span></td><td>Example A</td><td>条件甲<div>位置甲</div><img src="landscape.png"></td><td>区域甲</td></tr>
<tr><td><img src="portrait.png"></td><td>样例乙<span>已获取</span></td><td>Example B</td><td>条件乙</td><td>区域乙</td></tr>
</tbody></table><p>测试备注</p><script>never-execute()</script></body></html>)HTML";
        writeText(root + L"/catalog.html", html);
        writeText(root + L"/guides/demo.md", "# 示例阅读\n\n## 小节\n\n- 测试条目\n\n![示例](../landscape.png)\n");
        auto document = htmlguide::parse(html);
        assert(document.title == "示例图鉴" && document.entries.size() == 2);
        assert(document.entries[0].title == "样例甲" && document.entries[1].title == "样例乙");
        assert(document.overview.find("测试备注") != std::string::npos);
        assert(document.overview.find("never-execute") == std::string::npos);
        assert(htmlguide::entities("&#x4F8B;&#23376;") == "例子");
        auto lines = parseMarkdown(document.entries[0].body);
        assert(std::count_if(lines.begin(), lines.end(), [](const MdLine& line) { return line.type == MD_IMAGE; }) == 2);
        ui::theme() = ui::themePreset(0);
        G::cfg.scanSteam = G::cfg.scanEpic = G::cfg.scanStartMenu = false;
        G::cfg.hideOnFocusLost = false;
        rebuildTabs(); assert(G::tabs[1].items.size() == 2);
        G::W = 621; G::H = 900; ensureDib(G::W, G::H);
        G::W = 740; G::H = 1000; ensureDib(G::W, G::H);
        BITMAP dib{}; assert(GetObjectW(G::hDib, sizeof(dib), &dib));
        assert(dib.bmWidth == 740 && dib.bmHeight == 1000);
        G::W = 621; G::H = 900; ensureDib(G::W, G::H);
        assert(gp::verticalDirection(1) == -1 && gp::verticalDirection(-1) == 1);
        assert(gp::Input::norm(32767) == 1 && gp::Input::norm(-32768) == -1);
        gp::Pad neutral{}; assert(gp::isNeutral(neutral));
        neutral.wButtons = gp::B; assert(!gp::isNeutral(neutral));
        neutral = {}; neutral.sThumbRY = 10000; assert(!gp::isNeutral(neutral));
        neutral = {}; neutral.sThumbLY = -10000; assert(!gp::isNeutral(neutral));
        neutral = {}; neutral.bLeftTrigger = 31; assert(!gp::isNeutral(neutral));
        neutral = {}; neutral.sThumbRX = -8000; assert(gp::isNeutral(neutral));
        std::filesystem::create_directories(root + L"/import-fixtures/目录.exe");
        auto first = root + L"/import-fixtures/游戏 甲.exe";
        auto second = root + L"/import-fixtures/游戏 乙.EXE";
        auto shortcut = root + L"/import-fixtures/快捷方式.lnk";
        writeText(first, "test fixture; never executed"); writeText(second, "test fixture; never executed"); writeText(shortcut, "test fixture; never executed");
        lib::Config imported; lib::Item existing; existing.target = first; imported.manual.push_back(existing);
        auto batch = lib::addManualFiles(imported, {lib::toLowerCp(first), second, shortcut, shortcut, root + L"/import-fixtures/目录.exe", root + L"/readme.txt", root + L"/absent.exe"});
        assert(batch.added == 2 && batch.duplicates == 2 && batch.unsupported == 1 && batch.missing == 2);
        assert(imported.manual.size() == 3 && lib::manualPathKey(imported.manual[0].target) == lib::manualPathKey(second) && lib::manualPathKey(imported.manual[1].target) == lib::manualPathKey(shortcut));
        auto repeated = lib::addManualFiles(imported, {first, second, shortcut});
        assert(repeated.added == 0 && repeated.duplicates == 3 && imported.manual.size() == 3);
        auto image = G::imageCache.get(root + L"/landscape.png", 0);
        assert(image && image->GetWidth() == 512 && image->GetHeight() == 288);
        auto thumbnail = G::iconCache.get(root + L"/portrait.png", -48);
        assert(thumbnail && thumbnail->GetWidth() == 48 && thumbnail->GetHeight() == 48);
        G::cfg.extraTabs.resize(24); rebuildTabs(); assert(G::tabs.size() == 27 && G::sel.size() == 27);
        G::tabIndex = 26; switchTab(1); assert(G::tabIndex == 0);
        G::cfg.extraTabs.clear(); rebuildTabs();
        G::anim = G::pageAnim = 1; G::visible = true; ui::g_textScale = G::cfg.scale;
        for (int i = 0; i < 14; ++i) {
            Item item; item.kind = lib::Guide; item.title = L"示例项目 " + std::to_wstring(i); item.subtitle = L"回归测试";
            item.target = root + L"/guides/demo.md"; G::tabs[0].items.push_back(item);
        }
        snapshot(root + L"/list.png");
        int rowY = (int)(listTop() + 2 * (ITEM_H + ITEM_GAP) + 20);
        G::dirty = false; WndProc(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(80, rowY));
        assert(G::sel[0] == 2 && G::dirty);
        WndProc(nullptr, WM_LBUTTONUP, 0, MAKELPARAM(80, G::H - 10)); assert(G::sel[0] == 2);
        auto tab = G::tabRects[1];
        WndProc(nullptr, WM_LBUTTONUP, 0, MAKELPARAM((int)tab.X + 5, (int)tab.Y + 5)); assert(G::tabIndex == 1);
        auto& guides = G::tabs[1].items;
        auto catalog = std::find_if(guides.begin(), guides.end(), [](const Item& item) { return item.target.find(L"catalog.html") != std::wstring::npos; });
        assert(catalog != guides.end()); G::sel[1] = (int)(catalog - guides.begin());
        openSelected(); assert(G::htmlTabIndex == 3 && G::tabs[3].items.size() == 3);
        G::sel[3] = 1; openSelected(); assert(G::view == 1 && G::readerTitle == L"样例甲");
        G::readerScroll = 80; openSelected(); assert(G::readerScroll == 80);
        switchTab(1); assert(G::tabIndex == 3);
        goBack(); assert(G::view == 0 && G::tabIndex == 3);
        goBack(); assert(G::tabIndex == 1);
        G::tabIndex = 2; G::sel[2] = 0; openSelected(); assert(G::themePicker);
        navigate(3); assert(G::themePick == 3 && G::cfg.theme == 0);
        goBack(); assert(!G::themePicker && G::cfg.theme == 0);
        openSelected(); auto sheet = themeSheetRect();
        float rowH = (sheet.Height - 100 * S()) / ui::THEME_COUNT;
        WndProc(nullptr, WM_LBUTTONUP, 0, MAKELPARAM((int)sheet.X + 40, (int)(sheet.Y + 58 * S() + 2.5f * rowH)));
        assert(!G::themePicker && G::cfg.theme == 2);
        lib::Config loaded; lib::loadConfig(loaded); assert(loaded.theme == 2);
        for (int i = 0; i < 30; ++i) tickAnim(1.0f / 60);
        assert(G::themeProgress == 1 && ui::theme().glassBottom.a == ui::themePreset(2).glassBottom.a);
        G::toastT = 0; G::focusReady = true; G::focusY = 0; G::focusTarget = 100;
        tickAnim(0.1f); float slowFrame = G::focusY; G::focusY = 0;
        for (int i = 0; i < 6; ++i) tickAnim(1.0f / 60);
        assert(fabsf(G::focusY - slowFrame) < 0.01f);
        G::anim = 0; G::focusReady = G::pillReady = false;
        for (int i = 0; i < 15; ++i) tickAnim(0.016f); assert(G::anim > 0.999f);
        hidePanel(); tickAnim(0.09f); assert(G::closing && G::anim > 0.45f && G::anim < 0.55f);
        showPanel(); assert(!G::closing && G::visible); tickAnim(0.12f); assert(G::anim > 0.99f);
        hidePanel(); tickAnim(0.2f); assert(!G::visible && !G::closing);
        G::visible = true; G::anim = G::pageAnim = 1;
        G::backdrop = ui::blurBackdrop(&landscape, G::W, G::H);
        for (int i = 0; i < ui::THEME_COUNT; ++i) {
            G::cfg.theme = i; ui::theme() = ui::themePreset(i); G::focusReady = G::pillReady = false;
            snapshot(root + L"/theme-" + std::to_wstring(i) + L".png");
        }
        G::cfg.scale = 1.8f; ui::g_textScale = G::cfg.scale;
        G::focusReady = G::pillReady = false; G::sel[2] = 0; G::scroll[2] = 0;
        snapshot(root + L"/tools-large.png");
        writeText(lib::configPath(), R"({"theme":999,"scale":-8,"width":0,"margin":-3})");
        lib::Config bounds; lib::loadConfig(bounds);
        assert(bounds.theme == 3 && bounds.scale == 0.75f && bounds.width == 320 && bounds.margin == 0);
        DeleteFileW(lib::configPath().c_str());
        delete G::backdrop; G::backdrop = nullptr;
        G::iconCache.clear(); G::imageCache.clear(); releaseDib();
    }
    GdiplusShutdown(token); CoUninitialize();
    puts("PASS: synthetic guides, image sizing, batch imports/order/duplicates/validation, neutral controller handoff, tabs, mouse navigation, themes, transparency, motion and cleanup");
}
