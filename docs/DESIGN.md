# GeoReader 设计与工程说明

当前版本：3.1.0。基于主分支的 Qt Widgets + Qlementine 原生界面，合入科学数据查看器和统一图标；不再使用 Qt Quick/QML。

## 多维数据扩展

- 原生变量选择窗口先确认变量、空间轴和固定维度，再加入图层；取消不会留下图层。
- `ScientificData` 负责 GDAL 多维数组、内存 VRT 切片、物理量转换、抽样统计、时序和空间剖面。
- `ScientificPanel` 是可停靠面板，提供时间/深度切片、播放、色标、像元曲线、邻域值、直方图、属性与导出；无需 Qt Charts。
- 图层使用稳定 ID；后台请求带版本号，切换或删除图层后丢弃过期结果。GDAL/HDF5 调用及句柄释放共享递归锁。
- `georeader-md:` URI 保存文件、变量路径、轴、索引和显示设置；源文件只读。GDAL 经典子数据集仍有后备路径。
- 规则地理网格进入地图；无地理定位的数组使用像素坐标。非规则网格不伪装为经纬度。
- 细节与限制见 [科学数据](SCIENTIFIC_DATA.md)、[测试记录](SCIENTIFIC_TEST_REPORT.md)、[图标规范](APP_ICONS.md)。

## 已实现

- 图层管理面板底部提供可折叠的“底图”图层组，OpenStreetMap、Esri 世界
  影像和 OpenTopoMap 地形图三项互斥选择，默认启用 OpenStreetMap；各服务
  使用独立缓存键、最大缩放级别和动态署名
- 参考 QGIS 的互斥地图工具：默认平移、滚轮缩放、矩形框选缩放、
  矢量要素识别和栅格像元识别
- 矩形框选缩放，按 `Esc` 可取消框选
- 使用系统原生文件对话框打开：
  - Shapefile (`.shp`)
  - GeoJSON (`.geojson`, `.json`)
  - GeoPackage (`.gpkg`，按子图层加入)
  - GeoTIFF (`.tif`, `.tiff`)
  - 自动记住最近一次成功打开文件的目录；目录失效时回退到系统文稿目录
- 图层管理：
  - 以数据集为父节点、源图层为子节点；数据集与展开的子图层共享浅色
    容器，每个子图层保留独立卡片，当前图层以左侧竖线高亮
  - 多图层数据可折叠/展开，并可整组显隐和移除
  - 通过拖动手柄在数据集内调整图层顺序；列表顶部图层在地图中位于最上方
  - 图层右键菜单提供“缩放至图层”“打开属性表”和“元信息”；属性表与
    元信息使用居中、非模态悬浮窗口
  - 显示/隐藏、移除、不透明度
  - 按需查看矢量或栅格元信息，包括驱动、坐标参考系、投影定义、
    数据范围、字段/几何信息以及栅格尺寸、数据类型、像素大小、块大小
    和 NoData
  - 矢量线色、填充色和线宽
  - 单波段数据直接显示 Viridis、Plasma、Inferno、Magma、Cividis、
    Turbo、Terrain 和 Gray 色带，每种色带均支持正向和反向
  - 多波段数据直接显示 R/G/B 三行波段下拉选择
  - 最小值–最大值、2%–98% 累计裁剪、均值 ±2σ 和直方图均衡化
    四种拉伸方式
  - 色带、波段、拉伸范围和 NoData 修改后即时生效，无额外应用按钮
  - 每个波段的显示最小值/最大值可调整，NoData（含 `nan`）默认透明
- 栅格图层先读取元数据并立即加入地图，近似统计在后台更新，避免打开文件
  时阻塞 UI
- 栅格只读取当前地图视窗，并根据屏幕像素大小自动使用 GeoTIFF 内部或
  外部 overview（金字塔）；同一视窗的波段浮点数据使用 256 MiB LRU 缓存
- 色带、色带方向、拉伸范围和透明度只对缓存数据做内存着色，不重新扫描
  整幅栅格
- 打开栅格值面板后，随鼠标移动实时查询所有可见栅格的像元值
- 左下角经纬度使用 `E/W` 与 `N/S` 半球方位标记
- 打开矢量属性面板后自动选择首个可见矢量图层，单击地图即可查询；
  线、点、面要素命中后均会高亮并显示属性
- 矢量图层可打开居中、非模态且可拖动的属性表；按列头进行数值/文本
  升降序排列，并可选择字段后按属性值进行不区分大小写的包含查询；窗口
  主体可向左右或下方拖出主界面并自动裁剪，同时始终保留可拖回的标题栏区域
- 简体中文与 English 即时切换
- 字体、字号、语言、工具栏透明度（默认 0.85）、Qlementine 主题和
  快捷键持久化
