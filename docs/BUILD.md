# 构建与测试

## 软件测试

Python 3.11+、GCC。测试不会连接真实 HID，不需要安装 `hidapi`；Python 需要 Tcl/Tk 以导入图形模块。Ubuntu 可安装 `python3-tk` 后使用系统 Python。

```powershell
python scripts/check.py
```

脚本运行原生校准映射、Flash 故障注入、从实际 HID / 主循环函数提取的模拟测试、Python 协议与 GUI 状态测试，以及从实际 DLL 源码提取的全范围方向测试。产物写入忽略的 `build/`。

GitHub Actions 配置作为 [手动启用示例](software-checks.yml.example) 提供。需要时将其放到 `.github/workflows/software-checks.yml`；通过 OAuth 推送该路径需要 `workflow` 权限。首版保留本地验证结果，没有声称已通过远端 CI。

## STM32 固件（Windows / Keil）

已验证 Arm Compiler **6.24**，Keil STM32F4xx_DFP **3.1.1**。从 Keil 打开 `firmware/MDK-ARM/ChuniF4.uvprojx`，target 为 `ChuniF4`。

命令行编译需明确传入自己的安装位置，或设置进程环境变量 `KEIL_UV4`：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-firmware.ps1 -Keil "<Keil安装目录>\UV4\UV4.exe"
```

使用 PowerShell 7 时可把 `powershell` 换成 `pwsh`。脚本只编译，不烧录；公开工程所有 Before / After Make 自定义动作都已关闭。HEX 输出在 `firmware/MDK-ARM/ChuniF4/`。如需 BIN，可使用编译器随附 `fromelf --bin --output ChuniF4.bin ChuniF4.axf`。

IAR 工程保留在 `firmware/EWARM/`，Flash ICF 同样预留 sector6/7，但尚未进行 IAR 构建验收。CubeMX `.ioc` 是配置参考；重新生成前保留手工 HID 修改，并复核链接脚本与 `lever_storage*` 的登记。

## Windows x64 游戏 DLL

安装 MinGW-w64 GCC，并确保 `gcc` 可用：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build-game-io.ps1
```

得到 `build/game-io/chunif4_mu3io.dll`，脚本随后直接加载它进行离线测试，不连接实际设备。也可以直接运行：

```powershell
gcc -O2 -Wall -Wextra -Werror -shared game-io/chunif4_mu3io.c -o chunif4_mu3io.dll -lsetupapi -lhid -static-libgcc
python scripts/test-game-io.py --library ./chunif4_mu3io.dll
```

## 上位机

```powershell
python -m pip install -r host/requirements.txt
python host/io4_hid_gui.py
```

上位机源码包含全部功能，无需 EXE。若需要自己打包：

```powershell
python -m pip install -r host/requirements-build.txt
python -m PyInstaller --noconfirm --clean --windowed --name OngekiControlFlash --paths host --distpath dist --workpath build/pyinstaller host/io4_hid_gui.py
```

分发时必须保留整个 `dist/OngekiControlFlash/`，并随包提供 Python、Tcl/Tk、hidapi、PyInstaller 所需版权 / 许可。此仓库首次公开发布优先提供可检查源码、固件包和 DLL 包，不把本机旧打包缓存当成新的发布构建。

## 发布包

完成固件、BIN 导出和 DLL 构建后，运行 `python scripts/package-release.py --version v0.1.0`。BIN 放在 `build/ChuniF4.bin`。脚本验证 HEX 校验和、HEX/BIN 对应及程序不侵入校准区，然后打包固件、DLL、Python 上位机源码、文档和许可到 `dist/`。发布包带逐文件 SHA-256；不包含游戏、个人配置、历史备份或本机日志。
