# 许可证与来源

根目录 [MIT LICENSE](LICENSE) 适用于本项目原创的校准 / 存储模块、Python 上位机、MU3 插件、测试、构建脚本与项目文档。以下第三方文件及其派生修改不因根目录许可而被重新许可；原版权头与组件许可均已保留。

| 范围 | 来源 | 许可 |
|---|---|---|
| `firmware/Drivers/STM32F4xx_HAL_Driver/` | ST STM32CubeF4 HAL / LL | 组件 `LICENSE.txt` 指定 BSD-3-Clause；完整 BSD 文本亦见 `firmware/LICENSE` |
| `firmware/Drivers/CMSIS/Include/` | Arm CMSIS | Apache-2.0，完整文本在 `firmware/Drivers/CMSIS/LICENSE.txt` |
| `firmware/Drivers/CMSIS/Device/ST/STM32F4xx/` | ST CMSIS device headers | 组件声明 Apache-2.0；完整文本在 CMSIS 上级目录 |
| `firmware/Middlewares/ST/STM32_USB_Device_Library/`，包括修改过的 HID 类 | ST USB Device middleware | 保留原包 **SLA0044 Rev5/February 2018** 全文，不标为 MIT / Apache / BSD |
| `firmware/Core/` 中 CubeMX 生成文件、`USB_DEVICE/`、启动汇编 | ST STM32CubeMX / STM32CubeF4 生成基础文件 | 保留 ST 原版权头，BSD-3-Clause 文本在 `firmware/LICENSE`；独立原创的 `lever_*` 模块为 MIT |
| `mechanical/upstream/NAGEKI-3.33/` 与派生背板 DXF | Umi4Life/NAGEKI-3.33，分叉自 5mimusi/NAGEKI | MPL-2.0，原 LICENSE 保留；完整文本另见 `mechanical/LICENSE` |

ST USB 许可包含 ST 器件使用限制和禁止将该组件重新置于其他开源许可的条款。它的源代码按原许可随仓库分发；**整个固件并非统一的 MIT 或纯 OSI 许可项目**。移植到非 ST 芯片前需替换相应组件并检查其许可。

本次整理从已有工程选取实际构建所需的依赖，没有升级供应商源码。修改过的 ST HID 文件标注了 ChuniF4 修改范围；原始作者版权保留。CMSIS DSP / NN、无关 MCU 头文件、未使用 USB 类以及预编译供应商库未打包。

官方组件来源：

- [STM32CubeF4](https://github.com/STMicroelectronics/STM32CubeF4)
- [ST USB Device middleware](https://github.com/STMicroelectronics/stm32-mw-usb-device)，当前官方说明仍单独列出 [组件许可](https://github.com/STMicroelectronics/stm32-mw-usb-device/blob/master/LICENSE.md)。本仓库保留的是现有工程随附的 Rev5，未用新版本许可覆盖旧文件。
- [Arm CMSIS](https://github.com/ARM-software/CMSIS_5)

协议参考：

- [Segatools MU3 API](https://github.com/djhackersdev/segatools/blob/master/mu3io/mu3io.h) 和 [IO4 转换](https://github.com/djhackersdev/segatools/blob/master/mu3hook/io4.c)：用于核对输入接口、按键位、摇杆符号和 ADC 方向。插件是本项目适配代码，不包含 Segatools DLL。
- [GEEKiDoS/ongeeki-firmware](https://github.com/GEEKiDoS/ongeeki-firmware)：IO4 按键映射 / 报文布局参考；未把该仓库的完整实现作为本项目依赖分发，也不对其主张 MIT 许可。

运行时依赖由用户安装，不提交其二进制：Python / Tcl/Tk、`hidapi`。`hidapi` Python 绑定和底层 HIDAPI 的许可分别以所安装版本随附的许可为准。Windows 上位机自行打包时，应将 Python、Tcl/Tk、hidapi、PyInstaller 相关版权和许可一起放入发布包。

Windows DLL 发布包附带 `licenses/` 下的 MinGW-w64 runtime 版权声明、GCC GPLv3 文本及 GCC Runtime Library Exception 3.1。它们说明编译时可能链接的运行库部分，不将项目自有源码改为 GPL。

## 机械模型与切割图署名

手台外形、WAD、3.33 摇杆改件和六张原版 DXF 来源为 **Umi4Life/NAGEKI-3.33**，固定提交 `f7b277a77f77a303357a6cdc2e73a1d9f72dd2f2`。其原始项目关系为 **5mimusi/NAGEKI**。原件按上游 Git blob 导出；本项目的背板通孔改版注明 ChuniF4 contributors 的修改及输入 STL，不作为上游原版发布，继续保留 MPL-2.0。

摇杆原始设计另署名 **toxikmango/Ongeki-Lever**，引用提交 `cc8f2705098db12ef2ed6fee1cbd9b235e01c33f`。该提交目录中未找到 LICENSE / COPYING 文件，相关原始零件仅提供作者仓库链接；不将它们直接复制为本项目 MIT 文件。NAGEKI-3.33 自己收录的改件保持其上游路径与许可声明。

详细的作者、目录和逐文件永久链接见 [机械来源说明](mechanical/README.md) 及 [sources.json](mechanical/sources.json)。机械文件及派生几何不受根目录 MIT 重新许可。

SEGA、STM32、Arm、Keil 等名称与 USB 兼容描述中的标识属于相应权利人；提及它们不表示背书。本仓库不授予商标或 USB VID 的使用权。
