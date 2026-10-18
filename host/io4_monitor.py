"""IO4 protocol, lossless button-edge counting and single-owner HID transport."""
from collections import deque
import queue
import struct
import threading
import time

from lever_calibrate import OPS, build_command, parse_status

VID, PID = 0x0CA3, 0x0021
FLASH_COMMANDS = ('save', 'commit_save')
BUTTONS = (
    ('LA', 0, 0, '左 A', '#CE5069'), ('LB', 0, 5, '左 B', '#269D83'),
    ('LC', 0, 4, '左 C', '#397DD0'), ('LS', 1, 15, '左侧键', '#53708A'),
    ('LM', 1, 14, '左菜单', '#53708A'), ('RA', 0, 1, '右 A', '#CE5069'),
    ('RB', 1, 0, '右 B', '#269D83'), ('RC', 0, 15, '右 C', '#397DD0'),
    ('RS', 0, 14, '右侧键', '#53708A'), ('RM', 0, 13, '右菜单', '#53708A'),
)
ERRORS = {
    2: '命令格式不正确', 3: '请先开始校准', 4: '设备正在采样，请稍候',
    5: '左、中、右三个位置尚未全部采集', 6: '三个位置顺序或行程不正确，请重新采集',
    7: '中心死区过大，请减小后重新提交', 8: '采样不稳定，请固定摇杆后重新采集',
    9: '设备 ADC / DMA 不可用，请检查固件', 10: '设备操作超时，请查询当前状态',
    12: 'Flash 写入失败，未确认保存成功，请查询状态后检查设备',
    13: '没有可保存的有效校准，请先完成三点校准',
    14: '芯片或 Flash 布局不支持持久化，请检查配套固件',
    15: 'Flash 回读校验失败，未确认保存成功，请查询设备状态',
}

def decode(data):
    if len(data) == 64 and data[0] == 1:
        b = bytes(data)
    elif len(data) == 63:
        b = b'\x01' + bytes(data)
    else:
        return None
    return dict(lever=int.from_bytes(b[1:3], 'little'),
                words=struct.unpack_from('<HH', b, 29), system=b[33], usb=b[34],
                cal=parse_status(b), frame=b.hex(' ').upper())

class Monitor:
    def __init__(self):
        self.frame = None
        self.total = self.malformed = 0
        self.presses = [0] * 10
        self.down = [False] * 10
        self.seen = [False] * 10
        self.raw_window = deque(maxlen=200)
        self.minimum, self.maximum = 65535, 0
        self.last_rx = 0.0

    def ingest(self, data, now=None):
        f = decode(data)
        if f is None:
            self.malformed += 1
            return None
        self.total += 1
        self.frame = f
        self.last_rx = time.monotonic() if now is None else now
        for i, (_, word, bit, _, _) in enumerate(BUTTONS):
            down = bool(f['words'][word] & (1 << bit))
            if down and not self.down[i]:
                self.presses[i] += 1
                self.seen[i] = True
            self.down[i] = down
        self.minimum = min(self.minimum, f['lever'])
        self.maximum = max(self.maximum, f['lever'])
        if f['cal'] is not None:
            self.raw_window.append(f['cal']['raw'])
        return f

    def clear(self):
        self.presses = [0] * 10
        self.seen = [False] * 10
        self.minimum = self.frame['lever'] if self.frame else 65535
        self.maximum = self.frame['lever'] if self.frame else 0
        self.raw_window.clear()
        # Keep down[]: holding a key during reset must not create a new edge.

    def snapshot(self):
        return dict(frame=self.frame, total=self.total, malformed=self.malformed,
                    presses=self.presses[:], down=self.down[:], seen=self.seen[:],
                    minimum=self.minimum, maximum=self.maximum, last_rx=self.last_rx,
                    jitter=max(self.raw_window)-min(self.raw_window) if self.raw_window else None)

