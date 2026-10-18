# 手台模型、DXF 与来源

**手台机体的 3D 模型和原版 DXF 文件来自另一个开源仓库：[Umi4Life/NAGEKI-3.33](https://github.com/Umi4Life/NAGEKI-3.33)。** 本仓库整理收录这些机械文件，并标注来源与改动。

机械文件及其派生 DXF 遵循上游 **MPL-2.0**，见 [机械许可全文](LICENSE)。仓库根目录的 MIT 许可不覆盖这些模型。

## 作者与固定版本

| 用途 | 作者 / 上游 | 本次引用版本 |
|---|---|---|
| 手台外壳、WAD 侧键、3.33 摇杆改件、原版 DXF | [Umi4Life/NAGEKI-3.33](https://github.com/Umi4Life/NAGEKI-3.33) | [`f7b277a77f77a303357a6cdc2e73a1d9f72dd2f2`](https://github.com/Umi4Life/NAGEKI-3.33/tree/f7b277a77f77a303357a6cdc2e73a1d9f72dd2f2) |
| NAGEKI 项目原始来源 / 分叉关系 | [5mimusi/NAGEKI](https://github.com/5mimusi/NAGEKI) | 由 3.33 仓库的 GitHub parent / source 元数据确认；本次几何以 3.33 为准 |
| 3.33 README 引用的原始摇杆机构 | [toxikmango/Ongeki-Lever](https://github.com/toxikmango/Ongeki-Lever) | [`cc8f2705098db12ef2ed6fee1cbd9b235e01c33f`](https://github.com/toxikmango/Ongeki-Lever/tree/cc8f2705098db12ef2ed6fee1cbd9b235e01c33f) |

原始 Ongeki-Lever 仓库在上述提交中未找到独立 LICENSE / COPYING 文件，因此这里只标注原作者及固定下载链接；其中需要补充的原版夹具、齿轮等零件请从原作者仓库获取并核对授权。随本仓库提供的 `LEVER/` 文件来自 NAGEKI-3.33 自己的目录，按其随附 MPL-2.0 声明保留。

上游文件从指定 Git 提交直接导出，保持原始路径和内容，不从其他版本重新拼装。每个文件的来源 URL、原路径、提交、Git blob SHA-1、SHA-256、修改标记见 [sources.json](sources.json)。

## 3D 模型

| 文件 / 目录 | 内容与选择说明 |
|---|---|
| [NAGEKI 3.33.f3d](upstream/NAGEKI-3.33/CASE/NAGEKI%203.33.f3d) | 上游原生 Fusion 设计文件，用于继续查看和修改结构 |
| [the-whole-case.stl](upstream/NAGEKI-3.33/CASE/the-whole-case.stl) | 整壳展示模型，不能将其当成全部分块都已完成装配验证 |
| [CASE/](upstream/NAGEKI-3.33/CASE/) | 顶、底、前后、WAD 侧墙的分块打印模型与原始板框 DXF |
| [WAD/](upstream/NAGEKI-3.33/WAD/) | 侧键按钮、shield、四种背板、传感器支架及 cover；传感器支架有多个高度，按实际传感器选择 |
| [LEVER/](upstream/NAGEKI-3.33/LEVER/) | 3.33 握把、带止挡底座、六角螺杆适配件、滚花轴齿轮和 Abi 联轴器参考 CAD |

共提供 **47 个原版 STL、2 个原版 F3D**。本次没有重新缩放、重建或调整它们的孔位。目录中的支架高度、背板图案、摇杆方案属于备选件，按上游装配说明选择，不能把所有版本全部重复打印。原作者的 Arduino / Pro Micro 引脚说明与本项目 STM32 GPIO 不同，电气接线以 [本项目接线表](../docs/HARDWARE.md) 为准。

## 六张原版 DXF 的逐文件出处

下表中的路径均相对于 [固定版本上游目录](upstream/NAGEKI-3.33/)。每个原文件与上游 Git blob 一致，完整永久链接和 SHA-256 在 `sources.json` 中。

| 用途 | 上游原始文件路径 | 数量参考 |
|---|---|---:|
| 顶部结构板 | [CASE/top/top.dxf](upstream/NAGEKI-3.33/CASE/top/top.dxf) | 1 |
| 底板 | [CASE/bottom/bottom.dxf](upstream/NAGEKI-3.33/CASE/bottom/bottom.dxf) | 1 |
| 后板 | [CASE/sides/side north.dxf](upstream/NAGEKI-3.33/CASE/sides/side%20north.dxf) | 1 |
| 前板 / 玩家侧 | [CASE/sides/side south.dxf](upstream/NAGEKI-3.33/CASE/sides/side%20south.dxf) | 1 |
| WAD 左右侧板 | [CASE/wad/wad.dxf](upstream/NAGEKI-3.33/CASE/wad/wad.dxf) | 共 2，装配方向需核对 |
| 顶部透明亚克力 | [GRAPHICS/acrylic cover laser cut.dxf](upstream/NAGEKI-3.33/GRAPHICS/acrylic%20cover%20laser%20cut.dxf) | 1，上游采用 4 mm 材料 |

这些是原版轮廓，不等于已完成 4 mm 木板、2 mm 外饰亚克力或其他材料厚度的最终适配。3.0 与 3.33 的孔位 / 尺寸不能混用。原作者注明木板 DXF 方案尚未测试；加工应先用样片核对实际按钮、摇杆与 WAD 安装。

## 四张背板通孔改版 DXF

[modified-dxf/backplate-spring-holes/](modified-dxf/backplate-spring-holes/) 是本项目已有修改版，**不是上游原版 DXF**，修改者记为 ChuniF4 contributors，派生几何继续采用 MPL-2.0。

| 修改版文件 | 对应原始模型 |
|---|---|
| `backplate-left-CUT.dxf` | `WAD/backplate left.stl` |
| `backplate-right-CUT.dxf` | `WAD/backplate right.stl` |
| `backplate-akari-CUT.dxf` | `WAD/backplate akari.stl` |
| `backplate-setsuna-CUT.dxf` | `WAD/backplate setsuna.stl` |

保留原外轮廓与原有通孔，省去凹槽雕刻；akari / setsuna 两个弹簧凹槽的外侧轮廓改为通孔。每图独立移到 XY 最小值为零，单位 mm、比例 1:1，不添加切缝补偿。尺寸与处理记录见 [measurements.json](modified-dxf/backplate-spring-holes/measurements.json)。

弹簧孔约 15.305 mm，横向中心距约 135 mm；中间板各有六个内孔轮廓。打穿后原凹槽底部不再承托弹簧，需要实际安装结构或垫件承担定位与支撑。DXF 校验和与闭合轮廓检查不能证明实物配合或承载。

## 文件校验

仓库根目录运行 `python mechanical/verify_sources.py`，验证文件 SHA-256、原版 Git blob、F3D 压缩包完整性，以及四张修改版 DXF 的毫米单位和闭合轮廓数。此命令只读文件，不进行制造适配验证。
