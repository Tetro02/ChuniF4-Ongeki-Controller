# ChuniF4 音击控制器

基于 **STM32F411CEU6 / 512 KiB Flash / 25 MHz HSE** 的十键与绝对位置摇杆控制器，包含固件、Windows 上位机和 Segatools MU3 输入插件。

自有代码采用 MIT；ST、Arm 等第三方文件保留各自许可。特别是 ST USB Device 中间件及其修改受 **SLA0044** 约束，不能把整个固件视为 MIT。详见 [许可证与来源](THIRD_PARTY_NOTICES.md)。

## 功能

- 十个数字输入：六个主键、两个菜单键、两个侧键。
- ADC1_IN0 / PA0 读取摇杆；三点校准支持电位器正装、反装及中心死区。
- 上位机通过 HID Output Report `0x10` 触发左、中、右各 200 点平均采样。
- 保存校准到 Flash，重启自动加载；双扇区追加日志、CRC、最后提交标记及失败回退。
- 上位机显示按键状态与计数、原始 ADC、校准位置和保存状态；支持离线演示。
- Windows x64 MU3 插件将原始 IO4 HID 转为游戏输入，并修正摇杆左右方向。

## 快速开始

1. 按 [接线表](docs/HARDWARE.md) 接线，检查芯片型号与 25 MHz 晶振。
2. 使用 Keil Arm Compiler 6.24 编译 `firmware/MDK-ARM/ChuniF4.uvprojx`，或使用 Release 中的配套固件。程序起始地址为 `0x08000000`；Flash sector 6/7 用于校准。
3. 安装 Python 3.11 或更新版本（Windows 安装器包含 Tcl/Tk），在仓库根目录运行：

   ```powershell
   python -m pip install -r host/requirements.txt
   python host/io4_hid_gui.py
   ```

4. 连接设备，开始校准，依次记录物理左端、中心、右端，每次保持稳定；点击 **保存并应用校准**，等设备回读确认成功。
5. 按 [游戏接入说明](docs/GAME_SETUP.md) 安装配套 DLL，在游戏测试菜单的「レバー設定」重新记录左右行程并保存。

离线查看上位机：

```powershell
python host/io4_hid_gui.py --demo
```

命令行校准并保存：

```powershell
python host/lever_calibrate.py wizard --deadzone 0
```

## 文件导航

| 目录 / 文档 | 内容 |
|---|---|
| [firmware/](firmware/) | Keil / IAR 工程、CubeMX 配置、MCU 源码及必要第三方依赖 |
| [host/](host/) | Python 图形上位机、CLI、监测模块及测试 |
| [game-io/](game-io/) | Windows x64 MU3 插件源码与最小配置示例 |
| [scripts/](scripts/) | 跨平台软件测试、Windows 编译和打包脚本 |
| [硬件与接线](docs/HARDWARE.md) | GPIO、有效电平、ADC 与 USB 接线 |
| [上位机使用](docs/HOST_GUIDE.md) | 十键检测、三点校准、Flash 保存与常见问题 |
| [校准协议](docs/LEVER_CALIBRATION.md) | `0x10/C0/LC` 命令和状态字段 |
| [Flash 布局](docs/FLASH_PERSISTENCE.md) | 双扇区记录格式、掉电模型、升级保留校准 |
| [构建](docs/BUILD.md) | 开发环境、编译、测试和上位机打包 |
| [验证边界](docs/VALIDATION.md) | 已完成的软件验证与需要实物确认的项目 |

## 输入方向约定

| 物理位置 | 固件 / 上位机位置 | MU3 API 位置 | 经 mu3hook 转换的 ADC |
|---|---:|---:|---:|
| 左 | 0 | -32768 | 65535 |
| 中 | 32768 | 0 | 32767 |
| 右 | 65535 | 32767 | 0 |

固件中的 `invert` 只用于兼容电位器接反；游戏方向转换位于 DLL。上位机 Flash 校准与游戏内行程设置是两套独立设置。

## 开发与限制

```powershell
python scripts/check.py
```

测试不访问实际 USB 设备，也不烧录或擦写 MCU。完整构建与依赖见 [BUILD.md](docs/BUILD.md)。

这是针对本项目 IO4 报文的适配器，不能直接当作任意 STM32、NAGEKI、通用键盘或 XInput 手柄的驱动。LED 输出、投币器及完整 IO4 系统控制尚未实现；DLL 的 LED 回调为兼容入口。设备需在游戏启动前接入，初始化时无设备会返回失败。

本仓库不包含游戏、Segatools 二进制、个人游戏配置、密钥或服务器信息。名称用于说明协议兼容性；本项目与 SEGA、STMicroelectronics、Arm 无隶属或背书关系。
