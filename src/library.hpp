// 数据模型 / 配置读写 / 游戏库扫描（Steam、Epic、开始菜单）
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objbase.h>
#include <knownfolders.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <set>
#include <shobjidl.h>
#include "json.hpp"
#include "render.hpp"
#include "html_guide.hpp"

namespace lib {

namespace fs = std::filesystem;

enum Kind { App = 0, Url = 1, Guide = 2, Command = 3 };

struct Item {
    std::wstring title;
    std::wstring subtitle;
    std::wstring target;   // exe / url / md 路径 / 命令行
    std::wstring iconPath; // 空则取 target
    int kind = App;
    bool pinned = false;
    int launchCount = 0;
    std::string guideText; // HTML 图鉴条目的阅读内容，仅运行时使用
};

struct Tab {
    std::wstring name;
    std::vector<Item> items;
};

struct Config {
    int hotkeyMask = 0x0300;   // LB|RB 默认
    bool longBack = true;      // 长按 BACK 800ms 也可呼出
    int longPressMs = 800;
    int width = 460;
    int edge = 0;              // 0=右 1=左
    int margin = 24;
    bool scanSteam = true;
    bool scanEpic = true;
    bool scanStartMenu = false;
    bool hideOnFocusLost = true;
    float scale = 1.35f;       // UI 缩放（手持串流建议 1.3~1.6）
    int theme = 0;
    std::wstring themeFile;
    std::wstring pauseKey;     // 呼出时向游戏发送的按键（如 esc）
    bool inputIsolation = false;
    std::vector<std::wstring> favorites;
    std::vector<Item> manual;  // 手动添加
    std::vector<Tab> extraTabs;
};

// ---------------- 路径工具 ----------------
inline std::wstring appDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}
inline std::wstring guidesDir() { return appDir() + L"\\guides"; }

inline std::wstring regRead(HKEY root, const std::wstring& sub, const std::wstring& val) {
    wchar_t buf[1024]{}; DWORD sz = sizeof(buf);
    LONG r = RegGetValueW(root, sub.c_str(), val.c_str(), RRF_RT_REG_SZ, nullptr, buf, &sz);
    if (r != ERROR_SUCCESS) return L"";
    return std::wstring(buf);
}

inline bool fileExists(const std::wstring& p) {
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline std::string readTextFile(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string out;
    if (n > 0) { out.resize((size_t)n); fread(&out[0], 1, (size_t)n, f); }
    fclose(f);
    // 去 BOM
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return out;
}

// ---------------- VDF / 清单解析 ----------------
inline std::vector<std::wstring> steamLibraries(const std::wstring& steamPath) {
    std::vector<std::wstring> out;
    auto pushUniq = [&](const std::wstring& p) {
        for (auto& e : out) if (e == p) return;
        out.push_back(p);
    };
    pushUniq(steamPath);
    std::string vdf = readTextFile(steamPath + L"\\steamapps\\libraryfolders.vdf");
    if (vdf.empty()) return out;
    // v2: "path"		"D:\\SteamLibrary"
    size_t pos = 0;
    while ((pos = vdf.find("\"path\"", pos)) != std::string::npos) {
        size_t q1 = vdf.find('"', pos + 6);
        if (q1 == std::string::npos) break;
        size_t q2 = vdf.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string p = vdf.substr(q1 + 1, q2 - q1 - 1);
        std::replace(p.begin(), p.end(), '\\', '/');
        std::replace(p.begin(), p.end(), '/', '\\');
        pushUniq(ui::utf8(p));
        pos = q2 + 1;
    }
    return out;
}

inline std::wstring vdfField(const std::string& txt, const std::string& key) {
    std::string k = "\"" + key + "\"";
    size_t pos = txt.find(k);
    if (pos == std::string::npos) return L"";
    size_t q1 = txt.find('"', pos + k.size());
    if (q1 == std::string::npos) return L"";
    size_t q2 = txt.find('"', q1 + 1);
    if (q2 == std::string::npos) return L"";
    return ui::utf8(txt.substr(q1 + 1, q2 - q1 - 1));
}

// 在目录里挑一个最可能的启动 exe（浅两层，按"名字匹配 + 体积"评分，排除启动器/安装器）
inline std::wstring toLowerCp(std::wstring s) {
    for (auto& c : s) if (c >= L'A' && c <= L'Z') c += 32;
    return s;
}
inline std::wstring squeeze(std::wstring s) {
    std::wstring o;
    for (wchar_t c : s) if (c != L' ' && c != L'_' && c != L'-') o += c;
    return o;
}

struct ImportResult { int added = 0, duplicates = 0, unsupported = 0, missing = 0; };
inline std::wstring manualPathKey(const std::wstring& path) {
    std::error_code ec;
    auto absolute = fs::absolute(fs::path(path), ec);
    return toLowerCp((ec ? fs::path(path) : absolute).lexically_normal().wstring());
}
inline ImportResult addManualFiles(Config& config, const std::vector<std::wstring>& paths) {
    ImportResult result;
    std::set<std::wstring> existing;
    for (auto& item : config.manual) existing.insert(manualPathKey(item.target));
    std::vector<Item> added;
    for (auto& path : paths) {
        auto extension = toLowerCp(fs::path(path).extension().wstring());
        if (extension != L".exe" && extension != L".lnk" && extension != L".url") { ++result.unsupported; continue; }
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) { ++result.missing; continue; }
        if (!existing.insert(manualPathKey(path)).second) { ++result.duplicates; continue; }
        Item item; item.title = fs::path(path).stem().wstring();
        item.target = fs::absolute(fs::path(path)).lexically_normal().wstring();
        item.subtitle = item.target; item.kind = App;
        added.push_back(item); ++result.added;
    }
    config.manual.insert(config.manual.begin(), added.begin(), added.end());
    return result;
}

