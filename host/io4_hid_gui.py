#!/usr/bin/env python3
"""音击控制器测试台。--demo 为完全离线演示，不会打开 HID。"""
import argparse
from collections import deque
import json
from pathlib import Path
import queue
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog

from io4_monitor import BUTTONS, FLASH_COMMANDS, Worker, DemoState, DemoDevice, hardware_factory, enumerate_hardware

BG='#EFF3F8'; INK='#18324F'; MUTED='#667B91'; BLUE='#296CCB'
LINE='#D9E2ED'; WHITE='#FFFFFF'; GREEN='#23816C'; ORANGE='#A66717'; RED='#BE455A'
FONT='Microsoft YaHei UI'
NAMES=dict(begin='开始校准',left='记录左端',center='记录中心',right='记录右端',
           commit='仅应用到 RAM',cancel='取消校准',status='查询状态',deadzone='设置中心死区',
           save='保存当前配置到 Flash',commit_save='保存并应用校准')

class KeyTile(tk.Canvas):
    def __init__(self, master, entry, toggle):
        self.factor=float(master.tk.call('tk','scaling'))/(96/72)
        super().__init__(master, height=round(94*self.factor), bg=WHITE, highlightthickness=0)
        self.entry=entry; self.down=False; self.count=0; self.seen=False; self.fresh=False
        self.bind('<Configure>',lambda e:self.draw())
        self.bind('<Button-1>',lambda e:toggle())
    def update_value(self, down, count, seen, fresh):
        state=(down,count,seen,fresh)
        if state != (self.down,self.count,self.seen,self.fresh):
            self.down,self.count,self.seen,self.fresh=state; self.draw()
    def draw(self):
        self.delete('all'); w=max(60,self.winfo_width()/self.factor); color=self.entry[4]
        on=self.down and self.fresh
        self.create_rectangle(1,1,w-1,93,fill=color if on else WHITE,
                              outline=color if self.fresh else LINE,width=2 if on else 1)
        self.create_rectangle(1,1,w-1,5,fill=color,outline='')
        self.create_text(12,24,text=self.entry[0],anchor='w',font=(FONT,14,'bold'),fill=WHITE if on else INK)
        self.create_text(w-12,24,text='●' if on else ('✓' if self.seen else '○'),anchor='e',
                         fill=WHITE if on else (GREEN if self.seen else MUTED),font=(FONT,12))
        self.create_text(12,52,text=self.entry[3],anchor='w',fill=WHITE if on else MUTED,font=(FONT,9))
        label=('按下' if on else '松开') if self.fresh else '无实时数据'
        self.create_text(12,76,text=f'{label}  ·  {self.count} 次',anchor='w',fill=WHITE if on else MUTED,font=(FONT,9))
        self.scale('all',0,0,self.factor,self.factor)

