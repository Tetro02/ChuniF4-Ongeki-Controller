#!/usr/bin/env python3
"""Offline checks only: never opens USB or programs a board."""
from pathlib import Path
import argparse
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--gcc', default='gcc')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    fw = root / 'firmware'
    scratch = root / 'build/test-native'
    scratch.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONUTF8='1')

    def run(command):
        subprocess.run([str(x) for x in command], cwd=root, env=env, check=True)

    common = [args.gcc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', fw/'Core/Inc',
              fw/'Core/Src/lever_calibration.c', fw/'Core/Src/lever_storage.c', fw/'tools/mock_flash.c']
    for name in ('test_lever_calibration', 'test_lever_storage'):
        exe = scratch / (name + ('.exe' if os.name == 'nt' else ''))
        run(common + [fw/'tools'/f'{name}.c', '-o', exe])
        run([exe])
    run([sys.executable, '-m', 'unittest', 'discover', '-s', root/'host', '-p', 'test_*.py', '-v'])
    for name in ('test_usb_routes.py', 'test_save_handshake.py'):
        run([sys.executable, fw/'tools'/name, scratch])
    run([sys.executable, root/'scripts/test-game-io.py', '--gcc', args.gcc])
    run([sys.executable, root/'mechanical/verify_sources.py'])
    print('PASS: all offline checks; no USB hardware or MCU Flash accessed.')


if __name__ == '__main__':
    main()