- 工具栏采用 Heroicons 24 px outline 图标；APP Icon 使用蓝紫渐变的
  小圆角三图层设计，并提供 macOS `.icns`、Windows `.ico` 和 Linux SVG
- macOS Intel 与 Apple Silicon 安装包最低支持 macOS Monterey 12
- macOS、Windows 与 Linux 统一使用 Qlementine `v1.4.2` 原生
  `QStyle`，支持跟随系统、浅色和深色主题即时切换
- Linux 原生支持 Wayland，并保留 X11/XWayland 回退；Wayland 会话中默认
  按 `wayland;xcb` 顺序选择 Qt QPA 后端，用户设置的 `QT_QPA_PLATFORM`
  始终具有更高优先级

> 主界面完全由 Qt Widgets 构建；Qlementine 主题、字体、图层样式和波段
> 修改均即时生效，无需重启应用。

## 版权与使用

本 APP 由西北大学谭振宇团队开发。用户可以免费分发和使用；商业使用
必须获得作者授权。项目采用
[PolyForm Noncommercial License 1.0.0](LICENSE)，允许非商业用途下使用、
修改和分发；商业用途需要另行取得许可。相同声明也可在 GeoReader 的
“设置”面板中查看。

## 设计架构

GeoReader 将原生桌面界面、状态、地图交互和数据渲染分开，避免界面层
直接持有 GDAL/OGR 或 Mapnik 对象：

```text
MainWindow / QMainWindow + Qlementine
  ├─ 左侧磨砂 QToolBar 与地图工具
  ├─ 图层/栅格值/矢量属性/设置圆角浮动面板
  ├─ 非模态元信息窗口与属性表
  └─ 双语文本、快捷键、浅色/深色主题与样式输入
            │
            ▼
AppController
  ├─ 系统文件对话框、设置持久化、语言切换
  ├─ GDAL/OGR 数据集识别、范围与坐标系转换
  ├─ 后台栅格统计
  └─ 栅格像元与矢量要素查询
            │
            ├──────────────► LayerModel
            │                 └─ QAbstractListModel + LayerSnapshot
            ├──────────────► AttributeTableModel
            │                 └─ OGR 字段读取、列排序与属性筛选
            ▼
MapCanvas / QWidget
  ├─ Web Mercator 视口、平移/缩放/框选/识别工具
  ├─ OSM / Esri World Imagery / OpenTopoMap XYZ 瓦片与磁盘缓存
  ├─ RasterRenderer：GDAL 视窗读取、金字塔与内存着色
  ├─ Mapnik：矢量样式与透明背景离屏渲染
  └─ QPainter：底图、栅格、矢量和选中高亮合成
```

### 图层状态

`LayerModel` 是 UI 与渲染器之间唯一的图层状态源。模型仍以图层快照驱动
渲染，但通过稳定的 `datasetId` 将同一次打开的数据集及其全部子图层组合为
一个管理单元；数据集标题栏统一控制可见性和整体移除，子图层只负责样式、
顺序、元信息与属性表。每个 `LayerSnapshot` 包含数据集标识、数据路径、
图层类型、坐标系、可见性、不透明度、
矢量符号、RGB/单波段配置、色带方向、波段范围和 NoData。后台任务只接收
快照副本和值类型视口，不访问界面对象，也不会持有会随 UI 变化的
`QModelIndex`。同一数据集内的图层仍可拖动排序，Canvas 的合成顺序同步更新。

在线底图作为固定的“底图”图层组显示在数据集列表下方，可折叠但不可移除，
使面板从上到下与地图从上层到下层的图层栈顺序保持一致。
组内通过单选项保证同一时刻只显示一种底图，初始状态选择 OpenStreetMap；
切换后立即刷新瓦片、缓存命名空间和地图署名。


设置使用 `QSettings` 持久化。Qlementine 的跟随系统、浅色和深色主题可在
运行时切换；字体、语言、工具栏透明度、快捷键和图层显示参数也即时生效。

### 地图坐标与合成顺序

三种底图和 `MapCanvas` 均使用 Web Mercator。加载数据时 GDAL/OGR 将图层范围转换为
WGS 84 供“缩放至图层”使用；渲染时栅格由 GDAL 动态重投影到当前
Web Mercator 视窗，矢量由 Mapnik 依据图层 SRS 重投影。

绘制顺序为：

1. 当前选择的底图瓦片（默认 OSM）；
2. 从图层列表底部到顶部依次绘制所有可见图层，矢量与栅格可以任意交错；
3. 当前选中要素的白色外描边和橙色高亮；
4. 框选矩形与界面控件。

拖动排序调用 `QAbstractListModel::beginMoveRows/endMoveRows`，更新后触发新
generation 的离屏渲染，因此 UI 顺序、`LayerSnapshot` 快照顺序与 Canvas
合成顺序始终一致。

## 元信息与属性表