class Worker(threading.Thread):
    """One thread owns open/read/write/close. GUI consumes snapshots only."""
    def __init__(self, factory, events):
        super().__init__(daemon=True)
        self.factory, self.events = factory, events
        self.lock = threading.Lock()
        self.monitor = Monitor()
        self.requests = queue.Queue(maxsize=1)
        self.stop_flag = threading.Event()
        self.connected = False
        self.pending = None
        self.reserved = False
        self.error = ''
        self.seq = None
        self.dev = None
        self.uncertain = False

    def snapshot(self):
        with self.lock:
            s = self.monitor.snapshot()
            s.update(connected=self.connected, pending=self.reserved,
                     pending_op=self.pending['name'] if self.pending else None, error=self.error,
                     uncertain=self.uncertain)
            return s

    def submit(self, name, value=0):
        with self.lock:
            if not self.connected or self.monitor.frame is None:
                raise RuntimeError('设备未连接或尚未收到数据')
            cal = self.monitor.frame['cal']
            if cal is None:
                raise RuntimeError('当前固件没有 LC v1 校准接口，请先烧入新版固件')
            if time.monotonic() - self.monitor.last_rx > 0.6:
                raise RuntimeError('设备数据已超时，请重新连接')
            if name in FLASH_COMMANDS and not cal['flash_supported']:
                raise RuntimeError('当前设备不支持写入 Flash，请先使用配套的 Flash 持久化固件')
            if self.uncertain and name != 'status':
                raise RuntimeError('上次保存结果未知，请先查询状态或重新连接，勿直接重复保存')
            if (self.pending and self.pending['name'] in FLASH_COMMANDS) or cal['saving']:
                if name != 'status' or self.reserved:
                    raise RuntimeError('正在写入 Flash，不能取消或发送其他修改命令，请等待结果')
            if (self.reserved or cal['busy']) and name != 'cancel':
                raise RuntimeError('正在处理上一条命令，请稍候')
            # Validate before reserving or queuing anything.
            build_command(OPS[name], 0, value)
            if self.requests.full():
                raise RuntimeError('命令正在发送，请稍候再取消')
            self.reserved = True
            self.requests.put_nowait((name, value))

    def clear(self):
        with self.lock:
            self.monitor.clear()

    def _event(self, kind, **values):
        self.events.put(dict(kind=kind, **values))

    def run(self):
        try:
            self.dev = self.factory()
            with self.lock:
                self.connected = True
            self._event('connected')
            opened = time.monotonic()
            while not self.stop_flag.is_set():
                try:
                    name, value = self.requests.get_nowait()
                except queue.Empty:
                    name = None
                if name:
                    with self.lock:
                        self.seq = ((self.seq if self.seq is not None else 0) + 1) & 255
                        self.pending = dict(name=name, seq=self.seq, op=OPS[name],
                                            deadline=time.monotonic()+(15 if name in FLASH_COMMANDS else 3))
                    # Only this thread ever touches the HID handle.
                    written = self.dev.write(build_command(OPS[name], self.seq, value))
                    if written != 64:
                        raise RuntimeError('HID 写入不足 64 字节，命令未确认')
                    self._event('sent', name=name)
                data = self.dev.read(64, 10)
                now = time.monotonic()
                with self.lock:
                    f = self.monitor.ingest(data, now) if data else None
                    cal = f['cal'] if f else None
                    if self.seq is None and cal is not None:
                        self.seq = cal['seq']
                    pending = self.pending
                    if cal and pending and cal['seq'] == pending['seq'] and cal['op'] == pending['op']:
                        if cal['result'] not in (1, 11):
                            self.pending = None
                            self.reserved = not self.requests.empty()
                            error = ERRORS.get(cal['result'], '未知设备错误') if cal['result'] else ''
                            if not error and pending['name'] in FLASH_COMMANDS:
                                if not (cal['persisted'] and cal['valid'] and cal['flash_supported'] and not cal['saving']):
                                    error = '设备回执未确认 Flash 持久化成功，请查询状态核对'
                                    self.uncertain = True
                            if not error and pending['name'] == 'status':
                                self.uncertain = False
                            self._event('done', name=pending['name'], status=cal,
                                        error=error)
                    if self.pending and pending and now > pending['deadline']:
                        self.pending = None
                        self.reserved = not self.requests.empty()
                        if pending['name'] in FLASH_COMMANDS: self.uncertain = True
                        self._event('done', name=pending['name'], status=None,
                                    error='等待设备回执超时，保存结果未知。请查询状态或重新连接，勿直接重复提交')
                    flash_wait = self.pending and self.pending['name'] in FLASH_COMMANDS
                    if now - (self.monitor.last_rx or opened) > (15 if flash_wait else 3):
                        raise RuntimeError('连续 3 秒没有收到有效数据，连接已关闭')
        except Exception as exc:
            with self.lock:
                if self.pending and self.pending['name'] in FLASH_COMMANDS: self.uncertain = True
                self.error = str(exc) + ('；Flash 保存结果未知，请重连查询' if self.uncertain else '')
            self._event('error', message=self.error)
        finally:
            if self.dev is not None:
                try:
                    # Closing the host cancels a pending RAM session, never commits.
                    with self.lock:
                        f = self.monitor.frame
                        cancel = bool(f and f['cal'] and f['cal']['session'] and not f['cal']['saving']
                                      and not (self.pending and self.pending['name'] in FLASH_COMMANDS))
                    if cancel and not self.error:
                        self.dev.write(build_command(OPS['cancel'], ((self.seq or 0)+1)&255))
                        until = time.monotonic()+0.25
                        while time.monotonic() < until:
                            c = decode(self.dev.read(64, 10))
                            if c and c['cal'] and not c['cal']['session']:
                                break
                except Exception:
                    pass
                try:
                    self.dev.close()
                except Exception:
                    pass
            with self.lock:
                self.connected = self.reserved = False
                self.pending = None
            self._event('disconnected')

class DemoState:
    def __init__(self):
        self.lock = threading.Lock()
        self.position = 0.0
        self.keys = [False] * 10
        self.saved = None