inline HRESULT chooseGameFiles(HWND owner, std::vector<std::wstring>& paths, bool themeFiles=false) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) return result;
    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC themeFilter[] = {{L"SideDeck 主题包", L"*.json"}};
    const COMDLG_FILTERSPEC filters[] = {{L"游戏与快捷方式", L"*.exe;*.lnk;*.url"}, {L"可执行文件", L"*.exe"}, {L"快捷方式", L"*.lnk;*.url"}};
    if (SUCCEEDED(result)) result = dialog->SetFileTypes(themeFiles ? 1 : 3, themeFiles ? themeFilter : filters);
    if (SUCCEEDED(result)) result = dialog->SetTitle(themeFiles ? L"导入主题 · 选择包内的 theme.json（可多选）" : L"批量添加游戏 · Ctrl / Shift 多选");
    if (SUCCEEDED(result)) result = dialog->Show(owner);
    IShellItemArray* selection = nullptr;
    if (SUCCEEDED(result)) result = dialog->GetResults(&selection);
    if (selection) {
        DWORD count = 0; result = selection->GetCount(&count);
        for (DWORD i = 0; SUCCEEDED(result) && i < count; ++i) {
            IShellItem* item = nullptr;
            result = selection->GetItemAt(i, &item);
            if (SUCCEEDED(result)) {
                PWSTR path = nullptr;
                result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(result) && path) paths.emplace_back(path);
                CoTaskMemFree(path); item->Release();
            }
        }
        selection->Release();
    }
    dialog->Release();
    if (FAILED(result)) paths.clear();
    return result;
}
inline std::wstring pickExe(const std::wstring& dir, const std::wstring& gameName, const std::wstring& installDir) {
    if (dir.empty()) return L"";
    static const wchar_t* BAD[] = {
        L"launch", L"install", L"redist", L"directx", L"dxsetup", L"setup", L"unins",
        L"crash", L"workshop", L"busybox", L"editor", L"eac", L"easyanticheat",
        L"battleye", L"vcredist", L"dxweb", L"oalinst", L"report", L"upload",
        L"steamerror", L"watchdog", L"prereq", L"bootstrap", L"updater", L"patcher"
    };
    std::wstring dirL = squeeze(toLowerCp(installDir));
    std::wstring nameL = squeeze(toLowerCp(gameName));
    std::wstring nameFirst;
    {
        std::wstring t = toLowerCp(gameName);
        size_t p0 = t.find_first_not_of(L" \t");
        if (p0 != std::wstring::npos) {
            size_t p1 = t.find(' ', p0);
            nameFirst = squeeze(t.substr(p0, p1 == std::wstring::npos ? std::wstring::npos : p1 - p0));
        }
    }

    struct Cand { std::wstring path, base; unsigned long long size; int depth; };
    std::vector<Cand> cands;
    std::error_code ec;
    auto scanDir = [&](const std::wstring& d, int depth) {
        for (auto& e : fs::directory_iterator(d, ec)) {
            if (!e.is_regular_file(ec)) continue;
            auto p = e.path();
            if (p.extension() != L".exe") continue;
            Cand c;
            c.path = p.wstring();
            c.base = toLowerCp(p.stem().wstring());
            c.size = fs::file_size(p, ec);
            c.depth = depth;
            bool bad = false;
            for (auto b : BAD) if (c.base.find(b) != std::wstring::npos) { bad = true; break; }
            if (!bad) cands.push_back(c);
        }
    };
    scanDir(dir, 0);
    if (cands.empty()) {
        int cnt = 0;
        for (auto& e : fs::directory_iterator(dir, ec)) {
            if (++cnt > 300) break;
            if (!e.is_directory(ec)) continue;
            std::wstring dn = toLowerCp(e.path().filename().wstring());
            if (dn.find(L"redist") != std::wstring::npos || dn.find(L"directx") != std::wstring::npos ||
                dn.find(L"support") != std::wstring::npos || (dn.find(L"bin") == std::wstring::npos && dn.find(L"game") == std::wstring::npos && dn.find(L"data") == std::wstring::npos))
                continue;
            scanDir(e.path().wstring(), 1);
            if (!cands.empty()) break;
        }
    }
    if (cands.empty()) return L"";

    Cand* best = &cands[0];
    long long bestScore = -1;
    for (auto& c : cands) {
        std::wstring b = squeeze(c.base);
        long long sc = 0;
        if (c.depth == 0) sc += 10;
        if (b == dirL) sc += 100;
        else if (!dirL.empty() && b.find(dirL) != std::wstring::npos) sc += 60;
        if (!nameFirst.empty() && b.find(nameFirst) != std::wstring::npos) sc += 40;
        if (!nameL.empty() && (b.find(nameL) != std::wstring::npos || nameL.find(b) != std::wstring::npos)) sc += 50;
        sc += (long long)std::min<unsigned long long>(c.size / (100 * 1000 * 1000ull), 15); // 大文件加分，封顶
        if (sc > bestScore) { bestScore = sc; best = &c; }
    }
    return best->path;
}

