# SideDeck

为手机串流 PC 游戏设计的手柄侧边栏：呼出游戏库、查看本地攻略、切换玻璃主题。

## 使用

从 Releases 下载发布包，解压后运行 **SideDeck.exe**。程序名称固定，版本由 tag、程序标题下方、托盘提示和 Windows 文件属性中的版本号区分。

- 手柄默认 LB + RB 呼出；也支持长按 Back 和 Ctrl + 空格。
- 上下 / 左摇杆选择，A 打开，B 返回，LB / RB 切换标签。
- 阅读时用右摇杆滚动，X 回到顶部。
- 「工具 → 外观主题」提供冰川玻璃、暮紫玻璃、烟灰玻璃、浅色磨砂。
- 支持 Steam / Epic 扫描；「工具 → 批量添加游戏」或托盘同名菜单支持 Ctrl / Shift 多选 exe、lnk、url，也可一次拖入多个文件，重复路径会自动跳过。
- 支持本地 Markdown、TXT 和 HTML 图鉴；将攻略放在程序旁的 guides 目录，也可将 HTML 放在程序目录。

首次运行会生成本机 config.json 和使用说明。升级时退出托盘程序，再用新版本 SideDeck.exe 替换旧文件；保留现有 config.json、guides 和图片目录。

呼出时 Deck 会接管前台焦点，游戏继续运行；关闭后等按键、扳机和摇杆松开，再把焦点还给原窗口。接管失败会取消呼出并提示；主动切到其他窗口时会收起 Deck，不抢回焦点。此方案依赖游戏遵守前台输入规则，持续读取后台手柄的游戏仍可能响应，未使用暂停进程、驱动或游戏注入。

**仓库和 Release 仅包含程序及通用文档，不包含个人配置、游戏路径、私人攻略、服装图片或地图。**

## 构建与测试

需要 Windows、PowerShell 7 和 MinGW-w64（支持 C++17）。本机已有的 toolchain 目录可继续使用，但不提交到 Git。

```powershell
./build.ps1                         # build/release/SideDeck.exe
./build.ps1 -Configuration Debug   # build/debug/SideDeck.exe
./test.ps1                         # 自包含的回归检查，不需要私人攻略
./build.ps1 -Configuration FocusTest
./build/tests/focus_integration.exe # 可选：用临时测试窗口验证跨进程焦点交接
```

若没有本地工具链，将 g++.exe 加入 PATH，或设置环境变量 SIDEDECK_CXX 指向完整路径。版本从 VERSION 读取，自动写入程序与 Windows 版本资源。

现有 Git Bash 入口 build.sh、build_debug.sh、test.sh 仍可使用，内部调用同一套 PowerShell 构建。

## 版本与发布

当前版本见 VERSION，变更见 CHANGELOG.md。首次纳入版本管理的基线是 v0.1.0，不为以前临时命名的 exe 伪造源码历史。

每次发布：

1. 修改代码和 VERSION，在 CHANGELOG.md 增加对应版本说明。
2. 运行 ./test.ps1，提交代码。
3. 创建注释 tag 并推送：

```powershell
git tag -a v0.1.1 -m 'SideDeck v0.1.1'
git push origin main
git push origin v0.1.1
```

推送 v* tag 后，GitHub Actions 校验 tag 与 VERSION、测试、构建，再创建同名 Release。不要移动已发布的 tag，修复后发布下一个版本。

本地也可运行：

```powershell
./tools/release.ps1 -VerifyTag
```

发布包位于 releases/v<版本>/，ZIP 内始终是 SideDeck.exe；附带发布说明、源码 commit 信息和 SHA-256 校验文件。打包使用固定文件清单，不会把整个工作目录或私人资料打进去。

## 目录

- src/：Win32、GDI+、XInput、配置与攻略解析源码。
- tests/：使用合成样例的自包含回归检查。
- tools/：发布与本地更新工具。
- .github/workflows/：tag 触发的发布流程。
- build/、releases/、backups/：本机构建、发布包和旧文件备份，均不提交。
- config.json、guides/、map_imgs/、suit_imgs/、根目录 HTML：本地个人数据，均不提交。
