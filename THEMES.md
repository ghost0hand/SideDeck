# SideDeck 主题包

0.3.3 起可直接导入主题，无需改代码或发布新版本。

在「工具 → 导入主题包」选择 `theme.json`，支持多选，也可把 JSON 文件拖入面板。程序会把 JSON 和它引用的同目录图片一起复制到自己的 `themes` 目录，并切换到最后导入的主题。下载的压缩包请先解压，保留目录结构。重复导入创建独立副本，不覆盖已有主题。

也可以通过「工具 → 打开主题目录」，将整个主题文件夹放入，再打开「外观主题」刷新。每页显示四个主题，手柄上下选择或滚轮会自动翻页，手机可点底部的上页、下页。A 应用、B 返回。

主题结构：

```text
my-theme/
  theme.json
  background.png
```

```json
{
  "format": "sidedeck-theme",
  "version": 1,
  "name": "我的玻璃主题",
  "description": "冷蓝夜景",
  "base": 0,
  "background": "background.png",
  "imageOpacity": 0.88,
  "backdropOpacity": 0.25,
  "colors": {
    "glassTop": "#0B193846",
    "glassBottom": "#0D172B9E",
    "accentA": "#8DDFFF",
    "accentB": "#C5C6FF",
    "panelTop": "#152541E8"
  }
}
```

`base` 取 0–3，分别继承冰川、暮紫、烟灰、浅色磨砂。未填写的颜色保留继承值。颜色格式为 `#RRGGBB` 或 `#RRGGBBAA`（最后两位为透明度）。可覆盖字段：`panel`、`panelTop`、`text`、`textDim`、`textFaint`、`accentA`、`accentB`、`selBg`、`sep`、`chip`、`shadow`、`glassTop`、`glassBottom`、`rim`、`reader`。

图片可省略。支持同目录 PNG/JPG/JPEG，最大 16 MB、每边不超过 4096 像素；建议竖版，画面会按比例居中填满。两项 opacity 取 0–1；玻璃颜色本身的透明度也会影响背景可见程度。正文和卡片层保持额外遮罩以提高可读性。JSON 最大 32 KB；最多读取 128 个主题包。

升级和卸载保留主题文件。自己修改过的随附主题也不会被安装包覆盖。选择按相对文件路径保存；移动或删除当前主题时回退到其基础主题，恢复原路径后可再次选择。

## 随附主题与插画

三张原始插画通过内置 imagegen 工具生成，保存在 `theme-packs/01-star-rain/background.png`、`02-sakura-moon/background.png`、`03-forest-firefly/background.png`。它们是插画背景，不是完整 UI 截图。

生成提示词的共同要求：原创二次元成人角色、1024×1536 竖构图、人物位于右上方、左侧及中部为柔和暗色低细节区域，适合覆盖玻璃面板与文字；无界面、无文字、无标志或水印。

- 星雨霓蓝：银发女性、深蓝未来风外套、雨夜城市、青蓝与长春花紫、窗面倒影与星光。
- 樱月琉璃：深梅紫长发女性、象牙与淡紫衣裙、蓝调时刻的樱花庭院、月光与水面倒影、灰粉与薰衣草紫。
- 森萤翡翠：浅薄荷发精灵女性、深青旅行披风、暮色森林湖畔、萤火与青绿水面、翡翠与克制的金色。

另附「落日琥珀」「黑金曜石」两套无插画玻璃配色，原有四套主题继续保留。
