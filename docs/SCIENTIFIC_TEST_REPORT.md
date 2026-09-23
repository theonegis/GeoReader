# 多维科学数据与独立运行验收

日期：2026-09-24。平台：macOS Apple Silicon。Qt 6.11.2、GDAL 3.13.3、HDF4 4.3.1。

## 已实现

读取器使用 GDAL 多维数组接口，支持文件中的数值变量与组路径、空间轴/时间轴选择、
时间和其他维度切片。无地理参考的数组使用像素视图，具有规则地理参考的变量进入地图。
单击像元可绘制时序、行剖面或列剖面；提供时间播放、固定/自动色标、8 种色带及反转、
属性、邻域原始/物理数值、抽样直方图、CSV 和预览 PNG 导出。

合并保留主分支 Qt Widgets + Qlementine 界面、数据集分组、原有地图工具与视窗缓存。
科学数据功能通过原生停靠面板接入，没有恢复旧 QML 界面。

## 数据验证

测试使用仓库内的小型真实格式文件，数据值由公式构造，便于独立核对。

| 文件 | 覆盖内容 | 结果 |
|---|---|---|
| `netcdf-classic.nc` | NetCDF classic，时间/纬度/经度，缺测值 | 通过 |
| `netcdf4.nc` | NetCDF4/HDF5，物理值缩放与时间坐标 | 通过 |
| `permuted.nc` | 纬度/时间/经度的非标准维度顺序 | 通过 |
| `levels.nc` | 时间/深度/纬度/经度四维切片 | 通过 |
| `scientific.h5` | HDF5 组内变量、坐标、scale/offset | 通过 |
| `scientific.hdf` | HDF4 SDS 多维变量和切片读取 | 通过 |

模型测试包含 220 个断言，核对数值、缺测值、坐标、边界、轴选择、
深度索引、行列剖面、邻域表、CSV 内容、色带和显示范围。

Qt 界面测试实际点击“取消”“添加变量”、地图/像素预览、下一帧、播放/暂停，
覆盖 NetCDF classic、NetCDF4、HDF5、HDF4。核对点击位置曲线值，验证图层重排、
删除后不会收到过期曲线。附加 GeoJSON 显示、要素查询、属性表及筛选回归通过。

5 组 CTest 全部通过：ScientificData、ScientificUI、Model、Interaction、Multidimensional。
另有 20 项 Python 工程配置检查全部通过。HDF5 读取与释放采用统一锁，避免并发访问导致句柄异常。

证据：`tests/output/native-ctest.log`、`python-tests.log`、`icon-generation.log`，以及同目录原生界面截图。

## 打包与独立运行

正式包：`dist/3.1.0/GeoReader.app` 和 `dist/3.1.0/GeoReader-macOS-arm64.dmg`。
使用 Qt 6.11.2、GDAL 3.13.3（按需构建）、HDF4 4.3.1；正式构建关闭测试开关。

| 指标 | 原完整依赖包 | 当前精简发行包 |
|---|---:|---:|
| 动态二进制文件 | 388 | 69 |
| DMG | 约 133 MiB | 53.11 MiB（55,694,553 字节） |
| APP 文件总量 | — | 125.21 MiB（131,289,334 字节） |

安装包缩小约 60%。正式包不含 QtTest 或 offscreen；Qt、Mapnik 及所需驱动的传递依赖仍完整保留。
`GeoReader.dependencies.json` 记录库、依赖关系、最低系统要求和文件大小。

**实测通过**：69 个 Mach-O 文件依赖审计、签名校验、DMG 校验；最终发行包在禁止读取
Homebrew、开发依赖和发行编译目录的沙箱中读取 NetCDF4、HDF5、HDF4 真文件，
即使 GDAL/PROJ 环境变量故意设错也正常。实际 GDAL 路径位于 APP 内。
核心驱动自检同时确认 GTiff、GPKG、GeoJSON、ESRI Shapefile 存在。

另行构建的测试包采用相同精简 GDAL，并加入 QtTest/offscreen；在相同类型的隔离沙箱中，
四种文件变体的变量选择、点击曲线、切片/播放及 GeoJSON 回归全部通过。
最终发行包没有测试入口，使用驱动和真实文件自检验证独立运行。

证据：`tests/output/release-package.log`、`release-standalone.json`、`standalone-ui.log`。
SHA-256 见 `dist/3.1.0/SHA256SUMS.txt`。打包配方 `build_minimal_gdal.py` 已在本机运行成功。

## 范围与遗留限制

- 测试覆盖上述真实格式文件，不等于已经验证所有卫星产品和 HDF-EOS 地理定位规则。
- 非规则网格与缺少坐标系的数组提供像素检查；尚未实现曲线网格重网格化、多点/区域平均时序。
- 大变量按预览抽样统计；预览 PNG 最长边 800 像素。不是原始分辨率栅格导出。
- 本轮包为 macOS arm64，本地临时签名，未做开发者公证；Windows/Linux 未进行安装包真机验收。
- 本次本地 Homebrew 验证包需要 macOS 27.0，不能作为 macOS 15 的正式发行件。发行 CI 已改为 macOS 15 编译目标并强制审计每个动态依赖；须由该 CI 产物完成 Sequoia 验收后方可发布。