inline std::vector<Item> scanSteam() {
    std::vector<Item> out;
    std::wstring sp = regRead(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (sp.empty()) sp = regRead(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (sp.empty()) return out;
    std::replace(sp.begin(), sp.end(), '/', '\\');
    for (auto& libDir : steamLibraries(sp)) {
        std::wstring apps = libDir + L"\\steamapps";
        std::error_code ec;
        if (!fs::exists(apps, ec)) continue;
        for (auto& e : fs::directory_iterator(apps, ec)) {
            auto p = e.path();
            std::wstring fn = p.filename().wstring();
            if (fn.rfind(L"appmanifest_", 0) != 0) continue;
            // appid 用于封面图（appcache/librarycache/<appid>/header.jpg）
            std::wstring appid = fn.substr(12);
            if (appid.size() > 4 && appid.substr(appid.size() - 4) == L".acf")
                appid = appid.substr(0, appid.size() - 4);
            std::string txt = readTextFile(p.wstring());
            if (txt.empty()) continue;
            std::wstring name = vdfField(txt, "name");
            std::wstring dir = vdfField(txt, "installdir");
            if (name.empty() || dir.empty()) continue;
            std::wstring full = apps + L"\\common\\" + dir;
            if (!fs::exists(full, ec)) continue;
            std::wstring exe = pickExe(full, name, dir);
            if (exe.empty()) continue;
            Item it;
            it.title = name;
            it.subtitle = L"Steam";
            it.target = exe;
            it.kind = App;
            // Steam 官方封面，比 exe 图标好看
            if (!appid.empty()) {
                std::wstring coverImg = sp + L"\\appcache\\librarycache\\" + appid + L"\\header.jpg";
                std::error_code ec2;
                if (fs::exists(coverImg, ec2)) it.iconPath = coverImg;
            }
            out.push_back(it);
        }
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    out.erase(std::unique(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title == b.title; }), out.end());
    return out;
}

inline std::vector<Item> scanEpic() {
    std::vector<Item> out;
    wchar_t* pd = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd) != S_OK) return out;
    std::wstring mf = std::wstring(pd) + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    CoTaskMemFree(pd);
    std::error_code ec;
    if (!fs::exists(mf, ec)) return out;
    for (auto& e : fs::directory_iterator(mf, ec)) {
        if (e.path().extension() != L".item") continue;
        std::string txt = readTextFile(e.path().wstring());
        if (txt.empty()) continue;
        sj::Value v;
        try { v = sj::parse(txt); } catch (...) { continue; }
        std::wstring name = ui::utf8(v.getStr("DisplayName"));
        std::wstring loc = ui::utf8(v.getStr("InstallLocation"));
        std::wstring exe = ui::utf8(v.getStr("LaunchExecutable"));
        if (name.empty() || loc.empty() || exe.empty()) continue;
        std::wstring full = loc + L"\\" + exe;
        if (!fileExists(full)) continue;
        Item it; it.title = name; it.subtitle = L"Epic"; it.target = full; it.kind = App;
        out.push_back(it);
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    return out;
}

inline std::vector<Item> scanStartMenu() {
    std::vector<Item> out;
    wchar_t* ad = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &ad) != S_OK) return out;
    std::wstring base = std::wstring(ad) + L"\\Microsoft\\Windows\\Start Menu\\Programs";
    CoTaskMemFree(ad);
    std::error_code ec;
    std::vector<fs::path> lnks;
    for (fs::recursive_directory_iterator it(base, ec), end; it != end; ++it) {
        if (it->is_regular_file() && it->path().extension() == L".lnk") lnks.push_back(it->path());
        if (lnks.size() > 4000) break;
    }
    for (auto& lp : lnks) {
        IShellLinkW* sl = nullptr;
        if (CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl) != S_OK)
            continue;
        IPersistFile* pf = nullptr;
        if (sl->QueryInterface(IID_IPersistFile, (void**)&pf) != S_OK) { sl->Release(); continue; }
        if (pf->Load(lp.c_str(), STGM_READ) != S_OK) { pf->Release(); sl->Release(); continue; }
        wchar_t target[MAX_PATH]{};
        if (sl->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH) != S_OK) { pf->Release(); sl->Release(); continue; }
        std::wstring t(target);
        pf->Release(); sl->Release();
        if (t.empty() || t.rfind(L".exe") != t.size() - 4) continue;
        Item it;
        it.title = lp.stem().wstring();
        it.subtitle = L"开始菜单";
        it.target = lp.wstring(); // Retain the link's arguments and Start in.
        it.kind = App;
        out.push_back(it);
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    out.erase(std::unique(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.target == b.target; }), out.end());
    return out;
}