class App:
    def __init__(self, root, demo=False):
        self.root=root; self.worker=None; self.events=queue.Queue(); self.devices=[]
        self.demo_state=DemoState(); self.demo=demo; self.scanning=False; self.closed=False
        self.trace=deque(maxlen=250); self.last_frame=None; self.last_plot=0
        self.last_rate_time=time.monotonic(); self.last_count=0; self.rate=0
        self.start_deadzone=None; self.logs=deque(maxlen=100); self.last_snapshot=None
        self.mode=tk.StringVar(value='演示模式' if demo else '真实设备')
        self.selected=tk.StringVar(); self.deadzone=tk.StringVar(value='0')
        self.message=tk.StringVar(value='完成三点采集后点击“保存并应用”，写入 Flash 后可断电恢复。')
        self.flash_progress=False
        root.title('音击控制器测试台 · Flash版' + (' · 演示模式' if demo else ''))
        root.configure(bg=BG)
        self.factor=float(root.tk.call('tk','scaling'))/(96/72)
        width=min(round(1160*self.factor),root.winfo_screenwidth()-round(60*self.factor))
        height=min(round(860*self.factor),root.winfo_screenheight()-round(85*self.factor))
        root.geometry(f'{width}x{height}'); root.minsize(min(width,round(980*self.factor)),min(height,round(740*self.factor)))
        root.protocol('WM_DELETE_WINDOW',self.close)
        self._style(); self._build(); self._sync_controls(False,None,False)
        if demo: self.connect()
        else: self.scan()
        root.after(33,self.tick)

    def _style(self):
        style=ttk.Style(self.root); style.theme_use('clam')
        style.configure('.',font=(FONT,10))
        style.configure('TButton',padding=(12,7),background=WHITE,foreground=INK,bordercolor=LINE)
        style.map('TButton',background=[('active','#E6EEF9')],foreground=[('disabled','#9AAABB')])
        style.configure('Accent.TButton',background=BLUE,foreground=WHITE,bordercolor=BLUE)
        style.map('Accent.TButton',background=[('active','#225AA7'),('disabled','#D7E2F0')],foreground=[('disabled','#7E91A7')])
        style.configure('TCombobox',padding=5)
        style.configure('Horizontal.TProgressbar',background=BLUE,troughcolor='#E7EEF7',borderwidth=0)

    def label(self,parent,text='',size=10,color=INK,bold=False,**kw):
        return tk.Label(parent,text=text,bg=parent.cget('bg'),fg=color,
                        font=(FONT,size,'bold' if bold else 'normal'),**kw)

    def _build(self):
        host=tk.Frame(self.root,bg=BG);host.pack(fill='both',expand=True)
        page=tk.Canvas(host,bg=BG,highlightthickness=0)
        scrollbar=ttk.Scrollbar(host,orient='vertical',command=page.yview)
        scrollbar.pack(side='right',fill='y');page.pack(side='left',fill='both',expand=True)
        page.configure(yscrollcommand=scrollbar.set)
        outer=tk.Frame(page,bg=BG,padx=24,pady=14)
        page_window=page.create_window((0,0),window=outer,anchor='nw')
        def fit_page(event=None):
            page.itemconfigure(page_window,width=page.winfo_width(),height=max(page.winfo_height(),outer.winfo_reqheight()))
            page.configure(scrollregion=page.bbox('all'))
        page.bind('<Configure>',fit_page);outer.bind('<Configure>',fit_page)
        def scroll_page(event):
            if isinstance(event.widget,(tk.Scale,ttk.Spinbox,ttk.Combobox)):return
            page.yview_scroll(int(-event.delta/120)*3,'units')
        self.root.bind('<MouseWheel>',scroll_page)
        header=tk.Frame(outer,bg=BG); header.pack(fill='x')
        self.label(header,'音击控制器测试台',22,bold=True).pack(side='left')
        self.connection_label=self.label(header,'未连接',11,color=MUTED,bold=True)
        self.connection_label.pack(side='right')
        self.label(outer,'检查十键输入、观察摇杆位置，并完成三点校准。',10,color=MUTED,anchor='w').pack(fill='x',pady=(3,14))
        conn=tk.Frame(outer,bg=BG); conn.pack(fill='x')
        self.mode_combo=ttk.Combobox(conn,textvariable=self.mode,values=['真实设备','演示模式'],state='readonly',width=9)
        self.mode_combo.pack(side='left'); self.mode_combo.bind('<<ComboboxSelected>>',lambda e:self.mode_changed())
        self.device_combo=ttk.Combobox(conn,textvariable=self.selected,state='readonly',width=52)
        self.device_combo.pack(side='left',fill='x',expand=True,padx=8)
        self.scan_btn=ttk.Button(conn,text='刷新设备',command=self.scan); self.scan_btn.pack(side='left')
        self.connect_btn=ttk.Button(conn,text='连接',style='Accent.TButton',command=self.connect); self.connect_btn.pack(side='left',padx=(8,0))
        self.disconnect_btn=ttk.Button(conn,text='断开',command=self.disconnect); self.disconnect_btn.pack(side='left',padx=(8,0))
        self.banner=self.label(outer,'',color=MUTED,anchor='w'); self.banner.pack(fill='x',pady=(9,13))

        middle=tk.Frame(outer,bg=BG); middle.pack(fill='both',expand=True)
        middle.columnconfigure(0,weight=1); middle.columnconfigure(1,weight=0,minsize=round(342*self.factor)); middle.rowconfigure(0,weight=1)
        lever=tk.Frame(middle,bg=WHITE,padx=18,pady=14); lever.grid(row=0,column=0,sticky='nsew',padx=(0,14))
        self.label(lever,'摇杆检测',13,bold=True).pack(anchor='w')
        metrics=tk.Frame(lever,bg=WHITE); metrics.pack(fill='x',pady=(9,2))
        a=tk.Frame(metrics,bg=WHITE); a.pack(side='left',fill='x',expand=True)
        self.label(a,'IO4 输出位置',9,color=MUTED).pack(anchor='w')
        self.lever_value=self.label(a,'—',29,bold=True); self.lever_value.pack(anchor='w')
        b=tk.Frame(metrics,bg=WHITE); b.pack(side='left',padx=(20,0))
        self.label(b,'原始 ADC',9,color=MUTED).pack(anchor='w')
        self.adc_value=self.label(b,'—',21,bold=True); self.adc_value.pack(anchor='w')
        self.lever_meta=self.label(lever,'等待设备数据',9,color=MUTED,anchor='w'); self.lever_meta.pack(fill='x',pady=(0,6))
        self.bar=tk.Canvas(lever,bg=WHITE,height=round(58*self.factor),highlightthickness=0); self.bar.pack(fill='x')
        self.bar.bind('<Configure>',lambda e:self.draw_lever())
        self.jitter_label=self.label(lever,'最近 200 帧原始 ADC 波动：—',9,color=MUTED,anchor='w'); self.jitter_label.pack(fill='x',pady=(4,6))
        self.chart=tk.Canvas(lever,bg=WHITE,height=round(105*self.factor),highlightthickness=0); self.chart.pack(fill='both',expand=True)
        self.chart.bind('<Configure>',lambda e:self.draw_trace())

        cal=tk.Frame(middle,bg=WHITE,padx=17,pady=14); cal.grid(row=0,column=1,sticky='nsew')
        self.label(cal,'三点校准',13,bold=True).pack(anchor='w')
        self.cal_state=self.label(cal,'等待连接',10,color=MUTED,anchor='w'); self.cal_state.pack(fill='x',pady=(5,9))
        dz=tk.Frame(cal,bg=WHITE); dz.pack(fill='x')
        self.label(dz,'中心死区',10).pack(side='left')
        self.dz_spin=ttk.Spinbox(dz,from_=0,to=4094,textvariable=self.deadzone,width=5)
        self.dz_spin.pack(side='left',padx=(10,6)); self.label(dz,'ADC 计数 / 每侧',9,color=MUTED).pack(side='left')
        self.dz_btn=ttk.Button(dz,text='更新',command=self.update_deadzone,padding=(5,2))
        self.dz_btn.pack(side='right',padx=(5,0))
        self.start_btn=ttk.Button(cal,text='开始 / 重新校准',command=self.start,style='Accent.TButton')
        self.start_btn.pack(fill='x',pady=(10,8))
        self.capture_btns={}; self.anchor_labels={}
        for number,(name,title) in enumerate((('left','推到最左'),('center','放回中心'),('right','推到最右')),1):
            row=tk.Frame(cal,bg=WHITE); row.pack(fill='x',pady=3)
            self.label(row,str(number),11,color=BLUE,bold=True,width=2).pack(side='left')
            btn=ttk.Button(row,text=title+'，记录',command=lambda n=name:self.send(n),width=15)
            btn.pack(side='left',padx=(3,7)); self.capture_btns[name]=btn
            lab=self.label(row,'—',10,color=MUTED,width=6,anchor='e'); lab.pack(side='right'); self.anchor_labels[name]=lab
        self.progress=ttk.Progressbar(cal,mode='determinate',maximum=200); self.progress.pack(fill='x',pady=(10,3))
        self.progress_label=self.label(cal,'每个位置保持约 200 ms',9,color=MUTED,anchor='w'); self.progress_label.pack(fill='x')
        actions=tk.Frame(cal,bg=WHITE); actions.pack(fill='x',pady=(9,7))
        self.commit_btn=ttk.Button(actions,text='仅应用 RAM',command=lambda:self.send('commit'),padding=(6,7))
        self.commit_btn.pack(side='left')
        self.commit_save_btn=ttk.Button(actions,text='保存并应用',command=lambda:self.send('commit_save'),style='Accent.TButton')
        self.commit_save_btn.pack(side='left',fill='x',expand=True,padx=(7,0))
        self.cancel_btn=ttk.Button(actions,text='取消',command=lambda:self.send('cancel')); self.cancel_btn.pack(side='left',padx=(8,0))
        self.config_label=self.label(cal,'有效值：—',9,color=MUTED,justify='left',anchor='w'); self.config_label.pack(fill='x')
        storage=tk.Frame(cal,bg=WHITE); storage.pack(fill='x',pady=(9,4))
        self.save_btn=ttk.Button(storage,text='保存当前配置到 Flash',command=lambda:self.send('save'),padding=(6,6))
        self.save_btn.pack(side='left',fill='x',expand=True)
        self.query_btn=ttk.Button(storage,text='查询状态',command=lambda:self.send('status'),padding=(7,6))
        self.query_btn.pack(side='left',padx=(7,0))
        self.flash_label=self.label(cal,'Flash：等待设备状态',9,color=MUTED,anchor='w',justify='left',wraplength=round(305*self.factor))
        self.flash_label.pack(fill='x',pady=(3,0))

        keys=tk.Frame(outer,bg=BG); keys.pack(fill='x',pady=(15,0))
        titles=tk.Frame(keys,bg=BG); titles.pack(fill='x')
        self.label(titles,'十键检测',13,bold=True).pack(side='left')
        self.tested_label=self.label(titles,'已检测 0 / 10',10,color=MUTED); self.tested_label.pack(side='right')
        groups=tk.Frame(keys,bg=BG); groups.pack(fill='x',pady=(8,0))
        self.tiles=[]
        for i,entry in enumerate(BUTTONS):
            col=i if i<5 else i+1
            groups.columnconfigure(col,weight=1,uniform='keys')
            tile=KeyTile(groups,entry,lambda n=i:self.demo_toggle(n)); tile.grid(row=0,column=col,sticky='ew',padx=3)
            self.tiles.append(tile)
        groups.columnconfigure(5,minsize=18)
        keyfooter=tk.Frame(keys,bg=BG); keyfooter.pack(fill='x',pady=(6,0))
        self.words_label=self.label(keyfooter,'左侧 LA / LB / LC / LS / LM        右侧 RA / RB / RC / RS / RM',9,color=MUTED)
        self.words_label.pack(side='left')
        ttk.Button(keyfooter,text='清零检测计数',command=self.clear).pack(side='right')
        self.demo_controls=tk.Frame(outer,bg=BG)
        self.label(self.demo_controls,'演示摇杆（物理位置）',9,color=ORANGE).pack(side='left')
        self.demo_slider=tk.Scale(self.demo_controls,from_=-100,to=100,orient='horizontal',resolution=1,
                                  command=self.demo_position,bg=BG,fg=INK,highlightthickness=0,
                                  troughcolor=LINE,length=350,showvalue=False)
        self.demo_slider.pack(side='left',fill='x',expand=True,padx=8)
        self.label(self.demo_controls,'左端 ←     中心     → 右端',9,color=MUTED).pack(side='left')
        bottom=tk.Frame(outer,bg=BG); bottom.pack(fill='x',pady=(12,0))
        self.stats_label=self.label(bottom,'接收 0 帧',9,color=MUTED); self.stats_label.pack(side='left')
        ttk.Button(bottom,text='导出检测报告',command=self.export).pack(side='right')
        self.status_label=tk.Label(outer,textvariable=self.message,bg=BG,fg=INK,font=(FONT,10),anchor='w',justify='left',wraplength=round(1050*self.factor))
        self.status_label.pack(fill='x',pady=(10,0))

    def log(self,text,error=False):
        self.logs.append(dict(time=time.strftime('%H:%M:%S'),message=text))
        self.message.set(text); self.status_label.configure(fg=RED if error else INK)

    def mode_changed(self):
        if self.worker and self.worker.is_alive(): return
        self.worker=None; self.last_snapshot=None
        self.demo=self.mode.get()=='演示模式'; self.reset_display(); self.scan()
        self.banner.configure(text='请选择设备并连接。',fg=MUTED)
        self.demo_controls.pack_forget()

    def reset_display(self):
        self.trace.clear(); self.last_frame=None
        self.lever_value.configure(text='—'); self.adc_value.configure(text='—')
        self.lever_meta.configure(text='等待设备数据'); self.jitter_label.configure(text='最近 200 帧原始 ADC 波动：—')
        self.config_label.configure(text='有效值：—'); self.progress['value']=0
        self.flash_label.configure(text='Flash：等待设备状态',fg=MUTED)
        for lab in self.anchor_labels.values():lab.configure(text='—',fg=MUTED)
        for tile in self.tiles:tile.update_value(False,0,False,False)
        self.tested_label.configure(text='已检测 0 / 10',fg=MUTED)
        self.stats_label.configure(text='接收 0 帧')

    def scan(self):
        if self.scanning or (self.worker and self.worker.is_alive()): return
        if self.mode.get()=='演示模式':
            self.devices=[]; self.device_combo.configure(values=['离线模拟控制器（反向电位器）'])
            self.selected.set('离线模拟控制器（反向电位器）'); return
        self.scanning=True; self.log('正在查找音击控制器…')
        def scan_thread():
            try: self.events.put(dict(kind='scan',devices=enumerate_hardware()))
            except Exception as exc: self.events.put(dict(kind='scan',devices=[],error=str(exc)))
        threading.Thread(target=scan_thread,daemon=True).start()

    def connect(self):
        if self.worker and self.worker.is_alive(): return
        self.demo=self.mode.get()=='演示模式'
        if self.demo:
            with self.demo_state.lock:
                self.demo_state.position=0; self.demo_state.keys=[False]*10
            self.demo_slider.set(0)
            factory=lambda:DemoDevice(self.demo_state)
            self.demo_controls.pack(fill='x',before=self.stats_label.master,pady=(5,0))
            self.banner.configure(text='演示模式：全部数据为模拟值。点击十个按键切换状态；滑动摇杆完成模拟校准。',fg=ORANGE)
        else:
            index=self.device_combo.current()
            if index<0 or index>=len(self.devices): self.log('没有选中设备。连接 USB 后点击“刷新设备”。',True); return
            factory=hardware_factory(self.devices[index]['path'])
            self.demo_controls.pack_forget()
            self.banner.configure(text='实时设备模式：按键与摇杆数据来自控制器。校准时请关闭游戏及其他写入工具。',fg=MUTED)
        self.reset_display(); self.last_count=0; self.rate=0
        self.last_rate_time=time.monotonic(); self.start_deadzone=None
        self.worker=Worker(factory,self.events); self.worker.start()
        self.root.title('音击控制器测试台 · Flash版'+(' · 演示模式' if self.demo else ''))
        self.log('正在连接'+('模拟控制器…' if self.demo else '控制器…'))

    def disconnect(self):
        if self.local_flash_pending():
            self.log('正在写入 Flash，请等待结果后再断开。',True);return
        self.start_deadzone=None
        if self.worker:
            self.worker.stop_flag.set(); self.log('正在关闭连接并取消未提交校准…')

    def send(self,name,value=0):
        if not self.worker: return
        try:
            self.worker.submit(name,value)
            self.log(NAMES[name]+('，请保持 USB 连接，等待写入及校验回执…' if name in FLASH_COMMANDS else '，等待设备确认…'))
        except Exception as exc: self.log(str(exc),True)

    def start(self):
        try:
            value=int(self.deadzone.get())
            if not 0<=value<=4094: raise ValueError()
        except ValueError:
            self.log('中心死区请输入 0～4094 的整数，建议先使用 0。',True); return
        if self.worker:
            try:
                self.worker.submit('begin'); self.start_deadzone=value
                self.log('正在开始校准，等待设备确认…')
            except Exception as exc: self.log(str(exc),True)

    def clear(self):
        if self.worker: self.worker.clear()
        self.trace.clear(); self.log('检测计数和行程范围已清零。')

    def update_deadzone(self):
        try:
            value=int(self.deadzone.get())
            if not 0<=value<=4094:raise ValueError()
        except ValueError:
            self.log('中心死区请输入 0～4094 的整数。',True);return
        self.send('deadzone',value)

    def demo_toggle(self,index):
        if not self.demo or not self.worker or not self.worker.snapshot()['connected']: return
        with self.demo_state.lock: self.demo_state.keys[index]=not self.demo_state.keys[index]

    def demo_position(self,value):
        with self.demo_state.lock: self.demo_state.position=float(value)

    def _sync_controls(self,connected,cal,pending):
        alive=bool(self.worker and self.worker.is_alive())
        flash_pending=self.local_flash_pending()
        uncertain=bool(self.worker and self.worker.snapshot()['uncertain'])
        state=lambda enabled:'normal' if enabled else 'disabled'
        self.connect_btn.configure(state=state(not alive and not self.scanning))
        self.disconnect_btn.configure(state=state(alive and not flash_pending))
        self.scan_btn.configure(state=state(not alive and not self.scanning and self.mode.get()=='真实设备'))
        self.device_combo.configure(state='disabled' if alive else 'readonly')
        self.mode_combo.configure(state='disabled' if alive else 'readonly')
        ready=connected and cal is not None and not pending and not cal['busy'] and not cal['saving'] and not uncertain
        self.start_btn.configure(state=state(ready))
        self.dz_spin.configure(state=state(ready))
        self.dz_btn.configure(state=state(ready and cal['session'] if cal else False))
        for b in self.capture_btns.values(): b.configure(state=state(ready and cal['session'] if cal else False))
        self.commit_btn.configure(state=state(ready and cal['session'] and cal['captured']==7 if cal else False))
        self.commit_save_btn.configure(state=state(ready and cal['flash_supported'] and cal['session'] and cal['captured']==7 if cal else False))
        self.save_btn.configure(state=state(ready and cal['flash_supported'] and cal['valid'] and not cal['session'] and not cal['persisted'] if cal else False))
        self.query_btn.configure(state=state(connected and cal is not None and not pending))
        self.cancel_btn.configure(state=state(connected and cal is not None and not flash_pending and not cal['saving'] and not uncertain and (cal['session'] or pending)))

    def local_flash_pending(self):
        return bool(self.worker and self.worker.snapshot()['pending_op'] in FLASH_COMMANDS)

    def handle_event(self,event):
        kind=event['kind']
        if kind=='scan':
            self.scanning=False; self.devices=event['devices']
            labels=[f"{i+1}. {d.get('product_string') or 'IO4'}  /  {d.get('serial_number') or '无序列号'}  /  接口 {d.get('interface_number',0)}" for i,d in enumerate(self.devices)]
            self.device_combo.configure(values=labels)
            self.selected.set(labels[0] if len(labels)==1 else ('请选择设备' if labels else '没有找到设备'))
            if event.get('error'): self.log('枚举失败：'+event['error'],True)
            else: self.log(f'找到 {len(labels)} 个匹配设备。'+('点击连接。' if labels else '请插入控制器后刷新。'))
        elif kind=='connected': self.log('已连接，等待第一帧有效数据。')
        elif kind=='error': self.start_deadzone=None; self.log(event['message'],True)
        elif kind=='disconnected':
            if not self.closed and not (self.worker and self.worker.error): self.log('连接已关闭。')
        elif kind=='done':
            if event['error']:
                self.start_deadzone=None; self.log(event['error'],True)
            elif event['name']=='begin' and self.start_deadzone is not None:
                value=self.start_deadzone; self.start_deadzone=None; self.send('deadzone',value)
            elif event['name']=='deadzone': self.log('已开始校准。推到物理最左端，点击“推到最左，记录”，保持约 200 ms。')
            elif event['name']=='left': self.log('左端采集完成。把摇杆放回物理中心，记录中心。')
            elif event['name']=='center': self.log('中心采集完成。推到物理最右端，记录右端。')
            elif event['name']=='right': self.log('右端采集完成。三个位置都已记录后，点击“保存并应用”，断电后可自动恢复。')
            elif event['name']=='commit':
                cal=event['status']
                self.log('校准已应用；当前参数与已保存的 Flash 记录一致。' if cal and cal['persisted'] else
                         '校准仅应用到设备 RAM，尚未保存。需要断电恢复时点击“保存当前配置到 Flash”。')
            elif event['name'] in FLASH_COMMANDS:
                cal=event['status']
                if cal and cal['persisted'] and cal['valid'] and not cal['saving']:
                    self.log(('演示：已模拟保存，未写入真实设备。' if self.demo else '设备已确认写入 Flash 并通过回读校验，断电后自动恢复。'))
                else:self.log('设备未确认持久化成功，请查询状态核对。',True)
            elif event['name']=='status':self.log('状态已刷新，请核对有效值和 Flash 保存状态。')
            elif event['name']=='cancel': self.log('已取消本次校准，保留上一次有效配置。')

    def tick(self):
        if self.closed: return
        while True:
            try:self.handle_event(self.events.get_nowait())
            except queue.Empty:break
        now=time.monotonic()
        s=self.worker.snapshot() if self.worker else None
        f=s['frame'] if s else None; cal=f['cal'] if f else None
        fresh=bool(s and s['connected'] and f and now-s['last_rx']<0.6 and not self.worker.stop_flag.is_set())
        self.last_snapshot=s
        self.connection_label.configure(text=('● 演示连接' if self.demo else '● 已连接') if fresh else ('等待数据' if s and s['connected'] else '○ 未连接'),fg=(ORANGE if self.demo else GREEN) if fresh else MUTED)
        self._sync_controls(fresh,cal,bool(s and s['pending']))
        saving=bool(s and s['connected'] and (s['pending_op'] in FLASH_COMMANDS or (cal and cal['saving'])))
        if saving != self.flash_progress:
            self.flash_progress=saving;self.progress.stop()
            self.progress.configure(mode='indeterminate' if saving else 'determinate')
            if saving:self.progress.start(14)
            else:self.progress['value']=0
        if f:
            self.last_frame=f
            self.lever_value.configure(text=str(f['lever']) if fresh else '—')
            self.adc_value.configure(text=str(cal['raw']) if cal and fresh else '—')
            self.lever_meta.configure(text=(f"0x{f['lever']:04X}   /   {f['lever']/65535*100:.1f}%   行程 {s['minimum']}～{s['maximum']}" if fresh else '数据已停止更新，等待重新连接'))
            self.jitter_label.configure(text=('最近 200 帧原始 ADC 波动：'+str(s['jitter'])+' 计数' if cal and fresh else ('当前固件不回传原始 ADC；按键和 IO4 位置仍可检测' if not cal else '最近 200 帧原始 ADC 波动：—')))
            self.words_label.configure(text=f"Buttons0  0x{f['words'][0]:04X}       Buttons1  0x{f['words'][1]:04X}       系统  {f['system']:02X} / USB  {f['usb']:02X}" if fresh else '按键显示已置灰，历史计数保留')
            if fresh and now-self.last_plot>=0.04:
                self.trace.append(f['lever']); self.last_plot=now
            if now-self.last_rate_time>=0.5:
                self.rate=(s['total']-self.last_count)/(now-self.last_rate_time)
                self.last_rate_time=now; self.last_count=s['total']
            for i,tile in enumerate(self.tiles):tile.update_value(s['down'][i],s['presses'][i],s['seen'][i],fresh)
            self.tested_label.configure(text=f"已检测 {sum(s['seen'])} / 10",fg=GREEN if all(s['seen']) else MUTED)
            self.stats_label.configure(text=f"{'模拟' if self.demo else '接收'} {s['total']} 帧    {self.rate if fresh else 0:.0f} 帧/秒    异常 {s['malformed']}"+(f"    队列丢弃 {cal['queue_drops']}" if cal else ''))
        if cal:
            state='未校准'
            if cal['valid']:state='已校准 · '+('反向电位器' if cal['invert'] else '正向电位器')
            if cal['session']:state='正在校准（原配置继续工作）'
            if not fresh:state='数据已停止更新'
            if saving:state='正在写入 Flash · 等待回读校验'
            self.cal_state.configure(text=state,fg=GREEN if cal['valid'] and fresh else MUTED)
            if not saving:self.progress['value']=cal['samples'] if fresh else 0
            self.progress_label.configure(text=('正在保存，请保持连接；最多等待 15 秒' if saving else
                f"正在采集  {cal['samples']} / 200，请保持不动" if cal['busy'] and fresh else '每个位置保持约 200 ms'))
            for name,bit in (('left',1),('center',2),('right',4)):
                self.anchor_labels[name].configure(text=('✓ '+str(cal['pending_'+name])) if cal['captured']&bit else '—',fg=GREEN if cal['captured']&bit else MUTED)
            self.config_label.configure(text=(f"有效值  左 {cal['left']}  /  中 {cal['center']}  /  右 {cal['right']}\n中心死区  ±{cal['deadzone']} ADC" if cal['valid'] else '有效值：未校准，使用全量程映射'))
            if saving:flash_text='Flash：写入中，尚未确认成功';flash_color=ORANGE
            elif s['uncertain']:flash_text='Flash：保存结果未知，请查询状态或重连';flash_color=RED
            elif not fresh:flash_text='Flash：数据未更新，请重新连接核对';flash_color=MUTED
            elif not cal['flash_supported']:flash_text='Flash：接口不可用，请使用配套持久化固件';flash_color=ORANGE
            elif cal['persisted'] and cal['valid']:
                flash_text='Flash：当前有效配置已保存，可断电恢复';flash_color=GREEN
            elif cal['valid']:flash_text='Flash：当前配置仅在 RAM，断电后会丢失';flash_color=ORANGE
            else:flash_text='Flash：尚无有效校准，请先采集三个位置';flash_color=MUTED
            if fresh and not saving and cal['loaded_at_boot']:flash_text+='\n本次启动已从 Flash 加载记录'
            self.flash_label.configure(text=flash_text,fg=flash_color)
        elif f:
            self.cal_state.configure(text='旧固件：校准接口不可用',fg=ORANGE)
        else:
            self.cal_state.configure(text='等待连接',fg=MUTED)
        self.draw_lever(fresh); self.draw_trace()
        self.root.after(33,self.tick)

    def draw_lever(self,fresh=None):
        c=self.bar; c.delete('all'); w=max(100,c.winfo_width()/self.factor); l=10;r=w-10
        c.create_rectangle(l,15,r,27,fill='#E7EEF7',outline='')
        c.create_line(w/2,7,w/2,32,fill=MUTED)
        c.create_text(l,46,text='左 0',anchor='w',font=(FONT,9),fill=MUTED)
        c.create_text(w/2,46,text='中心 32768',font=(FONT,9),fill=MUTED)
        c.create_text(r,46,text='65535 右',anchor='e',font=(FONT,9),fill=MUTED)
        if fresh is None:
            fresh=bool(self.last_snapshot and self.last_snapshot['connected'] and time.monotonic()-self.last_snapshot['last_rx']<0.6)
        if self.last_frame and fresh:
            x=l+(r-l)*self.last_frame['lever']/65535
            c.create_rectangle(l,15,x,27,fill=BLUE,outline='')
            c.create_oval(x-7,11,x+7,31,fill=BLUE,outline=WHITE,width=2)
        c.scale('all',0,0,self.factor,self.factor)

    def draw_trace(self):
        c=self.chart;c.delete('all');w=max(100,c.winfo_width()/self.factor);h=max(70,c.winfo_height()/self.factor)
        left=42;right=w-8;top=14;bottom=h-18
        for v,text in ((0,'65535'),(.5,'32768'),(1,'0')):
            y=top+(bottom-top)*v;c.create_line(left,y,right,y,fill=LINE,dash=(3,3))
            c.create_text(left-6,y,text=text,anchor='e',font=(FONT,8),fill=MUTED)
        c.create_text(right,bottom+12,text='最近约 10 秒 · IO4 位置',anchor='e',font=(FONT,8),fill=MUTED)
        if len(self.trace)>1:
            points=[]
            for i,v in enumerate(self.trace):points.extend((right-(len(self.trace)-1-i)*(right-left)/249,bottom-v/65535*(bottom-top)))
            c.create_line(*points,fill=BLUE,width=2)
        c.scale('all',0,0,self.factor,self.factor)

    def export(self):
        s=self.last_snapshot
        if not s or not s['frame']:self.log('收到设备数据后即可导出报告。',True);return
        filename=filedialog.asksaveasfilename(parent=self.root,title='保存检测报告',defaultextension='.json',
                    initialfile='音击控制器检测-'+time.strftime('%Y%m%d-%H%M%S')+'.json',filetypes=[('JSON 检测报告','*.json')])
        if not filename:return
        report=dict(application='音击控制器测试台 1.1 Flash',time=time.strftime('%Y-%m-%d %H:%M:%S'),
                    mode='模拟数据' if self.demo else '设备实时数据',snapshot=s,
                    buttons=[dict(name=b[0],word=b[1],bit=b[2],presses=s['presses'][i],tested=s['seen'][i]) for i,b in enumerate(BUTTONS)],
                    logs=list(self.logs),note='05仅应用RAM；09保存当前配置；0A保存并应用。persisted为设备回传，不是主机推测。演示模式不写真实Flash。')
        try:Path(filename).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');self.log('检测报告已保存：'+filename)
        except OSError as exc:self.log('报告保存失败：'+str(exc),True)

    def close(self):
        if self.local_flash_pending():
            self.log('正在写入 Flash，请等待结果后再关闭窗口。',True);return
        self.closed=True
        if self.worker:
            self.worker.stop_flag.set(); self.worker.join(timeout=0.7)
        self.root.destroy()

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--demo',action='store_true');args=ap.parse_args()
    if sys.platform=='win32':
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:pass
    root=tk.Tk();App(root,args.demo);root.mainloop()

if __name__=='__main__':main()
