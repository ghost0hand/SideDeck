# 制作主题

新建一个文件夹，放入 UTF-8 编码的 `theme.json`，以及要使用的图片或 MP4：

```text
my-theme/
  theme.json
  background.png
  wallpaper.mp4
```

## JSON 示例（动态壁纸 + 图片回退）

```json
{
  "format": "sidedeck-theme",
  "version": 1,
  "name": "我的玻璃主题",
  "description": "冷蓝夜景",
  "base": 0,
  "background": "background.png",
  "video": "wallpaper.mp4",
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
- `background` / `video` 填写同目录文件名，不使用的字段直接删除。只用图片时删除 `video`；只用视频时删除 `background`。
- `imageOpacity` 控制图片或视频的不透明度，范围 `0–1`；玻璃颜色的透明度也会影响背景的可见程度。

图片支持 PNG、JPG、JPEG，最大 16 MB，每边不超过 4096 像素。图片和视频都按比例居中填满，超出部分裁切，不会拉伸；建议提前准备好竖版构图。

## 导入

在「工具 → 导入主题包」选择 `theme.json`，或将它拖入面板，引用的图片和视频会一起复制。随后在「外观主题」中选择即可，无需重新编译。分享主题时，将 JSON 和素材一起打包，接收方先解压再导入。

## MP4 动态壁纸

最简单的方式是在「导入主题包」中直接选择 MP4，或将 MP4 拖入面板，程序会自动创建动态主题。

自定义配色时使用上面的 JSON 示例即可：

- 视频文件使用小写 `.mp4` 扩展名，最大 256 MB，推荐 H.264 编码；能否解码取决于 Windows 媒体组件。
- 视频静音循环播放，优先于 `background`；图片可作为加载期间或播放失败时的回退。
- 收起面板后暂停，两秒后释放解码器；再次加载时从头播放。
- 建议压缩过大的原片，减少解码开销；不必为侧边栏准备超高分辨率视频。
