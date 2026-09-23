# 多维科学数据与独立运行验收

日期：2026-09-24。平台：macOS Apple Silicon。Qt 6.11.2、GDAL 3.13.3、HDF4 4.3.1。

## 已实现

读取器使用 GDAL 多维数组接口，支持文件中的数值变量与组路径、空间轴/时间轴选择、
时间和其他维度切片。无地理参考的数组使用像素视图，具有规则地理参考的变量进入地图。
单击像元可绘制时序、行剖面或列剖面；提供时间播放、固定/自动色标、8 种色带及反转、
属性、邻域原始/物理数值、抽样直方图、CSV 和预览 PNG 导出。

原项目存在 QML/CMake 已引用但源码缺失的栅格渲染器、属性表模型和模型测试；
本轮补齐这些模块及对应控制器接口，使新增功能能够实际编译和运行。

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

两个 CTest 测试均通过。此前针对共享 HDF5 库的并发释放问题，增加统一读取锁，
随后连续重复三轮通过；最终加入 NetCDF4 UI 和矢量回归后再次通过。

证据：`tests/output/ctest.log`、`ctest-repeat.log`、`icon-generation.log`，以及同目录界面截图。

## 打包与独立运行

最终包与隔离运行结果见 `tests/output/bundle-audit.log`、
`tests/output/standalone-runtime.json`、`tests/output/standalone-ui.log`。
完整 APP 位于 `dist/scientific/GeoReader.app`，安装映像为 `dist/scientific/GeoReader-macOS-arm64.dmg`。

**实测通过**：388 个 Mach-O 文件的依赖审计通过；隔离沙箱下三个驱动均可用，
三个真实格式文件均读取成功。报告中的 GDAL 加载位置为 APP 的 `Contents/Frameworks/`。
同一隔离环境下，四种文件变体的变量选择、点击曲线、切片/播放及矢量回归全部通过。
`codesign --verify --deep --strict` 与 DMG 校验均通过。安装包约 133 MiB，SHA-256 记录在 `dist/scientific/SHA256SUMS.txt`。

打包流程收集 Qt、GDAL、HDF4/HDF5/NetCDF、Mapnik 及其传递依赖，
附带 GDAL 数据和 PROJ 基本数据库。统一动态库路径并消除兼容名称导致的重复库加载。
`audit_macos_bundle.py` 禁止任何运行依赖指向 Homebrew 或开发目录。
`test_macos_standalone.sh` 通过 macOS 沙箱禁止读取 Homebrew、本地开发库和编译目录，
并故意设置无效的 GDAL/PROJ 环境变量，要求 APP 使用自身资源运行真实文件与界面测试。

## 范围与遗留限制

- 测试覆盖上述真实格式文件，不等于已经验证所有卫星产品和 HDF-EOS 地理定位规则。
- 非规则网格与缺少坐标系的数组提供像素检查；尚未实现曲线网格重网格化、多点/区域平均时序。
- 大变量按预览抽样统计；预览 PNG 最长边 800 像素。不是原始分辨率栅格导出。
- 本轮包为 macOS arm64，本地临时签名，未做开发者公证；Windows/Linux 未进行安装包真机验收。
- 原有 Python 检查 15 项中 9 项通过；另 6 项因原工作区缺少 `.github/workflows/package.yml`
  报文件不存在。这与数据读取/界面测试不同，未删除这些检查或将错误伪报为通过。
