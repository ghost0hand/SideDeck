# SideDeck

为手机串流 PC 游戏设计的手柄侧边栏：呼出游戏库、查看本地攻略、切换玻璃主题。

## 使用

从 Releases 下载 **SideDeck-v版本-Setup.exe** 安装；新版本运行同一安装包即可覆盖升级。也可使用完整便携 ZIP，解压后运行 **SideDeck.exe**。程序名称固定，版本由 tag、程序标题下方、托盘提示和 Windows 文件属性中的版本号区分。

- 手柄默认 LB + RB 呼出；也支持长按 Back 和 Ctrl + 空格。
- 上下 / 左摇杆选择，A 打开，B 返回，LB / RB 切换标签。
- 游戏库：Y 收藏 / 取消收藏，按下左摇杆切换全部 / 仅收藏，LT / RT 翻整页。
- X 打开字母速跳：十字键 / 左摇杆选字母，A 跳转，B 关闭；也可直接点击字母。
- 中文游戏按拼音首字母分组，英文按 A–Z，数字与其他字符归入 #；多音字采用默认字音，生僻字用系统拼音排序推断。
- 键盘 F2 收藏、F3 字母面板、F4 仅收藏，A–Z 直接跳转，PageUp / PageDown 翻整页。
- 手机 Moonlight 使用直接触控模式：在列表或攻略正文中单指上下滑动；支持跟手滚动和松手惯性，点住内容可停下惯性。
- 轻点保持原有选择 / 打开 / 返回操作，滑动松手不会误打开条目；电脑也支持鼠标左键按住拖动及高精度滚轮。
- 触屏浏览后切回手柄，会从当前可见位置继续选择；无需退回列表开头。
- 阅读时用右摇杆滚动，X 回到顶部。
- 「工具 → 外观主题」提供冰川玻璃、暮紫玻璃、烟灰玻璃、浅色磨砂。
- 支持 Steam / Epic 扫描；「工具 → 批量添加游戏」或托盘同名菜单支持 Ctrl / Shift 多选 exe、lnk、url，也可一次拖入多个文件，重复路径会自动跳过。
- 支持本地 Markdown、TXT 和 HTML 图鉴；将攻略放在程序旁的 guides 目录，也可将 HTML 放在程序目录。

首次运行会生成本机 config.json 和使用说明。升级时退出托盘程序，再用新版本 SideDeck.exe 替换旧文件；保留现有 config.json、guides 和图片目录。

默认模式下，呼出时 Deck 会接管前台焦点，游戏继续运行；关闭后等按键、扳机和摇杆松开，再把焦点还给原窗口。接管失败会取消呼出并提示；主动切到其他窗口时会收起 Deck，不抢回焦点。默认模式依赖游戏遵守前台输入规则，持续读取后台手柄的游戏仍可能响应；可选驱动隔离见下文。

## 手柄隔离（实验功能）

「工具 → 手柄输入隔离」或托盘同名入口可设置可选的驱动隔离。这一路径使用 HidHide 隐藏输入手柄，由 SideDeck 读取并转发到 ViGEmBus 虚拟 Xbox 360 手柄；不修改或暂停游戏进程。普通前台模式仍可在不安装驱动时使用。

1. 首次设置前退出游戏，只连接一只需要使用的 XInput 手柄。
2. 打开隔离设置，按提示安装缺少的官方签名驱动。安装程序需要管理员确认；如提示重启，请手动重启电脑后重新打开 SideDeck。已有 ViGEmBus 不会被强制重装。
3. 重新进入隔离设置，确认手柄后重新插拔接收器或重连手柄，再点确定。
4. 独立验证程序确认游戏侧只能看到虚拟输出，且 SideDeck 能读取输入后，才启用隔离；然后再启动游戏。

面板打开时，虚拟手柄保持连接但输出归零；关闭后等待手柄归位再恢复转发。输入在线程内按 2 毫秒休眠间隔轮询，独立于界面，不代表实测端到端延迟保证。支持基础按键、扳机、摇杆与标准双马达震动。当前一次接管一只手柄；出现多只输入或断线时保持归零。

再次点击隔离入口可以停止并恢复真实手柄；开始菜单也提供 Recover controller access 快捷方式。正常退出和独立恢复进程会撤销本次增加的隐藏/放行条目，保留其他软件原有配置；如恢复失败，会保留程序目录中的 input-recovery.json 供下次恢复，勿随意删除。关机或断电后的残留记录会在下一次启用前恢复。恢复后可能需要重连手柄或重启游戏。

串流客户端的手柄由 Sunshine 等软件生成，必须作为输入端被隔离，不能让它与 SideDeck 的输出一起被游戏读取。此路径需要单独验证，不保证所有串流软件、非 XInput 手柄、Steam Input 映射或游戏兼容。当前也不转发陀螺仪、触摸板或自适应扳机。

安装器只更新程序和依赖，不打包或删除个人攻略、配置。卸载 SideDeck 会保留个人数据及共享驱动；驱动如需移除，请通过 Windows 已安装的应用单独卸载。

**仓库和 Release 仅包含程序及通用文档，不包含个人配置、游戏路径、私人攻略、服装图片或地图。**

## 构建与测试

需要 Windows、PowerShell 7 和 MinGW-w64（支持 C++17）。本机已有的 toolchain 目录可继续使用，但不提交到 Git。

```powershell
./build.ps1                         # build/release/SideDeck.exe
./build.ps1 -Configuration Debug   # build/debug/SideDeck.exe
./test.ps1                         # 自包含的回归检查，不需要私人攻略
./build.ps1 -Configuration FocusTest
./build/tests/focus_integration.exe # 可选：用临时测试窗口验证跨进程焦点交接
./tools/package.ps1                # 校验/准备依赖，构建带驱动安装文件的安装包
./build.ps1 -Configuration InputTest
./build.ps1 -Configuration PerfTest
./build/tests/render_benchmark.exe # 合成离屏滚动，无缓存 / 缓存两种路径对比
# 手动将 build/deps/ViGEmClient.dll 放入 build/tests 后运行 input_driver_check.exe
# 此检查只创建归零的虚拟手柄，不模拟操作；需要本机已有 ViGEmBus。
```

若没有本地工具链，将 g++.exe 加入 PATH，或设置环境变量 SIDEDECK_CXX 指向完整路径。版本从 VERSION 读取，自动写入程序与 Windows 版本资源。

安装包还需要 Inno Setup（本次本地使用 7.1.0）。工具许可按其发行条款适用。依赖下载固定版本和 SHA-256，两个原始驱动安装器还校验 Authenticode 发布者；第三方许可见 THIRD_PARTY_NOTICES.md。驱动不会在构建或安装 SideDeck 本体时自动安装。

0.3.1 的性能改进保留现有透明窗口：后台加载图标，缓存字体、背景及卡片。仍由 GDI+ 绘制，未切换 GPU 渲染后端。性能脚本只测合成内容的离屏绘制，不包含窗口提交、手柄硬件、游戏帧率或网络延迟。

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

## 可导入主题（0.3.3）

工具 → 外观主题可选择原有四套主题，以及新增的星雨霓蓝、樱月琉璃、森萤翡翠、落日琥珀、黑金曜石。前三套附带二次元插画背景。

工具 → 导入主题包，选择解压后的 `theme.json`，会同时导入同目录背景图；也支持多选和拖入。之后增加主题无需改代码或升级版本。格式和说明见 [THEMES.md](THEMES.md)。
