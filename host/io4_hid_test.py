#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IO4 HID 硬件验收工具 (Windows / Python + hidapi)

用途
----
1. 枚举 HID 设备，确认 VID / PID / Manufacturer / Product / Path
2. 解析 MCU 上报的 Report Descriptor，独立核对 Input / Output 报文长度
3. 持续读取 64-byte Input Report，按 IO4 raw layout 解析并限速显示
4. 统计实际收到的报文数量与 reports/s，确认是否接近 1 kHz
5. 通过 Interrupt OUT 发送 64-byte 测试报文，输出实际写入字节数

本工具不修改 MCU 固件，也不要求 MCU 暴露任何调试接口。
MCU 侧的内部状态请用 Keil Watch 窗口观察。

依赖
----
    pip install hidapi
"""

import argparse
import sys
import time


def _force_utf8_console():
    """
    把 stdout / stderr 固定为 UTF-8。

    真实的 Windows 控制台不受影响：Python 在控制台下走 WriteConsoleW，
    与代码页无关。这一步是为了让重定向 / 管道输出(例如 > log.txt)也是
    干净的 UTF-8 —— 否则 Python 会退回系统 ANSI 代码页(本机为 GBK)编码，
    在编辑器里读成乱码。
    """
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            try:
                stream.reconfigure(encoding="utf-8", errors="replace")
            except Exception:                      # noqa: BLE001
                pass


_force_utf8_console()

try:
    import hid
except ImportError:  # pragma: no cover
    sys.stderr.write(
        "缺少 hidapi 模块。请先安装:\n"
        "    pip install hidapi\n"
    )
    raise SystemExit(2)


# --------------------------------------------------------------------------
# 目标设备与协议常量
# --------------------------------------------------------------------------

TARGET_VID = 0x0CA3
TARGET_PID = 0x0021

REPORT_LEN = 64
IN_REPORT_ID = 0x01
OUT_REPORT_ID = 0x10

# IO4 raw layout 的字节偏移(含 Report ID)
OFF_ANALOG0 = 1     # byte 1~2     Lever / ADC0
OFF_SPINNER = 17    # byte 17~24   4 x u16   -> analog[8..11]
OFF_CHUTE = 25      # byte 25~28   2 x u16   -> analog[12..13]
OFF_BUTTONS0 = 29   # byte 29~30
OFF_BUTTONS1 = 31   # byte 31~32
OFF_SYSTEM = 33     # byte 33
OFF_USB = 34        # byte 34

DEFAULT_DISPLAY_HZ = 20.0


# --------------------------------------------------------------------------
# 小工具函数
# --------------------------------------------------------------------------

def _to_text(value):
    """hidapi 在不同平台/版本上可能返回 str 或 bytes。"""
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    return str(value)


def _u16_le(buf, off):
    """按 little-endian 读取一个 16-bit 字段。"""
    return buf[off] | (buf[off + 1] << 8)


def _hex_bytes(buf, limit=None):
    if limit is not None and len(buf) > limit:
        return " ".join("%02X" % b for b in buf[:limit]) + " ... (%d bytes)" % len(buf)
    return " ".join("%02X" % b for b in buf)


# --------------------------------------------------------------------------
# Report Descriptor 解析
# --------------------------------------------------------------------------

def parse_report_descriptor(desc):
    """
    极简 HID Report Descriptor 解析器。

    只做一件事：按 Report ID 累计 Input / Output / Feature 报文的位宽，
    用来独立核对固件声明的报文长度。

    返回 {kind: {report_id: bits}}，kind 为 'input' / 'output' / 'feature'。
    """
    totals = {"input": {}, "output": {}, "feature": {}}
    sizes = {"input": {}, "output": {}, "feature": {}}
    main_tag_to_kind = {0x8: "input", 0x9: "output", 0xB: "feature"}

    report_size = 0
    report_count = 0
    report_id = 0

    i = 0
    n = len(desc)
    while i < n:
        prefix = desc[i]
        i += 1

        bsize = prefix & 0x03
        if bsize == 3:
            bsize = 4
        btype = (prefix >> 2) & 0x03
        btag = (prefix >> 4) & 0x0F

        value = 0
        for k in range(bsize):
            if i < n:
                value |= desc[i] << (8 * k)
                i += 1

        if btype == 0x01:            # Global item
            if btag == 0x7:          # Report Size
                report_size = value
            elif btag == 0x8:        # Report ID
                report_id = value
            elif btag == 0x9:        # Report Count
                report_count = value
        elif btype == 0x00:          # Main item
            kind = main_tag_to_kind.get(btag)
            if kind is not None:
                bits = report_size * report_count
                totals[kind][report_id] = totals[kind].get(report_id, 0) + bits

    # 位 -> 字节(向上取整)
    for kind in sizes:
        for rid, bits in totals[kind].items():
            sizes[kind][rid] = (bits + 7) // 8
    return sizes


def query_windows_caps(path):
    """
    用 Windows 原生 HID API 查询报文长度 —— 这是权威来源。

    hidapi 的 get_report_descriptor() 在部分 Windows 环境下会返回陈旧数据
    (本机实测:设备已换成新版描述符,它仍返回旧的一组字节),所以绝对不能
    拿它当判据。HidP_GetCaps 是 Windows HID 解析器自己算出来的结果,主机
    真正按它收发报文,不会骗人。

    返回 (input_len, output_len)，失败时 (None, None)。
    """
    if sys.platform != "win32":
        return None, None

    import ctypes
    import ctypes.wintypes as w
    from ctypes import byref, c_ushort, c_void_p, Structure

    class HIDP_CAPS(Structure):
        _fields_ = [("Usage", c_ushort), ("UsagePage", c_ushort),
                    ("InputReportByteLength", c_ushort),
                    ("OutputReportByteLength", c_ushort),
                    ("FeatureReportByteLength", c_ushort),
                    ("Reserved", c_ushort * 17),
                    ("NumberLinkCollectionNodes", c_ushort),
                    ("NumberInputButtonCaps", c_ushort),
                    ("NumberInputValueCaps", c_ushort),
                    ("NumberInputDataIndices", c_ushort),
                    ("NumberOutputButtonCaps", c_ushort),
                    ("NumberOutputValueCaps", c_ushort),
                    ("NumberOutputDataIndices", c_ushort),
                    ("NumberFeatureButtonCaps", c_ushort),
                    ("NumberFeatureValueCaps", c_ushort),
                    ("NumberFeatureDataIndices", c_ushort)]

    try:
        hid_dll = ctypes.WinDLL("hid.dll")
        kernel32 = ctypes.WinDLL("kernel32.dll")

        CreateFileW = kernel32.CreateFileW
        CreateFileW.restype = c_void_p
        CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c_void_p,
                                w.DWORD, w.DWORD, c_void_p]

        handle = CreateFileW(path, 0x80000000 | 0x40000000, 3, None, 3, 0, None)
        if handle == c_void_p(-1).value:
            return None, None

        try:
            preparsed = c_void_p()
            if not hid_dll.HidD_GetPreparsedData(c_void_p(handle), byref(preparsed)):
                return None, None
            try:
                caps = HIDP_CAPS()
                if hid_dll.HidP_GetCaps(preparsed, byref(caps)) != 0x00110000:
                    return None, None
                return caps.InputReportByteLength, caps.OutputReportByteLength
            finally:
                hid_dll.HidD_FreePreparsedData(preparsed)
        finally:
            kernel32.CloseHandle(c_void_p(handle))
    except Exception:                              # noqa: BLE001
        return None, None


# --------------------------------------------------------------------------
# 设备枚举
# --------------------------------------------------------------------------

def enumerate_devices():
    try:
        return hid.enumerate()
    except Exception as exc:                       # noqa: BLE001
        sys.stderr.write("枚举 HID 设备失败: %s\n" % exc)
        return []


def print_device(dev_info, prefix="  "):
    print("%sVID           : 0x%04X" % (prefix, dev_info.get("vendor_id", 0)))
    print("%sPID           : 0x%04X" % (prefix, dev_info.get("product_id", 0)))
    print("%sManufacturer  : %s" % (prefix, _to_text(dev_info.get("manufacturer_string"))))
    print("%sProduct       : %s" % (prefix, _to_text(dev_info.get("product_string"))))
    print("%sSerial        : %s" % (prefix, _to_text(dev_info.get("serial_number"))))
    print("%sInterface     : %s" % (prefix, dev_info.get("interface_number", -1)))
    print("%sUsagePage/Use : 0x%04X / 0x%04X"
          % (prefix, dev_info.get("usage_page", 0), dev_info.get("usage", 0)))
    print("%sPath          : %s" % (prefix, _to_text(dev_info.get("path"))))


def find_target(devices):
    return [d for d in devices
            if d.get("vendor_id") == TARGET_VID and d.get("product_id") == TARGET_PID]


# --------------------------------------------------------------------------
# 报文解析
# --------------------------------------------------------------------------

def parse_input_report(buf, payload_only=False):
    """
    按 IO4 raw layout 解析一帧 Input Report。

    payload_only=False 时 buf[0] 是 Report ID(Windows / Linux 惯例)；
    payload_only=True 表示该平台的 HID 栈已经把 Report ID 剥掉了。
    """
    off = 0 if payload_only else 1
    return {
        "lever": _u16_le(buf, off + OFF_ANALOG0 - 1),
        "spinner": [_u16_le(buf, off + OFF_SPINNER - 1 + 2 * k) for k in range(4)],
        "chute": [_u16_le(buf, off + OFF_CHUTE - 1 + 2 * k) for k in range(2)],
        "buttons0": _u16_le(buf, off + OFF_BUTTONS0 - 1),
        "buttons1": _u16_le(buf, off + OFF_BUTTONS1 - 1),
        "system": buf[off + OFF_SYSTEM - 1],
        "usb": buf[off + OFF_USB - 1],
    }


def format_status_line(r, count, rate, malformed=0):
    line = ("lever=%5d  buttons0=0x%04X  buttons1=0x%04X  system=0x%02X  usb=0x%02X  "
            "packets=%-8d  %.1f/s"
            % (r["lever"], r["buttons0"], r["buttons1"], r["system"], r["usb"],
               count, rate))
    if malformed:
        line += "  MALFORMED=%d" % malformed
    return line


def format_status_block(r, count, rate, malformed=0):
    text = ("lever=%d\n"
            "buttons0=0x%04X\n"
            "buttons1=0x%04X\n"
            "system=0x%02X\n"
            "usb=0x%02X\n"
            "packets=%d\n"
            "reports_per_sec=%.1f"
            % (r["lever"], r["buttons0"], r["buttons1"], r["system"], r["usb"],
               count, rate))
    if malformed:
        text += "\nmalformed=%d" % malformed
    return text


# --------------------------------------------------------------------------
# 帧格式判定
# --------------------------------------------------------------------------

class ReportDecoder:
    """
    判定 Input Report 的帧格式并解析。

    判定规则是**长度优先，Report ID 次之**：

        64 bytes 且 buf[0] == 0x01   -> numbered report，含 Report ID
        63 bytes                     -> Report ID 已被 HID 栈剥离，payload-only
        64 bytes 且 buf[0] != 0x01   -> 异常帧，不做猜测
        其他长度                      -> 异常帧

    不能拿 buf[0] != 0x01 当作「ID 被剥离」的判据：payload-only 时 buf[0] 是
    lever 的低字节，完全可能碰巧等于 0x01，那样会把整帧解析错位。

    每帧都会被独立校验，所以判定之后再出现的畸形帧依然会被丢弃而不是错位解析。
    同类告警只打印一次。
    """

    def __init__(self, force_payload_only=False):
        self.payload_only = bool(force_payload_only)
        self.detected = bool(force_payload_only)   # --no-report-id 时跳过判定
        self.malformed = 0
        self._reported = set()

    def _report_once(self, key, text):
        if key not in self._reported:
            self._reported.add(key)
            print(text)

    def expected_len(self):
        return (REPORT_LEN - 1) if self.payload_only else REPORT_LEN

    def validate(self, buf):
        """
        **每帧都要跑的热路径，必须廉价。** 只做格式判定和长度校验，不解析。

        这里刻意只做整数比较：读循环每秒要跑 1000 次，任何多余的对象分配
        都会拖慢循环，进而让 Windows 的 HID 输入缓冲溢出丢帧，测出来的速率
        凭空偏低 —— 而设备对此一无所知。解析留到显示刷新时(20Hz)再做。
        """
        n = len(buf)

        if not self.detected:
            if n == (REPORT_LEN - 1):
                self.payload_only = True
                self.detected = True
                self._report_once(
                    "payload_only",
                    "\n[warn] 收到 %d-byte Input Report，判定 HID 栈已剥离 Report ID，"
                    "按 payload-only 解析。\n" % n)
            elif n == REPORT_LEN:
                if buf[0] == IN_REPORT_ID:
                    self.payload_only = False
                    self.detected = True
                else:
                    self._report_once(
                        "bad_id",
                        "\n[warn] 收到 %d-byte report，但首字节 0x%02X != Report ID 0x%02X。"
                        "\n[warn] 不擅自按 payload-only 解析，该帧按异常丢弃。"
                        "\n[warn] 原始字节: %s"
                        "\n[warn] 若持续出现，请检查描述符，或用 --no-report-id 强制指定。\n"
                        % (n, buf[0], IN_REPORT_ID, _hex_bytes(buf)))
            else:
                self._report_once(
                    "bad_len",
                    "\n[warn] 非法 Input Report 长度: %d bytes（期望 %d 或 %d）\n"
                    % (n, REPORT_LEN, REPORT_LEN - 1))

        # 逐帧校验：payload-only 必须是 63 字节；否则必须是 64 字节且首字节是 Report ID。
        if self.payload_only:
            valid = (n == (REPORT_LEN - 1))
        else:
            valid = (n == REPORT_LEN) and (buf[0] == IN_REPORT_ID)

        if not valid:
            self.malformed += 1
            return False

        return True

    def accept(self, buf):
        """
        校验并解析一帧。返回解析后的 dict；帧被丢弃时返回 None。
        用于一次性处理，不用于读循环的热路径。
        """
        if not self.validate(buf):
            return None

        return parse_input_report(buf, payload_only=self.payload_only)


# --------------------------------------------------------------------------
# Interrupt OUT 发送
# --------------------------------------------------------------------------

def build_out_report(cmd):
    """
    构造 64-byte Interrupt OUT 测试报文。

    byte 0     = 0x10   Report ID
    byte 1     = cmd    测试命令字
    byte 2~63  = 0x00
    """
    buf = bytearray(REPORT_LEN)
    buf[0] = OUT_REPORT_ID
    buf[1] = cmd & 0xFF
    return bytes(buf)


def send_out_report(dev, cmd):
    buf = build_out_report(cmd)
    print("\n[OUT] 发送 %d bytes: %s" % (len(buf), _hex_bytes(buf, limit=16)))
    try:
        written = dev.write(buf)
    except Exception as exc:                       # noqa: BLE001
        print("[OUT] 写入失败: %s" % exc)
        return None

    if written is None or written < 0:
        print("[OUT] 写入失败: %s" % dev.error())
        return None

    print("[OUT] 实际写入 %d bytes" % written)
    print("[OUT] Keil Watch 预期: output_packet_count +1, "
          "last_output_cmd=0x%02X, last_output_length=64, malformed 不增加" % cmd)
    return written


# --------------------------------------------------------------------------
# 键盘输入(仅 Windows)
# --------------------------------------------------------------------------

def read_key():
    """非阻塞读一个按键；不可用或没有按键时返回 None。"""
    try:
        import msvcrt
    except ImportError:
        return None
    if msvcrt.kbhit():
        try:
            return msvcrt.getwch()
        except Exception:                          # noqa: BLE001
            return None
    return None


# --------------------------------------------------------------------------
# 主流程
# --------------------------------------------------------------------------

def cmd_list(devices, args):
    print("=== HID 设备枚举 ===")
    print("共 %d 个 HID 设备\n" % len(devices))

    if args.all:
        for idx, d in enumerate(devices):
            print("[%d]" % idx)
            print_device(d)
            print()
        return 0

    target = find_target(devices)
    if not target:
        print("未找到 VID=0x%04X PID=0x%04X 的设备。\n" % (TARGET_VID, TARGET_PID))
        print("当前枚举到的 HID 设备:")
        for idx, d in enumerate(devices):
            print("  [%d] VID=0x%04X PID=0x%04X  %s"
                  % (idx, d.get("vendor_id", 0), d.get("product_id", 0),
                     _to_text(d.get("product_string"))))
        print("\n用 --all 查看完整信息。")
        return 1

    print("找到 %d 个匹配设备\n" % len(target))
    for d in target:
        print_device(d)
        print()
    return 0


def cmd_monitor(devices, args):
    target = find_target(devices)
    if not target:
        sys.stderr.write("未找到 VID=0x%04X PID=0x%04X 的设备。"
                         "先执行 --list 查看枚举结果。\n" % (TARGET_VID, TARGET_PID))
        return 1

    info = target[0]
    if len(target) > 1:
        print("[warn] 匹配到 %d 个设备，使用第一个。" % len(target))

    print("=== 目标设备 ===")
    print_device(info)

    dev = hid.device()
    try:
        dev.open_path(info["path"])
    except Exception as exc:                       # noqa: BLE001
        sys.stderr.write("打开设备失败: %s\n" % exc)
        return 1

    try:
        print("\n=== 报文长度核对 (Windows HidP_GetCaps, 权威) ===")
        in_len, out_len = query_windows_caps(_to_text(info["path"]))
        print("  Input  报文长度(含 Report ID): %s" % (in_len if in_len else "查询失败"))
        print("  Output 报文长度(含 Report ID): %s" % (out_len if out_len else "查询失败"))
        if in_len is not None and in_len != REPORT_LEN:
            print("  [warn] 期望 %d，实际 %d" % (REPORT_LEN, in_len))

        # 仅供人工参考：hidapi 的 get_report_descriptor() 在部分 Windows 环境
        # 下会返回陈旧数据，因此它打印的内容不参与任何判断。
        try:
            raw = dev.get_report_descriptor()
            sizes = parse_report_descriptor(raw)
            print("  (参考) hidapi 读到的描述符 %d 字节，解析 input=%s output=%s"
                  % (len(raw), sizes["input"], sizes["output"]))
            print("  (参考) 该结果可能陈旧，判据以上面的 HidP_GetCaps 为准")
        except Exception:                          # noqa: BLE001
            pass

        print("\n=== 开始接收 ===")
        print("提示: 按 s 发送测试报文, 按 q 退出\n")

        if args.send is not None:
            send_out_report(dev, args.send)
            if args.no_monitor:
                return 0

        _monitor_loop(dev, args)
    finally:
        dev.close()
    return 0


def _monitor_loop(dev, args):
    interval = 1.0 / max(1.0, args.display_hz)
    tty = sys.stdout.isatty()

    decoder = ReportDecoder(force_payload_only=args.no_report_id)

    count = 0
    window_count = 0
    loop_start = time.monotonic()
    window_start = loop_start
    rate = 0.0
    last_display = loop_start
    quit_requested = False
    last = None
    last_raw = None

    while True:
        try:
            # 每帧一次。超时取小值，保证能及时排空 1 kHz 的输入队列。
            data = dev.read(REPORT_LEN, 5)
        except Exception as exc:                   # noqa: BLE001
            print("\n读取失败: %s" % exc)
            break

        if data:
            # 热路径，每秒跑 1000 次：只做格式校验，不解析、不拷贝。
            # 这里**绝不 continue** —— 后面的显示刷新、按键处理、--duration
            # 判断必须每轮都执行，否则持续收到异常帧时会变成不刷新、退不出的循环。
            if decoder.validate(data):
                count += 1
                window_count += 1
                last_raw = data

        now = time.monotonic()

        if (now - last_display) >= interval:
            elapsed = now - window_start
            if elapsed > 0:
                rate = window_count / elapsed
            window_count = 0
            window_start = now
            last_display = now

            # 解析放在这里(20Hz)，不放在 1kHz 的每帧路径上。
            if last_raw is not None:
                last = parse_input_report(last_raw, payload_only=decoder.payload_only)

            if last is not None:
                if tty:
                    sys.stdout.write("\r"
                                     + format_status_line(last, count, rate, decoder.malformed)
                                     + "   ")
                    sys.stdout.flush()
                else:
                    sys.stdout.write(format_status_block(last, count, rate, decoder.malformed)
                                     + "\n")
                    sys.stdout.flush()

            # 按键与显示同频处理(默认 20Hz)，绝不放进每帧路径。
            # Windows 的 HID 输入缓冲有限，每帧多做一次系统调用就可能溢出丢帧，
            # 而设备对此一无所知 —— 那会让测出来的速率凭空偏低。
            key = read_key()
            if key is not None:
                if key in ("q", "Q", "\x03", "\x1b"):
                    quit_requested = True
                elif key in ("s", "S"):
                    if tty:
                        sys.stdout.write("\r" + " " * 100 + "\r")
                    send_out_report(dev, args.send if args.send is not None else 0x55)

        if quit_requested:
            break

        if args.duration is not None and (now - loop_start) >= args.duration:
            print("\n已达到 --duration %.1f s，退出。" % args.duration)
            break

    if tty:
        sys.stdout.write("\n")

    print("\n=== 统计 ===")
    print("共接收 Input Report: %d 帧" % count)
    print("丢弃的异常帧        : %d 帧" % decoder.malformed)
    print("帧格式判定          : %s"
          % ("payload-only (Report ID 已被剥离)" if decoder.payload_only
             else "numbered (含 Report ID)"))
    if last is not None:
        print("最后一帧: lever=%d buttons0=0x%04X buttons1=0x%04X system=0x%02X usb=0x%02X"
              % (last["lever"], last["buttons0"], last["buttons1"],
                 last["system"], last["usb"]))
    else:
        print("未收到任何报文。请确认设备已枚举且固件在发送 Input Report。")


def main():
    ap = argparse.ArgumentParser(
        description="IO4 HID 硬件验收工具 (VID=0x%04X PID=0x%04X)"
                    % (TARGET_VID, TARGET_PID),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "示例:\n"
            "  python io4_hid_test.py --list          仅枚举并核对设备信息\n"
            "  python io4_hid_test.py                 打开设备并持续监视\n"
            "  python io4_hid_test.py --send 0x55     打开后先发一帧测试报文再监视\n"
            "  python io4_hid_test.py --send 0x55 --no-monitor   只发一帧\n"
        ))
    ap.add_argument("--list", action="store_true",
                    help="仅枚举 HID 设备并核对目标设备信息，不打开设备")
    ap.add_argument("--all", action="store_true",
                    help="配合 --list：列出全部 HID 设备的完整信息")
    ap.add_argument("--send", type=lambda s: int(s, 0), default=None, metavar="CMD",
                    help="打开设备后发送一帧 Interrupt OUT 测试报文，命令字按 C 语法解析(如 0x55)")
    ap.add_argument("--no-monitor", action="store_true",
                    help="配合 --send：发送后立即退出，不进入监视循环")
    ap.add_argument("--display-hz", type=float, default=DEFAULT_DISPLAY_HZ,
                    help="显示刷新率，默认 %.0f Hz" % DEFAULT_DISPLAY_HZ)
    ap.add_argument("--no-report-id", action="store_true",
                    help="强制按 payload-only 解析 Input Report(默认自动判定)")
    ap.add_argument("--duration", type=float, default=None, metavar="SEC",
                    help="监视指定秒数后自动退出，默认一直运行到按 q")
    args = ap.parse_args()

    devices = enumerate_devices()

    if args.list:
        return cmd_list(devices, args)

    return cmd_monitor(devices, args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n已中断。")
        raise SystemExit(130)
