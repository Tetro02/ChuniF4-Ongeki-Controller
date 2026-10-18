"""Unit tests of GUI state logic with fake widgets; no native window/input."""
import unittest
from io4_hid_gui import App

class Widget:
    def __init__(self):self.state=None
    def configure(self,**values):self.__dict__.update(values)

class Mode:
    def get(self):return '真实设备'

class FakeWorker:
    def __init__(self):self.data=dict(pending_op=None,uncertain=False)
    def is_alive(self):return True
    def snapshot(self):return self.data.copy()

class Controls(unittest.TestCase):
    def setUp(self):
        self.app=App.__new__(App);self.app.worker=FakeWorker();self.app.scanning=False;self.app.mode=Mode()
        self.app.capture_btns={k:Widget() for k in ('left','center','right')}
        for name in ('connect_btn','disconnect_btn','scan_btn','device_combo','mode_combo','start_btn',
                     'dz_spin','dz_btn','commit_btn','commit_save_btn','save_btn','query_btn','cancel_btn'):
            setattr(self.app,name,Widget())
        self.cal=dict(busy=False,saving=False,flash_supported=True,valid=True,session=False,
                      captured=7,persisted=False)
    def sync(self,connected=True,pending=False):self.app._sync_controls(connected,self.cal,pending)
    def test_completed_draft_enables_atomic_save(self):
        self.cal.update(session=True,valid=False);self.sync()
        self.assertEqual(self.app.commit_save_btn.state,'normal')
        self.assertEqual(self.app.commit_btn.state,'normal');self.assertEqual(self.app.save_btn.state,'disabled')
    def test_ram_config_enables_save_current(self):
        self.sync();self.assertEqual(self.app.save_btn.state,'normal')
        self.assertEqual(self.app.commit_save_btn.state,'disabled')
    def test_saved_config_disables_redundant_save(self):
        self.cal['persisted']=True;self.sync();self.assertEqual(self.app.save_btn.state,'disabled')
    def test_old_firmware_retains_only_ram_path(self):
        self.cal.update(session=True,flash_supported=False);self.sync()
        self.assertEqual(self.app.commit_save_btn.state,'disabled')
        self.assertEqual(self.app.save_btn.state,'disabled');self.assertEqual(self.app.commit_btn.state,'normal')
    def test_flash_pending_locks_disconnect_cancel_and_writes(self):
        self.app.worker.data['pending_op']='commit_save';self.sync(pending=True)
        for name in ('disconnect_btn','cancel_btn','save_btn','commit_btn','commit_save_btn','start_btn','query_btn'):
            self.assertEqual(getattr(self.app,name).state,'disabled',name)
    def test_external_save_allows_status_query(self):
        self.cal['saving']=True;self.sync()
        self.assertEqual(self.app.query_btn.state,'normal');self.assertEqual(self.app.save_btn.state,'disabled')
        self.assertEqual(self.app.cancel_btn.state,'disabled')
    def test_uncertain_result_requires_query(self):
        self.app.worker.data['uncertain']=True;self.sync()
        self.assertEqual(self.app.save_btn.state,'disabled');self.assertEqual(self.app.query_btn.state,'normal')
        self.assertEqual(self.app.start_btn.state,'disabled')
    def test_stale_data_disables_saves(self):
        self.sync(connected=False)
        self.assertEqual(self.app.save_btn.state,'disabled');self.assertEqual(self.app.query_btn.state,'disabled')

if __name__=='__main__':unittest.main(verbosity=2)
