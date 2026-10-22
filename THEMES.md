# 制作主题

新建一个文件夹，放入 UTF-8 编码的 `theme.json` 和可选的背景图片：

```text
my-theme/
  theme.json
  background.png
```

## JSON 示例

```json
{
  "format": "sidedeck-theme",
  "version": 1,
  "name": "我的玻璃主题",
  "description": "冷蓝夜景",
  "base": 0,
  "background": "background.png",
  "imageOpacity": 0.88,
  "colors": {
    "glassTop": "#0B193846",
    "glassBottom": "#0D172B9E",
    "accentA": "#8DDFFF",
    "accentB": "#C5C6FF"
  }
}
```

## 修改配色与背景

- `format` 和 `version` 保持示例中的值；`name` 是主题名称，`description` 是简短说明。
- `base` 选择基础配色：`0` 冰川、`1` 暮紫、`2` 烟灰、`3` 浅色磨砂。未填写的颜色沿用基础配色。
- `glassTop` / `glassBottom` 是玻璃上下渐变色；`accentA` / `accentB` 是强调色。更多字段可参考[随附主题](theme-packs/)。
- 颜色使用 `#RRGGBB` 或 `#RRGGBBAA`，最后两位表示透明度：`00` 全透明，`FF` 不透明。
- `background` 填写同目录图片的文件名，`imageOpacity` 为图片不透明度，范围 `0–1`。不需要背景时，删除这两项即可。

背景支持 PNG、JPG、JPEG，最大 16 MB，每边不超过 4096 像素；建议使用竖图，显示时会按比例居中填满。

## 导入

在「工具 → 导入主题包」选择 `theme.json`，或将它拖入面板，背景图会一起复制。随后在「外观主题」中选择即可。