class DemoDevice:
    """Explicit offline simulator, never imports hid or opens a USB device."""
    def __init__(self, state):
        self.state = state
        self.active = state.saved
        self.loaded_at_boot = self.active is not None
        self.working = [0, 0, 0]
        self.deadzone = 0
        self.session = False
        self.mask = self.point = self.seq = self.op = self.result = 0
        self.samples = []
        self.last_sample = -1
        self.closed = False
        self.saving = None
        self.save_delay = 0.15
        self.save_error = 0
        self.flash_supported = True

    def write(self, data):
        assert len(data) == 64 and data[:5] == bytes.fromhex('10 C0 4C 43 01')
        self.seq, self.op = data[5:7]
        v = int.from_bytes(data[7:9], 'little')
        self.result = 0
        if self.op == 6:
            self.session = False; self.point = self.mask = 0; self.samples = []
        elif (self.point or self.saving) and self.op != 7:
            self.result = 4
        elif self.op == 1:
            self.session = True; self.mask = 0; self.working = [0, 0, 0]
            self.samples = []; self.deadzone = self.active[3] if self.active else 0
        elif self.op == 7:
            pass
        elif self.op == 9:
            if not self.flash_supported: self.result = 14
            elif self.session: self.result = 4
            elif not self.active: self.result = 13
            else: self.begin_save(self.active)
        elif not self.session:
            self.result = 3
        elif self.op == 8:
            self.deadzone = v
        elif self.op in (5, 10):
            l, c, r = self.working
            if self.mask != 7: self.result = 5
            elif not (l < c < r or l > c > r) or min(abs(l-c), abs(r-c)) < 64: self.result = 6
            elif self.deadzone >= min(abs(l-c), abs(r-c)): self.result = 7
            elif self.op == 10:
                if not self.flash_supported: self.result = 14
                else: self.begin_save((l, c, r, self.deadzone))
            else: self.active = (l, c, r, self.deadzone); self.session = False
        elif self.op in (2, 3, 4):
            self.point = 1 << (self.op-2); self.mask &= ~self.point
            self.samples = []; self.result = 1
        return 64

    def begin_save(self, candidate):
        self.saving = (candidate, time.monotonic()+self.save_delay, self.seq, self.op)
        self.result = 11

    def read(self, size, timeout=0):
        time.sleep(0.001)
        if self.saving and time.monotonic() >= self.saving[1]:
            candidate, _, self.seq, self.op = self.saving
            self.result = self.save_error
            if not self.result:
                self.state.saved = candidate
                if self.op == 10:
                    self.active = candidate; self.session = False
            self.saving = None
        with self.state.lock:
            x = self.state.position / 100
            # A deliberately reversed potentiometer, matching the prior example.
            raw = round(2063 + (-x * 1717 if x < 0 else -x * 1751))
            keys = self.state.keys[:]
        tick = int(time.monotonic()*1000)
        if self.point and tick != self.last_sample:
            self.last_sample = tick; self.samples.append(raw)
            if len(self.samples) == 200:
                point = self.point; self.point = 0
                if max(self.samples)-min(self.samples) > 64: self.result = 8
                else:
                    self.working[{1: 0, 2: 1, 4: 2}[point]] = (sum(self.samples)+100)//200
                    self.mask |= point; self.result = 0
        lever = raw*65535//4095
        if self.active:
            l,c,r,dz = self.active
            x = raw-c; ls = c-l; rs = r-c
            if l > r: x=-x; ls=-ls; rs=-rs
            if x <= -ls: lever=0
            elif x >= rs: lever=65535
            elif -dz <= x <= dz: lever=32768
            elif x < -dz: lever=(x+ls)*32768//(ls-dz)
            else: lever=32768+(x-dz)*32767//(rs-dz)
        b = bytearray(64); b[0]=1; struct.pack_into('<H',b,1,lever)
        words=[0,0]
        for down, (_,w,bit,_,_) in zip(keys,BUTTONS):
            if down: words[w] |= 1 << bit
        struct.pack_into('<HH',b,29,*words)
        b[35:38]=b'LC\x01'; b[38:41]=bytes((self.seq,self.op,self.result))
        b[41]=(1 if self.active else 0)|(2 if self.session else 0)|(4 if self.point else 0)|(8 if self.active and self.active[0]>self.active[2] else 0)|\
              (16 if self.active and self.active==self.state.saved else 0)|(32 if self.loaded_at_boot else 0)|\
              (64 if self.saving else 0)|(128 if self.flash_supported else 0)
        b[42]=self.mask; b[43]=self.point
        a=self.active or (0,0,0,0)
        struct.pack_into('<10H',b,44,len(self.samples),raw,*a,*self.working,0)
        return bytes(b)

    def close(self):
        self.closed = True

def hardware_factory(path):
    def open_device():
        import hid
        dev = hid.device()
        dev.open_path(path)
        return dev
    return open_device

def enumerate_hardware():
    import hid
    return hid.enumerate(VID, PID)
