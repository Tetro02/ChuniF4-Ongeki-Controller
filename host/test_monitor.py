import queue
import struct
import time
import unittest
from io4_monitor import BUTTONS, Monitor, decode, DemoState, DemoDevice, Worker
from lever_calibrate import build_command
from io4_monitor import FLASH_COMMANDS

def report(words=(0,0),lever=1,extension=False):
    b=bytearray(64);b[0]=1;struct.pack_into('<H',b,1,lever);struct.pack_into('<HH',b,29,*words)
    if extension:b[35:38]=b'LC\x01'
    return bytes(b)

def wait_until(fn, timeout=2):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.003)
    raise AssertionError('timed out')

class ProtocolTests(unittest.TestCase):
    def test_all_ten_button_bits(self):
        m=Monitor();m.ingest(report())
        for i,(_,word,bit,_,_) in enumerate(BUTTONS):
            words=[0,0];words[word]=1<<bit
            m.ingest(report(words));self.assertEqual([j for j,v in enumerate(m.down) if v],[i])
            m.ingest(report());m.ingest(report(words));m.ingest(report())
        self.assertEqual(m.presses,[2]*10);self.assertTrue(all(m.seen))
    def test_chords_and_clear_held(self):
        words=[0,0]
        for _,w,b,_,_ in BUTTONS:words[w]|=1<<b
        m=Monitor();m.ingest(report(words));self.assertTrue(all(m.down))
        m.ingest(report(words));self.assertEqual(m.presses,[1]*10)
        m.clear();m.ingest(report(words));self.assertEqual(m.presses,[0]*10)
        m.ingest(report());m.ingest(report(words));self.assertEqual(m.presses,[1]*10)
    def test_payload_and_legacy(self):
        b=report(lever=0x0101)
        self.assertEqual(decode(b),decode(b[1:]))
        self.assertIsNone(decode(b)['cal'])
        self.assertIsNone(decode(bytes(64)));self.assertIsNone(decode(b[:62]))
        self.assertIsNotNone(decode(report(extension=True))['cal'])
    def test_short_tap_between_ui_refreshes(self):
        m=Monitor();m.ingest(report());m.ingest(report((1,0)));m.ingest(report())
        self.assertFalse(m.snapshot()['down'][0]);self.assertEqual(m.snapshot()['presses'][0],1)

