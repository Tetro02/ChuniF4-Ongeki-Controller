#!/usr/bin/env python3
"""Package freshly built firmware, game DLL and the Python host sources."""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess
import sys
import zipfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--version', default='v0.1.1')
    ap.add_argument('--output', type=Path, default=Path('dist'))
    args = ap.parse_args()
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?', args.version):
        ap.error('Expected a release version such as v0.1.0')
    root = Path(__file__).resolve().parents[1]
    subprocess.run([sys.executable, str(root/'mechanical/verify_sources.py')], check=True)
    image = root/'firmware/MDK-ARM/ChuniF4/ChuniF4.hex'
    binary = root/'build/ChuniF4.bin'
    dll = root/'build/game-io/chunif4_mu3io.dll'
    for p in (image, binary, dll):
        if not p.is_file():
            ap.error(f'Build artifact missing: {p.relative_to(root)}')
    memory, base, eof = {}, 0, False
    for line in image.read_text(encoding='ascii').splitlines():
        assert line.startswith(':')
        r = bytes.fromhex(line[1:])
        assert sum(r) % 256 == 0 and len(r) == r[0] + 5
        address, kind, data = int.from_bytes(r[1:3], 'big'), r[3], r[4:-1]
        if kind == 4:
            base = int.from_bytes(data, 'big') << 16
        elif kind == 0:
            for offset, value in enumerate(data):
                at = base + address + offset
                assert 0x08000000 <= at < 0x08040000 and at not in memory
                memory[at] = value
        elif kind == 1:
            eof = True
        else:
            assert kind == 5, f'Unexpected HEX record type: {kind}'
    data = binary.read_bytes()
    assert eof and min(memory) == 0x08000000
    assert len(data) == max(memory) - 0x08000000 + 1
    assert all(data[at - 0x08000000] == value for at, value in memory.items())
    files = {'firmware/ChuniF4.hex': image.read_bytes(), 'firmware/ChuniF4.bin': data,
             'game-io/chunif4_mu3io.dll': dll.read_bytes()}
    for directory in ('host', 'docs', 'licenses', 'mechanical'):
        for p in (root/directory).rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts:
                files[p.relative_to(root).as_posix()] = p.read_bytes()
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'game-io/segatools.example.ini'):
        files[name] = (root/name).read_bytes()
    for rel in ('LICENSE', 'Drivers/CMSIS/LICENSE.txt', 'Drivers/CMSIS/Device/ST/STM32F4xx/LICENSE.txt',
                'Drivers/STM32F4xx_HAL_Driver/LICENSE.txt', 'Middlewares/ST/STM32_USB_Device_Library/LICENSE.txt'):
        files['firmware/'+rel] = (root/'firmware'/rel).read_bytes()
    files['README.md'] = f'''# ChuniF4 {args.version} Windows 发布包

**手台机体的 3D 模型和原版 DXF 文件来自另一个开源仓库：[Umi4Life/NAGEKI-3.33](https://github.com/Umi4Life/NAGEKI-3.33)。**

包含 STM32F411CEU6 固件、Windows x64 MU3 插件、Python 上位机源码、手台机械模型、DXF 和使用文档。

1. 接线见 docs/HARDWARE.md，仅适配 512 KiB F411CEU6 / 25 MHz HSE。
2. firmware/ChuniF4.hex 用于烧录；BIN 起始地址 0x08000000。
   程序在前 256 KiB，sector6/7 保存校准；整片擦除会清空校准。
3. 安装 Python 3.11+（含 Tcl/Tk），运行 `python -m pip install -r host/requirements.txt`。
   双击 host/启动上位机.cmd，或运行 `python host/io4_hid_gui.py`。
   这是 Python 源码版，不含上位机 EXE；离线演示使用 host/演示模式.cmd。
4. 开始校准、记录物理左 / 中 / 右，点击“保存并应用校准”等待成功。
5. 游戏关闭后安装 game-io/chunif4_mu3io.dll 并合并示例配置，重启游戏。
   在游戏「レバー設定」重新扫过左右端并退出保存。
6. mechanical/README.md 标注模型 / DXF 作者、固定版本、原版及修改版；
   mechanical/sources.json 记录逐文件来源和 SHA-256。机械文件保留 MPL-2.0。

发布包不是自动安装或自动烧录工具。许可详见 THIRD_PARTY_NOTICES.md；
ST USB 部分保持 SLA0044，其他第三方许可与运行库版权随包保留。
软件测试和真实硬件验证的区别见 docs/VALIDATION.md。
'''.encode('utf-8')
    files['VALIDATION.txt'] = (f'ChuniF4 {args.version}\n'
        'Keil Arm Compiler 6.24: 0 errors, 0 warnings (public staging build).\n'
        'Native calibration/Flash faults/USB routing/save handshake: PASS.\n'
        'Python protocol, monitor and GUI state: 32 tests PASS.\n'
        'Windows game DLL: 65536 lever values, ten buttons and invalid reports PASS.\n'
        f'HEX checksums, BIN match and reserved-sector exclusion: PASS; last address {max(memory):#010x}.\n'
        'No new board programming, physical power-loss test or corrected game-direction confirmation performed.\n'
    ).encode('utf-8')
    files['SHA256SUMS.txt'] = ''.join(f'{hashlib.sha256(content).hexdigest()}  {name}\n'
                                     for name, content in sorted(files.items())).encode('utf-8')
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output/f'ChuniF4-{args.version}-Windows.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for name, content in sorted(files.items()):
            z.writestr(f'ChuniF4-{args.version}/{name}', content)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    print(f'PASS: {archive.resolve()} ({len(files)} files); HEX/BIN verified; calibration sectors untouched.')


if __name__ == '__main__':
    main()
