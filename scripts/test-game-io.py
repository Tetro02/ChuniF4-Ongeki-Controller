#!/usr/bin/env python3
"""Replay 16-bit lever values and ten buttons; no HID device is opened."""
from pathlib import Path
import argparse
import ctypes as c
import os
import subprocess
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--library', type=Path, help='Test an actual built Windows DLL')
    ap.add_argument('--gcc', default='gcc')
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    library = args.library
    if library is None:
        # Compile the actual decoder body with portable types for Linux CI.
        text = (root/'game-io/chunif4_mu3io.c').read_text(encoding='utf-8')
        start = text.index('API BOOL chunif4_decode_report(')
        end = text.index('\n}\n', start) + 3
        helper = text[text.index('static uint16_t u16('):text.index('\n', text.index('static uint16_t u16('))]
        scratch = root/'build/test-native'
        scratch.mkdir(parents=True, exist_ok=True)
        source = scratch/'game_decoder.c'
        library = scratch/('game_decoder.dll' if os.name == 'nt' else 'game_decoder.so')
        prefix = '#include <stdint.h>\n#define TRUE 1\n#define FALSE 0\ntypedef int BOOL;\n'
        prefix += '#define API __declspec(dllexport)\n' if os.name == 'nt' else '#define API\n'
        source.write_text(prefix + helper + '\n' + text[start:end], encoding='utf-8')
        flags = [] if os.name == 'nt' else ['-fPIC']
        subprocess.run([args.gcc, '-O2', '-Wall', '-Wextra', '-Werror', '-shared', *flags,
                        str(source), '-o', str(library)], check=True)
    lib = c.CDLL(str(library.resolve()))
    u8, i16 = c.c_uint8, c.c_int16
    decode = lib.chunif4_decode_report
    decode.argtypes = [c.POINTER(u8), c.c_uint32, c.POINTER(u8), c.POINTER(u8), c.POINTER(i16)]
    decode.restype = c.c_int
    report = (u8 * 64)()
    report[0] = 1
    left, right, lever = u8(), u8(), i16()
    for raw in range(65536):
        report[1], report[2] = raw & 255, raw >> 8
        assert decode(report, 64, c.byref(left), c.byref(right), c.byref(lever))
        assert lever.value == raw - 32768, raw
        assert ((32767 - lever.value) & 65535) == 65535 - raw
    vectors = [(0, 0, 0, 0), (1, 0, 1, 0), (32, 0, 2, 0), (16, 0, 4, 0),
               (0, 32768, 8, 0), (0, 16384, 16, 0), (2, 0, 0, 1),
               (0, 1, 0, 2), (32768, 0, 0, 4), (16384, 0, 0, 8),
               (8192, 0, 0, 16), (57395, 49153, 31, 31)]
    for b0, b1, l, r in vectors:
        report[29], report[30], report[31], report[32] = b0 & 255, b0 >> 8, b1 & 255, b1 >> 8
        assert decode(report, 64, c.byref(left), c.byref(right), c.byref(lever))
        assert (left.value, right.value) == (l, r)
    for size in (0, 35, 63, 65):
        assert not decode(report, size, c.byref(left), c.byref(right), c.byref(lever))
    report[0] = 16
    assert not decode(report, 64, c.byref(left), c.byref(right), c.byref(lever))
    assert not decode(None, 64, c.byref(left), c.byref(right), c.byref(lever))
    report[0] = 1
    for values in [(None, c.byref(right), c.byref(lever)),
                   (c.byref(left), None, c.byref(lever)), (c.byref(left), c.byref(right), None)]:
        assert not decode(report, 64, *values)
    print('PASS: 65536 lever values, ten independent buttons, combined input and invalid frames.')


if __name__ == '__main__':
    main()
