# 跨平台应用图标

## 为什么此前在 Dock 中偏大

原 SVG 的底板为 112 / 128，即画布宽度的 87.5%。同时，运行时
`QApplication::setWindowIcon` 使用该 SVG，会覆盖平台打包图标。
因此只缩小 ICNS 文件而不修改运行时资源，仍可能出现 Dock 图标偏大。

本次保留原有蓝紫渐变和三层空间数据标识，统一**可见图形的视觉尺寸**，
同时按平台提供透明边距、圆角和不同像素尺寸。以下比例是 GeoReader 的设计选择，
不表示操作系统规定所有应用都必须使用同一个比例。

| 平台 | 1024 画布内可见底板 | 处理方式 |
|---|---:|---|
| macOS | 824×824，80.47% | 居中透明边距，较饱满圆角；ICNS 包含普通与 Retina 表示 |
| Windows | 832×832，81.25% | 较小圆角；ICO 包含 15 个尺寸，覆盖任务栏和常用高 DPI 比例 |
| Linux | 832×832，81.25% | 保留原圆角风格；hicolor 安装 SVG 和 17 档 PNG，兼容不同桌面图标主题 |

运行时图标使用对应平台的 17 档 PNG，与安装包资源同源，避免 SVG 插件缺失、
运行时覆盖或系统缩放导致尺寸不一致。16、20、24、30、32、36、40、48、60、64、72、80、96、128、256、512、1024
像素均由矢量母版独立渲染，不放大小图。透明区不会被裁剪后重新铺满画布。

## 维护和验证

- 母版：`resources/icons/georeader-artwork.svg`。
- 生成器：`scripts/generate_icons.cpp`。只在维护图标时需要 Qt SVG；APP 正常编译不增加这一依赖。
- 平台资源：`resources/icons/{macos,windows,linux}/`、`georeader.icns`、`georeader.ico`。
- 视觉对比：`tests/output/icon-comparison.png`，包含亮/暗背景及 16/24/32 像素实尺寸。

```bash
cmake -S . -B build-scientific -DGEOREADER_BUILD_ICON_TOOLS=ON
cmake --build build-scientific --target GeoReaderIconTool
QT_QPA_PLATFORM=offscreen build-scientific/GeoReaderIconTool "$PWD"
```

生成过程校验各尺寸的透明背景、可见边界、居中和目标占比。
macOS 另外通过系统 `iconutil` 解码 ICNS 验证。
Windows/Linux 完成资源和打包路径检查，尚未宣称已经在其任务栏或桌面环境真机验收。
图标缓存可能使已经运行的旧 APP 继续显示旧图；退出旧版并启动新的 APP 后查看。

## 平台依据

- [Apple 应用图标说明](https://developer.apple.com/design/human-interface-guidelines/app-icons)：平台外形与高分辨率图标；本项目继续使用 Qt 兼容的 ICNS。
- [Microsoft Win32 图标尺寸与 DPI 选择](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-construction)：提供多尺寸、优先匹配准确尺寸。GeoReader 是桌面 Win32/Qt 应用，未套用 MSIX 磁贴的额外边距。
- [Microsoft 图标设计](https://learn.microsoft.com/en-us/windows/apps/design/iconography/app-icon-design)：轮廓、圆角、小尺寸识别性。
- [freedesktop 图标主题规范](https://specifications.freedesktop.org/icon-theme/latest/)：hicolor 回退主题、固定尺寸和 scalable 目录。