class WorkerTests(unittest.TestCase):
    def setUp(self):
        self.state=DemoState();self.device=DemoDevice(self.state);self.events=queue.Queue()
        self.worker=Worker(lambda:self.device,self.events);self.worker.start()
        wait_until(lambda:self.worker.snapshot()['frame'])
    def tearDown(self):
        self.worker.stop_flag.set();self.worker.join(1)
        self.assertFalse(self.worker.is_alive());self.assertTrue(self.device.closed)
    def command(self,name,value=0):
        self.worker.submit(name,value)
        return self.wait_done(name)
    def wait_done(self,name):
        end=time.monotonic()+5
        while time.monotonic()<end:
            e=self.events.get(timeout=5)
            if e['kind']=='done' and e['name']==name:return e
        self.fail('missing completion')
    def draft(self,scale=100):
        self.assertFalse(self.command('begin')['error'])
        self.assertFalse(self.command('deadzone',8)['error'])
        for name,x in [('left',-scale),('center',0),('right',scale)]:
            with self.state.lock:self.state.position=x
            self.assertFalse(self.command(name)['error'])
    def test_full_reverse_calibration(self):
        self.assertFalse(self.command('begin')['error'])
        self.assertFalse(self.command('deadzone',8)['error'])
        for name,x in [('left',-100),('center',0),('right',100)]:
            with self.state.lock:self.state.position=x
            self.assertFalse(self.command(name)['error'])
        result=self.command('commit');self.assertFalse(result['error']);self.assertTrue(result['status']['invert'])
        for x,value in [(-100,0),(0,32768),(100,65535)]:
            with self.state.lock:self.state.position=x
            wait_until(lambda:self.worker.snapshot()['frame']['lever']==value)
    def test_busy_and_cancel(self):
        self.command('begin');self.worker.submit('left')
        with self.assertRaises(RuntimeError):self.worker.submit('right')
        wait_until(lambda:self.worker.snapshot()['frame']['cal']['busy'])
        result=self.command('cancel');self.assertFalse(result['error']);self.assertFalse(result['status']['session'])
    def test_commit_failure_preserves_old(self):
        self.command('begin');e=self.command('commit')
        self.assertTrue(e['error']);self.assertFalse(e['status']['valid'])
    def test_short_write_disconnects(self):
        self.device.write=lambda data:2
        self.worker.submit('begin');wait_until(lambda:not self.worker.is_alive())
        self.assertIn('64',self.worker.snapshot()['error'])
    def test_legacy_read_only(self):
        original=self.device.read
        def legacy(*args):
            b=bytearray(original(*args));b[35:]=bytes(29);return bytes(b)
        self.device.read=legacy
        wait_until(lambda:self.worker.snapshot()['frame']['cal'] is None)
        with self.assertRaisesRegex(RuntimeError,'LC v1'):self.worker.submit('begin')
    def test_device_exception(self):
        def broken(*args):raise OSError('device removed')
        self.device.read=broken
        wait_until(lambda:not self.worker.is_alive())
        self.assertIn('device removed',self.worker.snapshot()['error'])
    def test_commit_save_and_reboot(self):
        self.draft()
        start=time.monotonic();result=self.command('commit_save')
        self.assertFalse(result['error']);self.assertTrue(result['status']['persisted'])
        self.assertGreaterEqual(time.monotonic()-start,self.device.save_delay)
        self.assertFalse(result['status']['saving']);self.assertFalse(result['status']['session'])
        reboot=DemoDevice(self.state);cal=decode(reboot.read(64))['cal']
        self.assertTrue(cal['loaded_at_boot'] and cal['persisted'] and cal['invert'])
        reboot.close()
    def test_save_ram_configuration(self):
        self.draft();self.command('commit')
        self.assertFalse(self.worker.snapshot()['frame']['cal']['persisted'])
        result=self.command('save');self.assertFalse(result['error']);self.assertTrue(result['status']['persisted'])
    def test_flash_verify_error_preserves_old(self):
        self.draft();self.command('commit_save');old=self.device.active
        self.draft(scale=80);self.device.save_error=15
        result=self.command('commit_save')
        self.assertIn('校验失败',result['error']);self.assertEqual(self.device.active,old)
        self.assertEqual(self.state.saved,old);self.assertTrue(result['status']['session'])
    def test_missing_durable_flag_is_not_success(self):
        self.draft();original=self.device.read
        def no_persist(*args):
            b=bytearray(original(*args));b[41]&=~16;return bytes(b)
        self.device.read=no_persist
        result=self.command('commit_save')
        self.assertTrue(result['error']);self.assertTrue(self.worker.snapshot()['uncertain'])
        with self.assertRaisesRegex(RuntimeError,'未知'):self.worker.submit('save')
        self.assertFalse(self.command('status')['error']);self.assertFalse(self.worker.snapshot()['uncertain'])
    def test_flash_busy_rejects_cancel_and_other_writes(self):
        self.draft();self.device.save_delay=.25;self.worker.submit('commit_save')
        wait_until(lambda:self.worker.snapshot()['frame']['cal']['saving'])
        for name in ('cancel','begin','save','right'):
            with self.assertRaisesRegex(RuntimeError,'Flash'):self.worker.submit(name)
        self.assertFalse(self.wait_done('commit_save')['error'])
    def test_flash_pause_longer_than_monitor_timeout(self):
        self.draft();self.command('commit');self.device.save_delay=3.15
        original=self.device.read
        def pause(*args):
            if self.device.saving and time.monotonic()<self.device.saving[1]:
                time.sleep(.008);return []
            return original(*args)
        self.device.read=pause
        result=self.command('save')
        self.assertFalse(result['error']);self.assertTrue(self.worker.is_alive())
        self.assertTrue(result['status']['persisted'])
    def test_old_ram_firmware_cannot_save(self):
        self.device.flash_supported=False
        wait_until(lambda:not self.worker.snapshot()['frame']['cal']['flash_supported'])
        for name in FLASH_COMMANDS:
            with self.assertRaisesRegex(RuntimeError,'配套'):self.worker.submit(name)
        self.assertIsNone(self.state.saved)
    def test_flash_timeout_does_not_retry(self):
        self.draft();self.device.save_delay=2;self.worker.submit('commit_save')
        wait_until(lambda:self.worker.snapshot()['frame']['cal']['saving'])
        with self.worker.lock:self.worker.pending['deadline']=time.monotonic()-.1
        result=self.wait_done('commit_save')
        self.assertIn('未知',result['error']);self.assertTrue(self.worker.snapshot()['uncertain'])
        with self.assertRaises(RuntimeError):self.worker.submit('commit_save')
    def test_disconnect_during_save_reports_unknown(self):
        self.draft();original=self.device.read
        def remove(*args):
            if self.device.saving:raise OSError('device removed during save')
            return original(*args)
        self.device.read=remove;self.worker.submit('commit_save')
        wait_until(lambda:not self.worker.is_alive())
        self.assertIn('保存结果未知',self.worker.snapshot()['error'])

if __name__=='__main__':unittest.main(verbosity=2)
