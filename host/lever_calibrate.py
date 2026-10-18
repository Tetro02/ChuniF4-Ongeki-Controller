#!/usr/bin/env python3
"""IO4 lever calibration host: python lever_calibrate.py wizard --deadzone 0.
Requires pip install hidapi. Wizard durably saves via firmware; no firmware upload.
"""
import argparse
import json
import struct
import time

OPS = dict(begin=1, left=2, center=3, right=4, commit=5, cancel=6, status=7,
           deadzone=8, save=9, commit_save=10)
ERRORS = ('ok', 'sampling', 'bad command', 'no session', 'busy', 'incomplete',
          'bad anchors/minimum span', 'invalid deadzone', 'unstable: hold still',
          'ADC/DMA error', 'operation timed out', 'saving to Flash', 'Flash operation failed',
          'no valid calibration', 'MCU/Flash layout not supported', 'Flash readback verification failed')

def build_command(op, seq, value=0):
    if not 1 <= op <= 10 or not 0 <= seq <= 255 or not 0 <= value < 4095:
        raise ValueError('invalid op, sequence or value')
    if op != 8 and value:
        raise ValueError('only deadzone accepts a parameter')
    return bytes((0x10, 0xC0, 0x4C, 0x43, 1, seq, op)) + struct.pack('<H', value) + bytes(55)

def parse_status(data):
    # Length determines framing; raw ADC low byte can also be 0x01.
    if len(data) == 64 and data[0] == 1:
        s = bytes(data[35:64])
    elif len(data) == 63:
        s = bytes(data[34:63])
    else:
        return None
    if s[:3] != b'LC\x01':
        return None
    values = struct.unpack_from('<10H', s, 9)
    return dict(seq=s[3], op=s[4], result=s[5], valid=bool(s[6] & 1),
                session=bool(s[6] & 2), busy=bool(s[6] & 4), invert=bool(s[6] & 8),
                persisted=bool(s[6] & 16), loaded_at_boot=bool(s[6] & 32),
                saving=bool(s[6] & 64), flash_supported=bool(s[6] & 128),
                captured=s[7], point=s[8], samples=values[0], raw=values[1],
                left=values[2], center=values[3], right=values[4], deadzone=values[5],
                pending_left=values[6], pending_center=values[7], pending_right=values[8],
                queue_drops=values[9])

class Client:
    def __init__(self, dev):
        self.dev = dev
        self.seq = 0
        # Drain buffered reports before choosing a new sequence. One writer only.
        latest = None
        dev.set_nonblocking(1)
        try:
            for _ in range(2048):
                data = dev.read(64)
                if not data:
                    break
                latest = parse_status(data) or latest
        finally:
            dev.set_nonblocking(0)
        if latest is None:
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline and latest is None:
                latest = parse_status(dev.read(64, 50))
        if latest is None:
            raise RuntimeError('No LC v1 telemetry: check firmware and device selection')
        self.seq = latest['seq']

    def command(self, name, value=0):
        self.seq = (self.seq + 1) & 255
        op = OPS[name]
        report = build_command(op, self.seq, value)
        if self.dev.write(report) != 64:
            raise RuntimeError('Short HID write; no command was confirmed')
        deadline = time.monotonic() + (15 if name in ('save', 'commit_save') else 3)
        while time.monotonic() < deadline:
            s = parse_status(self.dev.read(64, 50))
            if not s or s['seq'] != self.seq or s['op'] != op:
                continue
            if s['result'] in (1, 11):
                continue
            if s['result']:
                reason = ERRORS[s['result']] if s['result'] < len(ERRORS) else str(s['result'])
                raise RuntimeError(reason + '; ' + json.dumps(s))
            if name in ('save', 'commit_save') and not s['persisted']:
                raise RuntimeError('Completion ACK did not confirm durable Flash state')
            return s
        raise RuntimeError('No matching completion ACK. Do not retry automatically; query status.')

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command', choices=['wizard'] + list(OPS), nargs='?', default='wizard')
    ap.add_argument('--deadzone', type=int, default=0, help='ADC counts on EACH side (wizard)')
    ap.add_argument('--value', type=int, default=0, help='deadzone command parameter')
    ap.add_argument('--serial', help='exact serial when more than one device is connected')
    args = ap.parse_args()
    if not 0 <= args.deadzone < 4095 or not 0 <= args.value < 4095:
        ap.error('deadzone/value must be 0..4094')
    import hid
    devices = [d for d in hid.enumerate(0x0CA3, 0x0021)
               if args.serial is None or d.get('serial_number') == args.serial]
    if len(devices) != 1:
        ap.error('Expected exactly one IO4 device; found %d. Use --serial if needed.' % len(devices))
    dev = hid.device()
    dev.open_path(devices[0]['path'])
    client = None
    try:
        client = Client(dev)
        if args.command != 'wizard':
            print(json.dumps(client.command(args.command, args.value), indent=2))
            return
        client.command('begin')
        client.command('deadzone', args.deadzone)
        for name in ('left', 'center', 'right'):
            input('Hold lever at physical %s, press Enter, then keep still for 200 ms: ' % name)
            print(json.dumps(client.command(name), indent=2))
        print(json.dumps(client.command('commit_save'), indent=2))
        print('Calibration verified in Flash and applied. It reloads automatically after power-on.')
    except (KeyboardInterrupt, Exception):
        if client is not None and args.command == 'wizard':
            try:
                client.command('cancel')
            except Exception:
                pass
        raise
    finally:
        dev.close()

if __name__ == '__main__':
    main()
