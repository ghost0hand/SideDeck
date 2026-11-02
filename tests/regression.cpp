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
    int probeCount=0;auto probeArgs=CommandLineToArgvW(GetCommandLineW(),&probeCount);
    if(probeArgs && probeCount>=2 && std::wstring(probeArgs[1])==L"--launch-item") {
        if(probeCount==4 && std::wstring(probeArgs[3])==L"__fixture_launch_crash__") {
            TerminateProcess(GetCurrentProcess(),0xC0000005);return 1;
        }
        int result=launchdispatch::run(probeCount,probeArgs);LocalFree(probeArgs);return result;
    }
    if(probeArgs && probeCount==5 && std::wstring(probeArgs[1])==L"--launch-probe") {
        wchar_t cwd[32768]{};GetCurrentDirectoryW(32768,cwd);
        sj::Value output(sj::Type::Object);output["cwd"]=ui::narrow(cwd);
        output["argument"]=ui::narrow(probeArgs[3]);output["tail"]=ui::narrow(probeArgs[4]);
        auto temporary=std::wstring(probeArgs[2])+L".tmp";
        writeText(temporary,sj::dump(output));
        MoveFileExW(temporary.c_str(),probeArgs[2],MOVEFILE_REPLACE_EXISTING);
        LocalFree(probeArgs);return 0;
    }
    if(probeArgs)LocalFree(probeArgs);
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
        isolation::Gate gate;gp::Pad held{};held.wButtons=gp::A;
        assert(gate.output(held,false,true).wButtons==0); // startup waits for release
        assert(gate.output({},false,true).wButtons==0);
        assert(gate.output(held,false,true).wButtons==gp::A);
        assert(gate.output(held,true,true).wButtons==0); // panel consumes input
        assert(gate.output(held,false,true).wButtons==0); // close cannot leak held A
        gate.output({},false,true);assert(gate.output(held,false,true).wButtons==gp::A);
        assert(gate.output(held,false,false).wButtons==0); // disconnect resets gate
        assert(gate.output(held,false,true).wButtons==0);
        held={};held.sThumbLX=24000;held.bRightTrigger=200;
        assert(gate.output(held,false,true).sThumbLX==0);
        gate.output({},false,true);auto forwarded=gate.output(held,false,true);
        assert(forwarded.sThumbLX==24000 && forwarded.bRightTrigger==200);
        assert(isolation::contains({L"USB\\Demo"},L"usb\\demo"));
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
        // Real shell launch of a self-contained probe, never a user's game.
        auto launchRoot=std::filesystem::path(root)/L"launch-fixtures";
        auto launchWork=launchRoot/L"游戏 实际起始目录";auto linkFolder=launchRoot/L"桌面快捷方式";
        std::filesystem::create_directories(launchWork);std::filesystem::create_directories(linkFolder);
        auto launchLink=linkFolder/L"测试 游戏.LNK";auto report=launchRoot/L"probe.json";
        wchar_t testExe[32768]{};GetModuleFileNameW(nullptr,testExe,32768);
        IShellLinkW* shellLink=nullptr;assert(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shellLink))));
        assert(SUCCEEDED(shellLink->SetPath(testExe)));
        auto parameters=L"--launch-probe \""+report.wstring()+L"\" \"中文 参数\" --renderer=vulkan";
        assert(SUCCEEDED(shellLink->SetArguments(parameters.c_str())));
        assert(SUCCEEDED(shellLink->SetWorkingDirectory(launchWork.c_str())));
        assert(SUCCEEDED(shellLink->SetShowCmd(SW_HIDE)));
        IPersistFile* linkFile=nullptr;assert(SUCCEEDED(shellLink->QueryInterface(IID_PPV_ARGS(&linkFile))));
        assert(SUCCEEDED(linkFile->Save(launchLink.c_str(),TRUE)));
        auto waitProbe=[&](HANDLE process){
            assert(process && WaitForSingleObject(process,10000)==WAIT_OBJECT_0);
            DWORD code=99;assert(GetExitCodeProcess(process,&code) && code==0);CloseHandle(process);
            return sj::Parser(lib::readTextFile(report.wstring())).parse();
        };
        lib::Item launchItem;launchItem.target=launchLink.wstring();auto request=lib::prepareLaunch(launchItem);
        assert(request.file==launchLink.wstring() && request.parameters.empty() && request.directory==launchWork.wstring());
        DeleteFileW(report.c_str());HANDLE launched=nullptr;assert(lib::launch(launchItem,nullptr,&launched)==ERROR_SUCCESS);
        auto launchedReport=waitProbe(launched);
        assert(lib::manualPathKey(ui::utf8(launchedReport.getStr("cwd")))==lib::manualPathKey(launchWork.wstring()));
        assert(launchedReport.getStr("argument")=="中文 参数" && launchedReport.getStr("tail")=="--renderer=vulkan");
        DeleteFileW(report.c_str());HANDLE dispatch=nullptr;assert(launchdispatch::start(launchItem,dispatch)==ERROR_SUCCESS);
        assert(WaitForSingleObject(dispatch,10000)==WAIT_OBJECT_0);DWORD dispatchCode=99;
        assert(GetExitCodeProcess(dispatch,&dispatchCode) && dispatchCode==0);CloseHandle(dispatch);
        for(int i=0;i<200 && !lib::fileExists(report.wstring());++i)Sleep(10);
        auto dispatched=sj::Parser(lib::readTextFile(report.wstring())).parse();
        assert(dispatched.getStr("argument")=="中文 参数" && dispatched.getStr("tail")=="--renderer=vulkan");
        assert(lib::manualPathKey(ui::utf8(dispatched.getStr("cwd")))==lib::manualPathKey(launchWork.wstring()));
        lib::Item failedChild;failedChild.target=L"__fixture_launch_crash__";
        assert(launchdispatch::start(failedChild,dispatch)==ERROR_SUCCESS);
        assert(WaitForSingleObject(dispatch,10000)==WAIT_OBJECT_0);
        assert(GetExitCodeProcess(dispatch,&dispatchCode) && dispatchCode==0xC0000005);CloseHandle(dispatch);
        // The parent remains alive after a child exits with the observed fault code.
        wchar_t systemDir[MAX_PATH]{};GetSystemDirectoryW(systemDir,MAX_PATH);
        auto iconResource=std::wstring(systemDir)+L"\\shell32.dll";
        assert(SUCCEEDED(shellLink->SetIconLocation(iconResource.c_str(),0)));
        assert(SUCCEEDED(linkFile->Save(launchLink.c_str(),TRUE)));
        auto shortcutIcon=G::iconCache.get(launchLink.wstring(),48);
        assert(shortcutIcon && shortcutIcon->GetWidth()==48);
        for(auto text:{L"",L"trailing\\",L"含空格 text",L"embedded\"quote",L"\\\\\"tail\\"}) {
            auto cmd=L"probe "+launchdispatch::quote(text);int n=0;auto parsed=CommandLineToArgvW(cmd.c_str(),&n);
            assert(parsed && n==2 && std::wstring(parsed[1])==text);LocalFree(parsed);
        }
        SetEnvironmentVariableW(L"SIDEDECK_TEST_WORK",launchWork.c_str());
        assert(SUCCEEDED(shellLink->SetWorkingDirectory(L"%SIDEDECK_TEST_WORK%")));assert(SUCCEEDED(linkFile->Save(launchLink.c_str(),TRUE)));
        assert(lib::prepareLaunch(launchItem).directory==launchWork.wstring());
        assert(SUCCEEDED(shellLink->SetWorkingDirectory(L"")));assert(SUCCEEDED(linkFile->Save(launchLink.c_str(),TRUE)));
        assert(lib::prepareLaunch(launchItem).directory==std::filesystem::path(testExe).parent_path().wstring());
        linkFile->Release();shellLink->Release();SetEnvironmentVariableW(L"SIDEDECK_TEST_WORK",nullptr);
        lib::Item missingItem;missingItem.target=(launchRoot/L"missing.exe").wstring();
        assert(lib::launch(missingItem)!=ERROR_SUCCESS);
        lib::Item directItem;directItem.target=testExe;assert(lib::prepareLaunch(directItem).directory==std::filesystem::path(testExe).parent_path().wstring());
        lib::Item protocol;protocol.kind=lib::Url;protocol.target=L"steam://rungameid/123";assert(lib::prepareLaunch(protocol).directory.empty());
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
        assert(libraryindex::initial(L"《剑星》")==L'J');
        assert(libraryindex::initial(L"艾尔登法环")==L'A');
        assert(libraryindex::initial(L"八方旅人")==L'B');
        assert(libraryindex::initial(L"绯红结系")==L'F');
        assert(libraryindex::initial(L"黑神话")==L'H');
        assert(libraryindex::initial(L"只狼")==L'Z');
        assert(libraryindex::initial(L"portal 2")==L'P');
        assert(libraryindex::initial(L"123 Demo")==L'#');
        const std::wstring samples=L"阿八刺底恶法古黑剑空龙魔尼欧匹七热赛泰巫星伊战",expected=L"ABCDEFGHJKLMNOPQRSTWXYZ";
        assert(samples.size()==expected.size());
        for(size_t i=0;i<samples.size();++i)assert(libraryindex::initial(samples.substr(i,1))==expected[i]);
        assert(libraryindex::find({},L'A')==-1);
        G::cfg.scale=1.35f;ui::g_textScale=G::cfg.scale;G::cfg.manual.clear();G::cfg.favorites.clear();
        for(auto title:{L"剑星",L"只狼",L"艾尔登法环",L"八方旅人",L"绯红结系",L"黑神话",L"Portal 2",L"123 Demo"}) {
            Item item;item.title=title;item.kind=lib::App;item.target=root+L"/"+title+L".exe";item.iconPath=root+L"/landscape.png";G::cfg.manual.push_back(item);
        }
        G::tabIndex=0;G::view=0;rebuildTabs();assert(G::tabs[0].items.size()==8);
        assert(G::tabs[0].items.front().title==L"艾尔登法环" && G::tabs[0].items.back().title==L"123 Demo");
        jumpTo(L'J');assert(G::tabs[0].items[G::sel[0]].title==L"剑星");
        toggleFavorite();lib::Config favoriteSaved;lib::loadConfig(favoriteSaved);
        assert(favoriteSaved.manual.size()==8 && favoriteSaved.favorites.size()==1);
        Item sameGame=G::tabs[0].items[G::sel[0]];sameGame.target=lib::toLowerCp(sameGame.target);
        assert(libraryindex::favorite(favoriteSaved,sameGame));
        toggleFavoritesOnly();assert(G::tabs[0].items.size()==1 && G::tabs[0].items[0].pinned);
        toggleFavorite();assert(G::tabs[0].items.empty());
        toggleFavoritesOnly();assert(G::tabs[0].items.size()==8);
        toggleFavorite();G::focusReady=G::pillReady=false;snapshot(root+L"/favorites.png");
        openJumpPicker();assert(G::jumpPicker && G::jumpAvailable[9] && !G::jumpAvailable[2]);
        snapshot(root+L"/letter-jump.png");
        navigate(1);assert(G::jumpIndex==6);switchTab(1);assert(G::jumpIndex==7 && G::tabIndex==0);
        jumpTo(L'J');assert(!G::jumpPicker && G::tabs[0].items[G::sel[0]].title==L"剑星");
        WndProc(nullptr,WM_KEYDOWN,L'B',0);assert(G::tabs[0].items[G::sel[0]].title==L"八方旅人");
        int beforeJump=G::sel[0];jumpTo(L'Q');assert(G::sel[0]==beforeJump);
        // 长按移除：只删条目，确认后配置与列表同步，移除名单能挡住重扫回来的条目。
        G::cfg.scanSteam=G::cfg.scanEpic=G::cfg.scanStartMenu=false;G::rescanRequested=true;
        G::tabIndex=0;G::view=0;rebuildTabs();
        int manualBefore=(int)G::cfg.manual.size(),itemsBefore=(int)G::tabs[0].items.size();
        assert(itemsBefore==manualBefore && itemsBefore>0);
        G::sel[0]=0;std::wstring removedTitle=G::tabs[0].items[0].title,removedTarget=G::tabs[0].items[0].target;
        openRemoveConfirm();assert(G::removeConfirm && G::removeItem.title==removedTitle);
        G::dirty=false;G::focusReady=G::pillReady=false;snapshot(root+L"/remove-confirm.png");
        assert(G::removeActions.size()==2);
        WndProc(nullptr,WM_SIDEDECK_TAP,0,MAKELPARAM((int)(G::removeActions[0].X+5),(int)(G::removeActions[0].Y+5)));
        assert(!G::removeConfirm && (int)G::cfg.manual.size()==manualBefore);
        // 键盘：长按 Delete 才进确认，短按不动作
        G::cfg.holdRemoveMs=400;
        WndProc(nullptr,WM_KEYDOWN,VK_DELETE,0);assert(!G::removeConfirm);
        Sleep(450);WndProc(nullptr,WM_KEYDOWN,VK_DELETE,(LPARAM)(1<<30));
        assert(G::removeConfirm && G::removeItem.title==removedTitle);
        WndProc(nullptr,WM_KEYUP,VK_DELETE,0);
        WndProc(nullptr,WM_KEYDOWN,VK_ESCAPE,0);assert(!G::removeConfirm);
        G::cfg.holdRemoveMs=800;assert(G::deleteKeyDown==0);
        WndProc(nullptr,WM_KEYDOWN,VK_DELETE,0);
        Sleep(850);WndProc(nullptr,WM_KEYDOWN,VK_DELETE,(LPARAM)(1<<30));
        assert(G::removeConfirm);
        WndProc(nullptr,WM_KEYDOWN,VK_RETURN,0);
        assert(!G::removeConfirm && (int)G::cfg.manual.size()==manualBefore-1);
        assert((int)G::tabs[0].items.size()==itemsBefore-1);
        assert(std::none_of(G::tabs[0].items.begin(),G::tabs[0].items.end(),[&](const Item& i){return i.title==removedTitle;}));
        {lib::Config persisted;lib::loadConfig(persisted);
         assert((int)persisted.manual.size()==manualBefore-1 && persisted.removed.size()==1);}
        Item ghost;ghost.kind=lib::App;ghost.title=L"移除样例";ghost.target=removedTarget;
        assert(libraryindex::removed(G::cfg,ghost));
        G::cfg.manual.push_back(ghost);rebuildTabs();
        assert(std::none_of(G::tabs[0].items.begin(),G::tabs[0].items.end(),[](const Item& i){return i.title==L"移除样例";}));
        libraryindex::forget(G::cfg.removed,libraryindex::identity(ghost));rebuildTabs();
        assert(std::any_of(G::tabs[0].items.begin(),G::tabs[0].items.end(),[](const Item& i){return i.title==L"移除样例";}));
        G::cfg.manual.pop_back();G::cfg.removed.clear();rebuildTabs();
        // 长按过程的可视反馈
        G::sel[0]=0;G::holdArmed=true;G::holdProgress=0.6f;G::focusReady=G::pillReady=false;
        snapshot(root+L"/remove-hold.png");
        G::holdArmed=false;G::holdProgress=0;G::deleteKeyDown=0;
        assert((int)G::cfg.manual.size()==manualBefore-1);
        {ui::IconCache background; background.enableAsync();assert(background.get(root+L"/landscape.png",48)==nullptr);
         for(int wait=0;wait<200 && background.revision()==0;++wait)Sleep(5);
         assert(background.revision()==1 && background.get(root+L"/landscape.png",48));background.clear();}
        {touchscroll::Gesture gesture;
         gesture.begin(100,300,0,true);assert(gesture.move(103,303,0.01,10,3000)==0);assert(!gesture.end(0.02));
         gesture.begin(100,300,1,true);assert(gesture.move(100,200,1.05,10,3000)==100);assert(gesture.end(1.06));
         assert(gesture.coast(0.016f)>0);
         gesture.begin(100,200,1.08,true);assert(gesture.end(1.09)); // tap stops an existing fling
         gesture.begin(100,200,2,false);assert(gesture.move(100,50,2.05,10,3000)==0);assert(gesture.end(2.06));
         gesture.begin(100,300,3,true);gesture.move(180,302,3.05,10,3000);assert(gesture.end(3.06)&&gesture.velocity==0);
         gesture.begin(100,300,4,true);gesture.move(100,200,4.05,10,3000);assert(gesture.end(4.3)&&gesture.velocity==0);
         touchscroll::Gesture coarse,fine;coarse.velocity=fine.velocity=1200;
         float distance=coarse.coast(0.1f),sum=0;for(int frame=0;frame<6;++frame)sum+=fine.coast(0.1f/6);
         assert(fabsf(distance-sum)<0.01f && fabsf(coarse.velocity-fine.velocity)<0.01f);}
        G::jumpPicker=G::themePicker=false;G::view=0;G::tabIndex=0;G::visible=true;G::closing=false;G::toastT=0;
        G::tabs[0].items.clear();
        for(int i=0;i<60;++i) {Item item;item.kind=lib::Guide;item.title=L"触控样例 "+std::to_wstring(i);item.target=root+L"/guides/demo.md";G::tabs[0].items.push_back(item);}
        cancelTouch();G::cancelledMouseTap=false;G::freeScroll=false;G::sel[0]=0;G::scroll[0]=G::scrollTarget[0]=0;
        int startY=(int)(listTop()+ITEM_H*3),endY=startY-180;
        WndProc(nullptr,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,startY));Sleep(12);
        WndProc(nullptr,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(100,endY));
        WndProc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(100,endY));
        assert(G::view==0 && G::sel[0]==0 && G::freeScroll && G::scroll[0]>150 && G::touch.velocity>0);
        float dragged=G::scroll[0];snapshot(root+L"/touch-list.png");
        assert(G::scrollTarget[0]==dragged); // drawing must not recenter the old selection
        tickAnim(0.016f);assert(G::scroll[0]>dragged);
        int firstVisible=(int)ceilf(G::scroll[0]/(ITEM_H+ITEM_GAP));navigate(1);
        assert(!G::freeScroll && G::sel[0]==firstVisible && G::touch.velocity==0);
        scrollPixels(1e6f,true);assert(G::scroll[0]==listMaximum() && G::scrollTarget[0]==listMaximum());
        scrollPixels(-1e6f,true);assert(G::scroll[0]==0);
        G::sel[0]=0;G::cancelledMouseTap=false;
        int tapY=(int)(listTop()+ITEM_H/2);
        WndProc(nullptr,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,tapY));
        WndProc(nullptr,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(102,tapY+2));
        WndProc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(102,tapY+2));assert(G::view==1); // small jitter remains a tap
        int backX=(int)PADX+10,backY=(int)HEADER_H;
        WndProc(nullptr,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(backX,backY));
        WndProc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(backX,backY));assert(G::view==0);
        G::sel[0]=0;G::scroll[0]=G::scrollTarget[0]=0;G::cancelledMouseTap=false;
        WndProc(nullptr,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,tapY));
        WndProc(nullptr,WM_CANCELMODE,0,0);WndProc(nullptr,WM_LBUTTONUP,0,MAKELPARAM(100,tapY));
        assert(G::view==0 && !G::touch.active); // lost capture cannot become a click
        WndProc(nullptr,WM_MOUSEWHEEL,MAKEWPARAM(0,-15),0);
        assert(G::freeScroll && fabsf(G::scrollTarget[0]-9*S())<0.1f);
        G::view=1;G::readerScroll=G::readerTarget=100;G::readerMaxScroll=500;
        assert(scrollPixels(150,true) && G::readerScroll==250);
        scrollPixels(1000,true);assert(G::readerScroll==500);scrollPixels(-1000,true);assert(G::readerScroll==0);
        cancelTouch();G::view=0;
        {Bitmap source(4,4,PixelFormat32bppPARGB);{Graphics g(&source);g.Clear(Gdiplus::Color(128,100,40,20));}
         std::unique_ptr<Bitmap> surface;wallpaper::raster(&source,surface,80,120);
         Gdiplus::Color color;surface->GetPixel(40,60,&color);
         assert(color.GetA()==128 && abs((int)color.GetR()-100)<=2 && abs((int)color.GetB()-20)<=2);
         auto pointer=surface.get();wallpaper::raster(&source,surface,80,120);assert(surface.get()==pointer);}
        {std::unique_ptr<Bitmap> output;
         for(bool tall:{true,false}){Bitmap source(tall?10:100,tall?100:10,PixelFormat32bppPARGB);
           {Graphics g(&source);g.Clear(Gdiplus::Color(255,0,180,0));SolidBrush red(Gdiplus::Color(255,255,0,0)),blue(Gdiplus::Color(255,0,0,255));
            g.FillRectangle(&red,0,0,tall?10:10,tall?10:10);g.FillRectangle(&blue,tall?0:90,tall?90:0,10,10);}
           wallpaper::raster(&source,output,100,100);Gdiplus::Color first,last;
           output->GetPixel(tall?50:0,tall?0:50,&first);output->GetPixel(tall?50:99,tall?99:50,&last);
           assert(first.GetR()>240 && last.GetB()>240);
           wallpaper::raster(&source,output,100,100,"cover");output->GetPixel(50,50,&first);assert(first.GetG()>170);
         }}
        // Import must be self-contained, reject malformed data and preserve stable selection.
        auto fixtures=std::filesystem::path(root)/(L"theme-fixtures-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(fixtures/L"source");
        auto source=fixtures/L"source"/L"theme.json";
        savePng(portrait,(fixtures/L"source"/L"art.png").wstring());
        std::string valid=R"({"format":"sidedeck-theme","version":1,"name":"Test glass","base":1,"background":"art.png","colors":{"accentA":"#12ABEF","glassTop":"#12345678"},"imageOpacity":0.8})";
        writeText(source.wstring(),valid);themes::Entry entry;
        assert(themes::load(source,entry) && entry.palette.accentA.g==0xAB && entry.palette.glassTop.a==0x78);
        auto imports=fixtures/L"imports";std::wstring key,secondKey;
        assert(themes::import(source,imports,key));assert(themes::import(source,imports,secondKey) && key!=secondKey);
        std::filesystem::remove(fixtures/L"source"/L"art.png");
        assert(themes::load(imports/key,entry)); // source files are no longer required
        themes::reload(imports);assert(themes::count()==6);
        int importedTheme=themes::selected(0,key);assert(importedTheme>=4);
        applyTheme(importedTheme);lib::Config restored;lib::loadConfig(restored);
        assert(restored.themeFile==key && restored.theme==1 && G::themeImage);
        std::filesystem::create_directories(imports/L"aaa");
        writeText((imports/L"aaa"/L"theme.json").wstring(),R"({"format":"sidedeck-theme","version":1,"name":"Sorted first"})");
        themes::reload(imports);assert(themes::selected(restored.theme,restored.themeFile)!=importedTheme);
        assert(themes::get(themes::selected(restored.theme,restored.themeFile)).key==key);
        G::themePicker=true;G::themePick=0;navigate(4);assert(themePageStart()==4);
        sheet=themeSheetRect();
        WndProc(nullptr,WM_SIDEDECK_TAP,0,MAKELPARAM((int)(sheet.X+24*S()),(int)(sheet.GetBottom()-20*S())));
        assert(G::themePick==0); // touch previous page
        WndProc(nullptr,WM_SIDEDECK_TAP,0,MAKELPARAM((int)(sheet.GetRight()-24*S()),(int)(sheet.GetBottom()-20*S())));
        assert(G::themePick==4);goBack();
        for(auto bad:{
            R"({"format":"sidedeck-theme","version":1,"name":"Bad","videoFit":"stretch"})",
            R"({"format":"sidedeck-theme","version":1,"name":"Bad","background":"../secret.png"})",
            R"({"format":"sidedeck-theme","version":1,"name":"Bad","imageOpacity":2})",
            R"({"format":"sidedeck-theme","version":1,"name":"Bad","base":0.5})",
            R"({"format":"sidedeck-theme","version":1,"name":"Bad","colors":{"text":"#gggggg"}})",
            R"({"format":"sidedeck-theme","version":1,"name":"Bad","colors":{"unknown":"#FFFFFF"}})",
            R"({"format":"sidedeck-theme","version":1,"name":"Bad"} garbage)"}) {
            writeText(source.wstring(),bad);assert(!themes::load(source,entry));assert(!themes::import(source,imports,secondKey));
        }
        writeText(source.wstring(),std::string(33000,' '));assert(!themes::load(source,entry));
        std::string deep(100,'[');deep+=std::string(100,']');writeText(source.wstring(),deep);assert(!themes::load(source,entry));
        auto fakeVideo=fixtures/L"sample.mp4";writeText(fakeVideo.wstring(),"synthetic video metadata fixture");
        std::wstring videoKey;assert(themes::import(fakeVideo,imports,videoKey));
        assert(themes::load(imports/videoKey,entry) && entry.video.filename()==L"wallpaper.mp4");
        assert(lib::readTextFile(entry.video.wstring())=="synthetic video metadata fixture");
        writeText(source.wstring(),R"({"format":"sidedeck-theme","version":1,"name":"Bad video","video":"../sample.mp4"})");
        assert(!themes::load(source,entry));
        // Render every shipping theme with synthetic content, no personal library.
        themes::reload(std::filesystem::path(root)/L".."/L".."/L"theme-packs");assert(themes::count()==9);
        G::view=0;G::tabIndex=2;G::scroll[2]=G::scrollTarget[2]=0;G::sel[2]=0;G::cfg.scale=1;ui::g_textScale=1;
        G::W=540;G::H=960;G::toastT=0;G::anim=G::pageAnim=1;G::themePicker=false;
        for(int i=4;i<themes::count();++i){
            applyTheme(i);G::themeProgress=1;ui::theme()=currentThemeEntry().palette;G::toastT=0;
            G::focusReady=G::pillReady=false;
            snapshot(root+L"/pack-"+std::to_wstring(i)+L".png");
        }
        G::themePicker=true;G::themePick=4;snapshot(root+L"/theme-picker-page2.png");G::themePicker=false;
        G::themeImage.reset();G::cfg.themeFile.clear();themes::reload(fixtures/L"empty");
        writeText(lib::configPath(), R"({"theme":999,"scale":-8,"width":0,"margin":-3})");
        lib::Config bounds; lib::loadConfig(bounds);
        assert(bounds.theme == 3 && bounds.scale == 0.75f && bounds.width == 320 && bounds.margin == 0);
        DeleteFileW(lib::configPath().c_str());
        delete G::backdrop; G::backdrop = nullptr;
        G::iconCache.clear(); G::imageCache.clear();G::rowCache.clear();G::glassCache.reset();G::themeImage.reset();ui::fontCache.clear(); releaseDib();
    }
    GdiplusShutdown(token); CoUninitialize();
    puts("PASS: shortcut launch, child fault isolation, arguments, working directory, safe shortcut icons, theme import, paging, touch, favorites, pinyin, entry removal, guides and cleanup");
}
