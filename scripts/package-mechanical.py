#!/usr/bin/env python3
"""Package the licensed models/DXFs with fixed source attribution."""
from pathlib import Path
import argparse
import re
import subprocess
import sys
import zipfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--version', default='v0.1.1')
    ap.add_argument('--output', type=Path, default=Path('dist'))
    args = ap.parse_args()
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?',args.version):
        ap.error('Expected a release version such as v0.1.1')
    root = Path(__file__).resolve().parents[1]
    subprocess.run([sys.executable,str(root/'mechanical/verify_sources.py')],check=True)
    files = {p.relative_to(root).as_posix():p.read_bytes() for p in (root/'mechanical').rglob('*')
             if p.is_file() and '__pycache__' not in p.parts}
    for name in ('LICENSE','docs/HARDWARE.md'):
        files[name] = (root/name).read_bytes()
    files['README.md'] = f'''# ChuniF4 {args.version} 机械模型 / DXF 包

**手台机体的 3D 模型和原版 DXF 文件来自另一个开源仓库：[Umi4Life/NAGEKI-3.33](https://github.com/Umi4Life/NAGEKI-3.33)。**

请先打开 mechanical/README.md，查看作者、固定提交和原版 / 修改版的区别。
模型与派生几何来自 Umi4Life/NAGEKI-3.33，继续采用 MPL-2.0；
原作者及相关 5mimusi、toxikmango 来源均在说明中署名。
mechanical/LICENSE 保留完整许可，sources.json 记录逐文件 URL 和哈希。

包含 47 个 STL、2 个 F3D、6 张原版 DXF、4 张背板通孔改版 DXF。
运行 python mechanical/verify_sources.py 可只读校验来源和文件完整性。
STL / DXF 的孔位、弹簧支撑及板厚适配仍需实物确认。
STM32 电气引脚见 docs/HARDWARE.md。

原始 Ongeki-Lever 中未明确许可的额外零件仅链接到作者仓库；本包
没有把这些额外零件重新声明为 MIT。工具脚本 / 项目自有说明适用根目录 MIT。
'''.encode('utf-8')
    args.output.mkdir(parents=True,exist_ok=True)
    archive = args.output/f'ChuniF4-{args.version}-mechanical.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(files.items()):
            z.writestr(f'ChuniF4-{args.version}/{name}',data)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
    print(f'PASS: {archive.resolve()} ({len(files)} files), licenses and provenance included.')


if __name__ == '__main__':
    main()