元信息在用户打开详情时才通过 GDAL/OGR 读取，避免拖慢普通图层加载。
矢量元信息包含驱动、源图层、几何类型、要素/字段数量、字段存储类型、
字符编码和完整投影定义；栅格元信息包含尺寸、波段数、像素与块大小、
波段数据类型、颜色解释、NoData、仿射变换和完整投影定义。

`AttributeTableModel` 是只读 `QAbstractTableModel`。打开属性表时以 RAII
方式读取所选 OGR 图层并缓存字段字符串；`TableView` 只实例化可见单元格。
列头排序会优先按数值比较，在不能解析为数值时使用本地化文本比较；查询
对指定列执行不区分大小写的包含匹配。属性表是非模态浮动窗口，地图及其他
面板仍可继续操作。字段较少时各列自动均分并填满表格视口；字段较多时保持
可读的最小列宽，通过横向滚动查看其余字段。

## 栅格渲染策略

### 1. 即时打开与异步统计

打开 GeoTIFF 时，主线程只读取尺寸、地理变换、坐标系、波段数、NoData
和已经存在的统计元数据。没有统计信息时先使用数据类型范围作为可用默认
值，使图层可立即加入地图。随后 `QtConcurrent` 后台任务以 128×128
低分辨率样本估计各波段范围，并在完成后更新图层拉伸值。该过程不会在
文件打开对话框之后阻塞界面。

### 2. 随缩放级别选择金字塔

`RasterRenderer` 不读取整幅影像，而是把当前 EPSG:3857 视窗范围和
Canvas 像素尺寸交给 GDAL Warp。参数 `-ovr AUTO` 会按目标地面分辨率
自动选择最接近的 internal overview 或外部 `.ovr`：

- 地图缩小时读取更粗的金字塔层，减少磁盘读取和解压；
- 地图放大时逐步选择更高分辨率层，显示更多细节；
- 没有 overview 时 GDAL 仍直接输出屏幕目标尺寸的粗采样，不生成整幅
  中间图像，也不修改用户源文件。

对于特别大的生产数据，建议预先创建 overview，例如：

```bash
gdaladdo -r average your-data.tif 2 4 8 16 32
```

如不希望修改 GeoTIFF，可让 GDAL 生成外部 `.ovr`。GeoReader 当前不会
未经确认写入用户数据；后续可增加显式的“构建金字塔”命令和应用缓存目录。

### 3. 视窗缓存与快速换色

GDAL 输出的是当前视窗所需波段的 `Float32` 数据和 alpha mask，而不是
已经着色的 PNG。缓存键由源路径、修改时间、视窗范围、输出尺寸、所选
波段和 NoData 设置组成，采用线程安全的 256 MiB LRU 缓存：

- 修改色带、正反方向、拉伸范围或图层不透明度：复用浮点缓存，仅执行
  C++ 内存插值与着色；
- 修改 RGB 波段组合、NoData、视窗范围或缩放级别：读取新的视窗数据；
- 修改源文件后：文件修改时间变化，旧缓存不会被命中。

单波段色带采用相邻色标的线性 RGB 插值；反向色带通过反转归一化位置
实现。RGB 模式分别对三个所选波段进行拉伸。除手动最小值–最大值外，
2%–98% 累计裁剪、均值 ±2σ 和直方图均衡化均从当前视窗缓存估算，不会
重新读取源数据；直方图使用 1024 个区间。GDAL 生成的 alpha mask 与图层
不透明度相乘，NoData（包括 `nan`）保持透明。

### 4. 后台渲染与过期帧丢弃

地图叠加层在 `QtConcurrent` 工作线程中渲染。连续平移或缩放会增加
generation 编号并由短计时器合并频繁请求；任务结束时只有与当前
generation 一致的结果才能进入 Canvas，过期帧会被丢弃。这样既避免 UI
线程进行重投影和逐像素着色，也避免旧视图覆盖用户的新视图。

### 5. 实时像元查询

栅格识别模式使用十字光标。Widgets 定时器以 75 ms 周期持续消费最新鼠标坐标，
而不是等待鼠标停止或单击；仅在坐标变化时查询。只读 GDAL dataset 句柄
按文件复用，鼠标移动不会反复打开文件。查询结果包含所有可见栅格的像素
行列号和各波段值。

## C++20 与资源安全

- GDAL/OGR 句柄使用带自定义析构器的 `std::unique_ptr` 或受控的
  `std::shared_ptr`，确保异常和提前返回时仍正确关闭；
- 后台结果使用值语义结构体，缓存内容用 `std::shared_ptr` 保证跨线程
  读取期间的生命周期；
- `std::clamp`、结构化绑定、`[[nodiscard]]`、RAII 和不可变快照用于
  减少边界错误与隐式所有权；
- 渲染缓存由 `QMutex` 保护，Widgets/GUI 状态只在主线程更新；
- CMake 强制 `CMAKE_CXX_STANDARD 20` 且关闭编译器扩展。

