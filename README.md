# SideDeck

轻量的 Windows 游戏侧边栏，让手柄也能方便地启动游戏、切换主题和浏览游戏库。

适合本地游玩，也适合通过 Moonlight 等客户端串流到手机或掌机。采用 C++、Win32 和 GDI+ 构建，无需浏览器运行时。

[下载最新版](https://github.com/ghost0hand/SideDeck/releases/latest) · [更新日志](CHANGELOG.md) · [主题格式](THEMES.md) · [反馈问题](https://github.com/ghost0hand/SideDeck/issues)

## 界面预览

| 游戏库 | 字母速跳 | 玻璃主题 |
| :---: | :---: | :---: |
| <img src="docs/screenshots/library.png" width="260" alt="SideDeck 游戏库：透明侧边栏、游戏列表与收藏"> | <img src="docs/screenshots/quick-jump.png" width="260" alt="SideDeck 字母速跳：按拼音或英文字母定位游戏"> | <img src="docs/screenshots/themes.png" width="260" alt="SideDeck 主题选择：插画背景与玻璃配色"> |

以上为程序原生界面渲染预览，使用示例游戏库；图标为示例占位图。

## 功能

- **随时呼出**：手柄组合键、长按 Back 或键盘快捷键打开侧边栏，平时收起到系统托盘。
- **集中启动游戏**：扫描 Steam / Epic 游戏，支持批量添加 EXE、快捷方式和 URL 文件，也可以直接拖入。
- **收藏与快速定位**：收藏常玩的游戏，按中文拼音或英文字母跳转，支持整页翻动。
- **多种输入方式**：手柄、键鼠和触屏均可操作；支持拖动、惯性滚动与高精度滚轮。
- **玻璃拟态主题**：内置九套主题，包括纯色玻璃与二次元插画背景，支持导入自己的主题包与 MP4 动态壁纸。
- **原生轻量界面**：后台加载图标，缓存字体、背景和卡片绘制，减少滚动时的重复工作。

## 安装与开始使用

支持 **Windows 10 / 11 x64**。手柄操作需要兼容 XInput 的设备。

1. 在 [Releases](https://github.com/ghost0hand/SideDeck/releases/latest) 下载 `SideDeck-v版本-Setup.exe` 并安装。
2. 启动 SideDeck，按 **LB + RB** 或 **Ctrl + Space** 呼出面板。
3. 自动扫描游戏，或在「工具 → 批量添加游戏」中选择文件；支持 Ctrl / Shift 多选。
4. 在「工具 → 外观主题」中选择喜欢的配色和背景。

也可下载便携 ZIP，**完整解压后**运行 `SideDeck.exe`。升级时运行新版安装包，已有设置、游戏库、收藏和自定义主题会保留。

## 操作

| 功能 | 手柄 | 键盘 |
| --- | --- | --- |
| 呼出 / 收起面板 | LB + RB；或长按 Back | Ctrl + Space |
| 上下选择 | 十字键 ↑↓ / 左摇杆 | ↑↓ |
| 切换标签 | LB / RB | ← / → |
| 打开选中项 | A | Enter |
| 返回 | B | Backspace |
| 收起面板 | Start | Esc |
| 收藏 / 取消收藏 | Y | F2 |
| 移除条目（不删游戏文件） | 长按 RS | 长按 Delete |
| 字母速跳 | X | F3；也可直接按 A–Z |
| 全部 / 仅收藏 | 按下左摇杆 | F4 |
| 整页翻动 | LT / RT | PageUp / PageDown |

中文游戏按拼音首字母定位，英文按 A–Z，数字及其他字符归入 `#`。呼出组合键可在工具中修改。

长按 RS（右摇杆按下）或 Delete 约 0.8 秒可移除游戏库里选中的条目（例如已经失效的快捷方式），确认后只从侧边栏移除，不会删除或移动游戏文件。RS 短按原本没有功能，长按不会影响切换标签和收藏。移除扫描来源的游戏会记入配置，重新扫描也不会再出现；`config.json` 的 `holdRemoveMs` 可以调整长按时长。

手机使用 Moonlight 的**直接触控**模式时，可单指上下滑动列表，轻点选择或打开条目。电脑端也支持鼠标按住拖动和滚轮滚动。

## 自定义主题

主题包由 `theme.json` 和可选的同目录背景图组成。

在「工具 → 导入主题包」中选择 JSON 文件，或直接将它拖入面板，程序会一起复制背景图。也可以把整个主题文件夹放进主题目录，再打开主题选择器刷新。

新增主题无需修改代码或重新编译。主题字段和示例见 [THEMES.md](THEMES.md)。

## 手柄输入隔离

默认模式无需额外驱动，打开面板时接管前台焦点。部分游戏持续读取后台手柄，此时游戏仍可能响应按键。

「工具 → 手柄输入隔离」提供基于 HidHide 和 ViGEmBus 的**实验性隔离功能**，需要单独安装驱动并完成设备验证。当前一次接管一只 XInput 手柄；串流输入和不同映射工具的兼容性需要分别确认。安装 SideDeck 本身不会自动安装驱动或重启电脑。

## 从源码构建

需要 Windows、PowerShell 7 和支持 C++17 的 MinGW-w64。将 `g++.exe` 加入 PATH，或通过 `SIDEDECK_CXX` 指定编译器路径。

```powershell
./build.ps1                         # 构建发布版，输出到 build/release/
./build.ps1 -Configuration Debug   # 构建调试版
./test.ps1                         # 运行回归检查
./tools/package.ps1                # 构建安装包，另需 Inno Setup
```

当前版本见 [VERSION](VERSION)，第三方组件及许可见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
