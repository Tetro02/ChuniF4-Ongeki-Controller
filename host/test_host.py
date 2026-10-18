import unittest
from lever_calibrate import build_command, parse_status, Client

class Device:
    def __init__(self):
        self.s = bytearray(64)
        self.s[0] = 1
        self.s[35:38] = b'LC\x01'
        self.s[38] = 253
        self.initial = True
        self.pending = False
        self.waiting = False
    def set_nonblocking(self, value):
        pass
    def read(self, length, timeout=0):
        if self.initial:
            self.initial = False
            return bytes(self.s)
        if self.pending:
            self.pending = False
            self.waiting = True
            self.s[40] = 11 if self.s[39] in (9,10) else 1
            return bytes(self.s)
        if self.waiting:
            self.waiting = False
            self.s[40] = 0
            if self.s[39] in (9,10): self.s[41] = 0x91
            return bytes(self.s)
        return []
    def write(self, report):
        self.s[38] = report[5]
        self.s[39] = report[6]
        self.pending = True
        return len(report)

class Tests(unittest.TestCase):
    def test_packet(self):
        self.assertEqual(build_command(8, 7, 20), bytes.fromhex('10 C0 4C 43 01 07 08 14 00')+bytes(55))
        with self.assertRaises(ValueError): build_command(1, 2, 3)
        with self.assertRaises(ValueError): build_command(8, 2, 4095)
        self.assertEqual(build_command(9, 7)[:9], bytes.fromhex('10 C0 4C 43 01 07 09 00 00'))
        self.assertEqual(build_command(10, 8)[:9], bytes.fromhex('10 C0 4C 43 01 08 0A 00 00'))
    def test_layout(self):
        data=bytearray(64); data[0]=1; data[35:38]=b'LC\x01'
        data[41]=15; data[46:48]=(2063).to_bytes(2,'little')
        data[62:64]=(7).to_bytes(2,'little')
        s=parse_status(data)
        self.assertEqual(s['raw'],2063); self.assertEqual(s['queue_drops'],7)
        self.assertTrue(s['invert']); self.assertEqual(s,parse_status(data[1:]))
        data[41]=255
        s=parse_status(data)
        self.assertTrue(s['persisted'] and s['loaded_at_boot'] and s['saving'] and s['flash_supported'])
        self.assertIsNone(parse_status(bytes(64)))
        self.assertIsNone(parse_status(bytes(10)))
    def test_ack_and_wrap(self):
        client=Client(Device())
        self.assertEqual(client.command('begin')['seq'],254)
        self.assertEqual(client.command('left')['seq'],255)
        self.assertEqual(client.command('center')['seq'],0)
    def test_flash_saving_and_durable_ack(self):
        client=Client(Device())
        self.assertTrue(client.command('save')['persisted'])
        self.assertTrue(client.command('commit_save')['persisted'])
    def test_flash_missing_durable_flag(self):
        class NoFlash(Device):
            def read(self,*args):
                data=super().read(*args)
                if data:
                    data=bytearray(data);data[41]=0
                return data
        with self.assertRaisesRegex(RuntimeError,'durable Flash'):
            Client(NoFlash()).command('save')

if __name__=='__main__': unittest.main()