inline std::vector<Item> scanGuides(const std::wstring& root = appDir()) {
    std::vector<Item> out;
    std::error_code ec;
    std::wstring gd = root + L"\\guides";
    if (!fs::exists(gd, ec)) { fs::create_directories(gd, ec); return out; }
    std::vector<fs::path> guidePaths;
    for (auto& e : fs::directory_iterator(gd, ec)) if (e.is_regular_file(ec)) guidePaths.push_back(e.path());
    // 兼容项目根目录中的现有 HTML 图鉴，同时读取 guides 内的 HTML。
    for (auto& e : fs::directory_iterator(root, ec)) {
        auto ext = toLowerCp(e.path().extension().wstring());
        if (e.is_regular_file(ec) && (ext == L".html" || ext == L".htm")) guidePaths.push_back(e.path());
    }
    for (auto& p : guidePaths) {
        auto ext = p.extension().wstring();
        for (auto& c : ext) if (c >= 'A' && c <= 'Z') c += 32;
        bool html = ext == L".html" || ext == L".htm";
        if (ext != L".md" && ext != L".txt" && !html) continue;
        std::string txt = readTextFile(p.wstring());
        std::wstring title = p.stem().wstring();
        std::wstring sub;
        // 取首个 # 标题
        size_t i = txt.find('#');
        if (i != std::string::npos && (i == 0 || txt[i - 1] == '\n')) {
            size_t eol = txt.find('\n', i);
            std::string h = txt.substr(i + 1, eol == std::string::npos ? std::string::npos : eol - i - 1);
            while (!h.empty() && (h.front() == ' ' || h.front() == '#')) h.erase(h.begin());
            while (!h.empty() && (h.back() == '\r' || h.back() == ' ')) h.pop_back();
            if (!h.empty()) title = ui::utf8(h);
        }
        Item it; it.title = title; it.subtitle = L"攻略"; it.target = p.wstring(); it.kind = Guide;
        if (html) {
            auto doc = htmlguide::parse(txt);
            if (!doc.title.empty()) it.title = ui::utf8(doc.title);
            it.subtitle = L"HTML 图鉴 · " + std::to_wstring(doc.entries.size()) + L" 条记录";
        }
        out.push_back(it);
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.title < b.title; });
    return out;
}