## 编译、测试与依赖

使用 C++20、CMake 3.25+、Qt 6.8+、GDAL 3.8+、Mapnik 4。Qlementine 1.4.2 的源码和 SHA-256 固定在 CMake 中。
开发依赖可以来自 Homebrew 或 vcpkg；发行包必须自带实际使用的第三方运行库。`vcpkg.json` 固定 baseline，关闭 GDAL 默认可选格式，仅声明需要的科学数据、图像和空间数据库功能。

```bash
# macOS 开发依赖
brew install cmake ninja qt gdal mapnik
# Homebrew GDAL 缺少 HDF4 时构建匹配的开发用驱动
bash scripts/build_hdf4_plugin.sh
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGEOREADER_BUILD_TESTS=ON \
  -DGEOREADER_GDAL_PLUGIN_DIR="$PWD/.test-deps/hdf4-driver"
cmake --build build-native --parallel 6
ctest --test-dir build-native --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

测试夹具是小型真实 NetCDF classic、NetCDF4、HDF5、HDF4 SDS 文件。Python 只用于开发工具和夹具生成，不进入 APP。原生 UI 测试只有 `GEOREADER_BUILD_TESTS=ON` 才编译。

Windows 使用 VS 2022 C++ 工具链，Linux 使用支持 C++20 的编译器；可使用 `scripts/build.ps1` / `scripts/build.sh`、`cmake/triplets` 和 `.github/workflows/package.yml` 的 vcpkg 配置。Linux 保留 Wayland、X11 和输入法所需插件，不能为了体积删除这些运行依赖。

## 精简运行包

不能简单删除动态库：从 GeoReader 可执行文件及显式启用的插件出发，递归收集实际依赖，缺失依赖时打包失败。

macOS 使用 `scripts/bundle_macos.py`：

- 只复制必要 Qt 模块、Cocoa 平台、系统 TLS 和 SVG 图标插件；发行版不包含 QML、QtTest、offscreen、SQL 驱动或开发头文件。
- Mapnik 仅保留 OGR/GDAL、GeoJSON、Shapefile 输入；栅格由 GDAL 渲染，不需要 Mapnik raster 插件。
- 对动态库去重、移除调试符号、改写加载路径，然后签名并审计；生成 `GeoReader.dependencies.json`，列出依赖边、最低系统版本和体积。
- 内置 GDAL 数据、PROJ 核心数据库；不包含未使用的区域高精度转换网格。
- `scripts/build_minimal_gdal.py` 构建匹配 ABI 的 GDAL，内置 HDF4，保留 GeoTIFF、HDF4/HDF5、NetCDF、PNG/JPEG、VRT/MEM、GeoPackage、GeoJSON、Shapefile。GDAL 内部所需 SQLite 等依赖仍保留；关闭 Arrow、PDF、远程数据库等可选格式。
- GDAL 3.13.3（本地）和 3.12.4（固定 vcpkg）源码均校验 SHA-256；不允许随意替换不同 ABI 的运行库。

```bash
python3 scripts/build_minimal_gdal.py
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGEOREADER_BUILD_TESTS=OFF
cmake --build build-release --parallel 6
export GEOREADER_GDAL_PREFIX="$PWD/.test-deps/gdal-minimal"
bash packaging/macos/package_dmg.sh build-release/GeoReader.app arm64 dist/release
```

打包流程会审计依赖、执行驱动自检、验证签名和 DMG；保留主分支已有的磁盘映像创建重试。测试包可显式添加 `--test-runtime`，只用于隔离交互验证，不作为正式安装包。

Windows/Linux 使用 CMake `GET_RUNTIME_DEPENDENCIES` 收集实际导入的 vcpkg 库，取代整目录打包。Qt 的部署流程负责平台和输入法插件。三类科学格式必须通过 `--runtime-check`；这两个平台的安装包尚待各自 CI/真机验证，CI 在部署前构建精简 GDAL（内置 HDF4），仅替换当前仓库私有 vcpkg 目录中的同 ABI 运行库；不会修改系统安装。

本地 Homebrew 二进制的最低系统要求是 macOS 27.0，当前本地包据实写入此要求。CI 使用 macOS 12 triplet 和 Qt 6.8，并设置 `GEOREADER_REQUIRE_MACOS12=1`：任何库要求更新系统都会阻止打包，不能只改 Info.plist 冒充兼容。

## 维护与发布

README 只保留功能、截图和基础编译方式。工程细节记录在本文件及关联设计文档。发布前运行 C++/UI 测试、Python 工程配置检查、独立运行和依赖审计；未经验证的平台不得标记为验收通过。
项目使用 PolyForm Noncommercial License 1.0.0；发行包包含许可证与第三方声明。具体权利以 LICENSE 为准。
