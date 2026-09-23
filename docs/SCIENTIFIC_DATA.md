# GeoReader 科学数据查看器

## 面向使用者

发行版 APP 自带 Qt、GDAL、NetCDF、HDF5、HDF4 驱动及相关动态库。
使用者无需安装 GDAL、Homebrew、Python 或设置 GDAL/PROJ 环境变量。
本轮实际构建和验证的平台是 macOS Apple Silicon；其他平台尚未进行安装包验收。

### 打开变量

1. 点击左侧“打开文件”，或按原有的打开快捷键。
2. 选择 `.nc`、`.nc4`、`.cdf`、`.h5`、`.hdf5`、`.he5`、`.hdf`、`.h4` 或 `.hdf4` 文件。
3. 在变量窗口按名称或完整组路径检索变量，例如 `/science/temperature`。
4. 确认 X（列）、Y（行）和时间轴。软件会依据维度类型、名称和坐标属性给出初值；未知轴必须由使用者确认。
5. 时间、深度、成员等非空间维度的索引从 **0** 开始。选择“添加变量”后才会生成图层，取消不会添加图层。

具有有效地理参考的规则网格进入地图，可与矢量图层叠加；没有地理参考的数组在像素视图显示。
软件不会把没有坐标系的数组行列号解释成经纬度。经纬度坐标具有 `degrees_east` / `degrees_north`
单位并能建立规则仿射网格时，采用 WGS84 地理坐标。

### 时间、深度和空间剖面

- 在图层面板点击“变量切片 / 像元时序”，打开所选变量的专用面板。
- 分别改变时间、深度或其他维度的索引，地图和像素预览随之刷新。
- 使用上一帧、下一帧、播放/暂停和速度控件浏览时间变化；播放到末帧后循环。
- 在地图或像素预览上单击，查看该像元沿时间维度的曲线；其他维度保持固定。
- 曲线下拉框可切换为行剖面（X）或列剖面（Y），需要先点击一个位置。
- 曲线横轴使用实际坐标值，因此不等间隔时间不会被画成等间隔。悬停可查看具体时间和数值。
- `_FillValue` / GDAL NoData 及非有限值在图像中透明、曲线中断开，不按 0 连接。
- `scale_factor` / `add_offset` 按 `物理值 = 原始值 × scale_factor + add_offset` 应用；普通波段不会被误认为时间或 RGB。
- Gregorian 系列日历的秒、分、小时、天数时间坐标可转换为日期。其他日历保留数值时间和原始单位、日历名称，避免假造日期。

### 常用检查和显示功能

- Viridis、Plasma、Inferno、Magma、Cividis、Turbo、Terrain、Gray 色带及反转。
- 自动色标与固定最小/最大值。跨时间比较时建议固定范围，避免每帧自动拉伸造成误判。
- 当前切片色标、变量单位、像元位置和已选位置标记。
- “数值”：点击位置附近最多 5×5 邻域的行、列、原始值和物理值。
- “分布”：预览抽样的有效数量、均值和 32 区间直方图。它是**抽样统计**，不是整个变量的精确统计。
- “属性”：文件、完整变量路径、数值类型和变量属性。
- “导出 CSV”：当前时序或剖面的坐标、标签、物理值、单位和日历；缺测值留空。
- “导出预览 PNG”：导出当前色带和范围下的预览，最长边 800 像素，不冒充原始分辨率影像。

数据文件以只读方式打开；切片和显示使用内存 VRT，不向源文件写入统计、修改变量或创建金字塔。
文件读取和曲线计算在后台进行；过期结果不会覆盖新变量或已删除的图层。
NetCDF 与 HDF5 可能共用非线程安全的 HDF5 库，因此读取操作使用同一个锁，包含句柄释放。

## 开发与验证

开发机的编译依赖与发行版运行时依赖是两回事：编译可使用 Homebrew，发行版必须通过内置依赖检查。

```bash
# 开发机依赖
brew install cmake ninja qt gdal mapnik dylibbundler

# Homebrew GDAL 缺少 HDF4 时，在项目内构建匹配版本的驱动
bash scripts/build_hdf4_plugin.sh

cmake -S . -B build-scientific -G Ninja \
  -DGEOREADER_BUILD_TESTS=ON \
  -DGEOREADER_GDAL_PLUGIN_DIR="$PWD/.test-deps/hdf4-driver"
cmake --build build-scientific --parallel 6
ctest --test-dir build-scientific --output-on-failure

cmake --install build-scientific --prefix "$PWD/stage-scientific"
bash packaging/macos/package_dmg.sh \
  stage-scientific/GeoReader.app arm64 dist/scientific
```

HDF4 插件必须与 GDAL ABI 匹配。当前可复现配方固定 GDAL 3.13.3 和 HDF4 4.3.1，源码下载校验 SHA-256。
测试目录包含小型真实格式文件，无需使用者另外下载卫星产品。生成脚本为 `tests/generate_scientific_fixtures.py`，
其 Python 依赖只用于开发测试。

打包脚本会收集 GDAL 插件的传递依赖，打包 GDAL 数据与 PROJ 的 `proj.db` / `proj.ini`，
对每个 Mach-O 依赖进行审计，并运行 `--runtime-check` 真文件检查。可选的地区高精度基准转换网格未打包。

```bash
python3 scripts/audit_macos_bundle.py dist/scientific/GeoReader.app
QT_QPA_PLATFORM=offscreen \
  dist/scientific/GeoReader.app/Contents/MacOS/GeoReader --runtime-check \
  tests/data/netcdf4.nc tests/data/scientific.h5 tests/data/scientific.hdf
```

## 当前支持边界

- 主要针对二维及更高维的数值数组，支持组内变量。坐标向量用于轴信息。
- HDF4 测试覆盖 SDS 数值变量；复杂 HDF-EOS 产品的地理定位仍取决于驱动提供的元数据。
- 非规则/曲线网格、缺少 CRS 的数组可按像素检查，本轮不实现非结构网格地图渲染。
- 同一时刻显示一个选定像元的一条曲线，尚未实现多点对比、区域平均时序或数据编辑。
- 单条曲线最多读取 100 万点；超过此限制会明确提示先创建数据子集。
- Windows/Linux 的依赖配置可继续使用，但本轮未宣称其安装包已经通过真机测试。

交互参考：[HDFView](https://support.hdfgroup.org/documentation/hdfview/latest/) 的变量路径、属性、切片和数值检查，
以及 [Ncview](https://cirrus.ucsd.edu/ncview/) 的切片播放、色带和剖面浏览。