// ---------------- 配置 ----------------
inline std::wstring configPath() { return appDir() + L"\\config.json"; }

inline void loadConfig(Config& c) {
    std::string txt = readTextFile(configPath());
    if (txt.empty()) return;
    sj::Value v;
    try { v = sj::parse(txt); } catch (...) { return; }
    c.hotkeyMask = v.getInt("hotkeyMask", c.hotkeyMask);
    c.longBack = v.getBool("longBack", c.longBack);
    c.longPressMs = v.getInt("longPressMs", c.longPressMs);
    c.width = v.getInt("width", c.width);
    c.edge = v.getInt("edge", c.edge);
    c.margin = v.getInt("margin", c.margin);
    c.scanSteam = v.getBool("scanSteam", c.scanSteam);
    c.scanEpic = v.getBool("scanEpic", c.scanEpic);
    c.scanStartMenu = v.getBool("scanStartMenu", c.scanStartMenu);
    c.hideOnFocusLost = v.getBool("hideOnFocusLost", c.hideOnFocusLost);
    c.themeFile = ui::utf8(v.getStr("themeFile"));
    c.theme = std::clamp(v.getInt("theme", 0), 0, ui::THEME_COUNT - 1);
    c.pauseKey = ui::utf8(v.getStr("pauseKey", ""));
    c.inputIsolation = v.getBool("inputIsolation", false);
    c.favorites.clear();
    if(v.has("favorites") && v["favorites"].isArr())for(auto& favorite:v["favorites"].arr)if(favorite.isStr())c.favorites.push_back(ui::utf8(favorite.str));
    if (v.has("scale")) c.scale = (float)v["scale"].numOr(1.35);
    if (!std::isfinite(c.scale)) c.scale = 1.35f;
    c.scale = std::clamp(c.scale, 0.75f, 2.5f);
    c.width = std::clamp(c.width, 320, 1200);
    c.margin = std::clamp(c.margin, 0, 200);
    c.longPressMs = std::clamp(c.longPressMs, 200, 5000);
    c.hotkeyMask &= 0xF7FF;
    c.edge = c.edge == 1 ? 1 : 0;
    c.manual.clear(); c.extraTabs.clear();

    if (v.has("manual") && v["manual"].isArr()) {
        for (auto& g : v["manual"].arr) {
            Item it;
            it.title = ui::utf8(g.getStr("title"));
            it.subtitle = ui::utf8(g.getStr("subtitle"));
            it.target = ui::utf8(g.getStr("target"));
            it.iconPath = ui::utf8(g.getStr("icon"));
            it.kind = g.getInt("kind", App);
            c.manual.push_back(it);
        }
    }
    if (v.has("tabs") && v["tabs"].isArr()) {
        for (auto& t : v["tabs"].arr) {
            Tab tb; tb.name = ui::utf8(t.getStr("name", "标签"));
            if (t.has("items")) {
                for (auto& g : t["items"].arr) {
                    Item it;
                    it.title = ui::utf8(g.getStr("title"));
                    it.subtitle = ui::utf8(g.getStr("subtitle"));
                    it.target = ui::utf8(g.getStr("target"));
                    it.iconPath = ui::utf8(g.getStr("icon"));
                    it.kind = g.getInt("kind", Command);
                    tb.items.push_back(it);
                }
            }
            c.extraTabs.push_back(tb);
        }
    }
}

inline void saveConfig(const Config& c) {
    sj::Value v(sj::Object{});
    v["hotkeyMask"] = sj::Value(c.hotkeyMask);
    v["longBack"] = sj::Value(c.longBack);
    v["longPressMs"] = sj::Value(c.longPressMs);
    v["width"] = sj::Value(c.width);
    v["edge"] = sj::Value(c.edge);
    v["margin"] = sj::Value(c.margin);
    v["scanSteam"] = sj::Value(c.scanSteam);
    v["scanEpic"] = sj::Value(c.scanEpic);
    v["scanStartMenu"] = sj::Value(c.scanStartMenu);
    v["hideOnFocusLost"] = sj::Value(c.hideOnFocusLost);
    v["theme"] = sj::Value(c.theme);
    v["themeFile"] = sj::Value(ui::narrow(c.themeFile));
    v["pauseKey"] = sj::Value(ui::narrow(c.pauseKey));
    v["inputIsolation"] = sj::Value(c.inputIsolation);
    sj::Array favorites;for(auto& favorite:c.favorites)favorites.push_back(sj::Value(ui::narrow(favorite)));
    v["favorites"]=sj::Value(favorites);
    v["scale"] = sj::Value((double)c.scale);
    sj::Array man;
    for (auto& it : c.manual) {
        sj::Object o;
        o["title"] = sj::Value(ui::narrow(it.title));
        o["subtitle"] = sj::Value(ui::narrow(it.subtitle));
        o["target"] = sj::Value(ui::narrow(it.target));
        o["icon"] = sj::Value(ui::narrow(it.iconPath));
        o["kind"] = sj::Value(it.kind);
        man.push_back(sj::Value(o));
    }
    v["manual"] = sj::Value(man);
    sj::Array tabs;
    for (auto& t : c.extraTabs) {
        sj::Object o;
        o["name"] = sj::Value(ui::narrow(t.name));
        sj::Array items;
        for (auto& it : t.items) {
            sj::Object io;
            io["title"] = sj::Value(ui::narrow(it.title));
            io["subtitle"] = sj::Value(ui::narrow(it.subtitle));
            io["target"] = sj::Value(ui::narrow(it.target));
            io["icon"] = sj::Value(ui::narrow(it.iconPath));
            io["kind"] = sj::Value(it.kind);
            items.push_back(sj::Value(io));
        }
        o["items"] = sj::Value(items);
        tabs.push_back(sj::Value(o));
    }
    v["tabs"] = sj::Value(tabs);
    // 使用宽字符路径，并先写临时文件，避免中文目录保存失败或半写入损坏原配置。
    std::wstring temp = configPath() + L".tmp";
    FILE* f = _wfopen(temp.c_str(), L"wb");
    if (!f) return;
    std::string data = sj::dump(v);
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    if (fclose(f) != 0) ok = false;
    if (ok) MoveFileExW(temp.c_str(), configPath().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    else DeleteFileW(temp.c_str());
}

// ---------------- 启动 ----------------
struct LaunchRequest {
    std::wstring file, parameters, directory, executable, arguments;
    int show=SW_SHOWNORMAL;
};
inline std::wstring expandEnvironment(const std::wstring& value) {
    if(value.empty())return {};
    DWORD size=ExpandEnvironmentStringsW(value.c_str(),nullptr,0);
    if(!size || size>32768)return value;
    std::vector<wchar_t> expanded(size);
    DWORD written=ExpandEnvironmentStringsW(value.c_str(),expanded.data(),size);
    return written && written<=size?std::wstring(expanded.data()):value;
}
inline LaunchRequest prepareLaunch(const Item& it) {
    LaunchRequest request;request.file=it.target;
    if(it.kind==Url || PathIsURLW(it.target.c_str()))return request;
    if(it.kind==Command && it.target.rfind(L"cmd:",0)==0) {
        request.file=L"cmd.exe";request.parameters=L"/c "+it.target.substr(4);request.show=SW_HIDE;return request;
    }
    auto extension=toLowerCp(fs::path(it.target).extension().wstring());
    if(extension==L".lnk") {
        // Execute the original link to preserve arguments, elevation and window
        // state. The shortcut folder is never the game's working directory.
        IShellLinkW* link=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))) {
            IPersistFile* persist=nullptr;
            if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist)))) {
                if(SUCCEEDED(persist->Load(it.target.c_str(),STGM_READ))) {
                    std::vector<wchar_t> buffer(32768);
                    if(SUCCEEDED(link->GetWorkingDirectory(buffer.data(),(int)buffer.size())))request.directory=expandEnvironment(buffer.data());
                    if(request.directory.empty() && SUCCEEDED(link->GetPath(buffer.data(),(int)buffer.size(),nullptr,SLGP_RAWPATH))) {
                        std::wstring target=expandEnvironment(buffer.data());
                        if(!target.empty() && !PathIsRelativeW(target.c_str()) && !PathIsURLW(target.c_str()))request.directory=fs::path(target).parent_path().wstring();
                    }
                    // Common executable links need no Shell extension at all.
                    DWORD flags=0;IShellLinkDataList* data=nullptr;
                    if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&data)))){data->GetFlags(&flags);data->Release();}
                    if(!(flags&(SLDF_RUNAS_USER|SLDF_HAS_DARWINID))) {
                        buffer[0]=0;
                        if(SUCCEEDED(link->GetPath(buffer.data(),(int)buffer.size(),nullptr,SLGP_RAWPATH))) {
                            auto target=expandEnvironment(buffer.data());
                            if(toLowerCp(fs::path(target).extension().wstring())==L".exe" && !PathIsRelativeW(target.c_str())) {
                                request.executable=target;buffer[0]=0;
                                if(SUCCEEDED(link->GetArguments(buffer.data(),(int)buffer.size())))request.arguments=buffer.data();
                                link->GetShowCmd(&request.show);
                            }
                        }
                    }
                }
                persist->Release();
            }
            link->Release();
        }
        return request;
    }
    if(extension!=L".url")request.directory=fs::path(it.target).parent_path().wstring();
    if(extension==L".exe")request.executable=it.target;
    return request;
}
// Returns the Windows error; optional process handle is owned by the caller.
inline DWORD launch(const Item& it,HWND owner=nullptr,HANDLE* process=nullptr) {
    if(process)*process=nullptr;
    if(it.target.empty())return ERROR_INVALID_PARAMETER;
    auto request=prepareLaunch(it);
    if(!request.executable.empty()) {
        std::wstring command=L"\""+request.executable+L"\"";
        if(!request.arguments.empty())command+=L" "+request.arguments;
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=(WORD)request.show;
        PROCESS_INFORMATION child{};
        if(CreateProcessW(request.executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,
            request.directory.empty()?nullptr:request.directory.c_str(),&startup,&child)) {
            CloseHandle(child.hThread);if(process)*process=child.hProcess;else CloseHandle(child.hProcess);return ERROR_SUCCESS;
        }
        DWORD error=GetLastError();
        if(error!=ERROR_ELEVATION_REQUIRED)return error; // Shell handles elevation only.
    }
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.hwnd=owner;
    info.fMask=SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI|(process?SEE_MASK_NOCLOSEPROCESS:0);
    info.lpVerb=L"open";info.lpFile=request.file.c_str();info.nShow=request.show;
    info.lpParameters=request.parameters.empty()?nullptr:request.parameters.c_str();
    info.lpDirectory=request.directory.empty()?nullptr:request.directory.c_str();
    if(!ShellExecuteExW(&info)){DWORD error=GetLastError();return error?error:ERROR_GEN_FAILURE;}
    if(process)*process=info.hProcess;
    return ERROR_SUCCESS;
}

} // namespace lib
