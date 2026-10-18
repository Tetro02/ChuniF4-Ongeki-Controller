"""Compile actual parser/queue/receive function bodies with mocked USB/IRQ I/O.
This validates software normalisation, NOT a physical USB transaction.
Run from any directory: python test_usb_routes.py <scratch-directory>
"""
from pathlib import Path
import subprocess
import sys
import re

p = Path(__file__).resolve().parents[1]
t = (p/'Middlewares/ST/STM32_USB_Device_Library/Class/HID/Src/usbd_hid.c').read_text(encoding='utf-8')
def function(name):
    start = re.search(r'(?:static )?(?:void|uint8_t) '+name+r'\([^;]+?\)\s*\{',t).start()
    brace = t.index('{',start); end = brace+1; depth=1
    while depth:
        if t[end]=='{': depth+=1
        if t[end]=='}': depth-=1
        end+=1
    return t[start:end]

prefix = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "lever_calibration.h"
#define IO4_OUT_REPORT_ID 0x10U
#define IO4_OUT_REPORT_LEN 64U
#define IO4_OUT_PAYLOAD_LEN 62U
#define IO4_CAL_QUEUE_SLOTS 5U
#define HID_EPOUT_SIZE 64U
#define USBD_FAIL 2U
#define USBD_OK 0U
static uint8_t IO4_CalQueue[5][63], IO4_CalHead, IO4_CalTail, IO4_LastOutputCmd;
static uint16_t IO4_LastOutputLength;
static uint32_t IO4_CalDrops, IO4_OutputPacketCount, IO4_MalformedOutputCount;
static uint32_t irq, rx_length, rearmed;
static uint8_t HIDOutEpAdd=1;
typedef struct { uint8_t OutReport[64], OutReportType, OutReportId;
uint16_t OutReportLength; uint8_t OutEpReport[64]; uint16_t OutEpReportLength;
} USBD_HID_HandleTypeDef;
typedef struct { void *pClassDataCmsit[1]; uint8_t classId; } USBD_HandleTypeDef;
static uint32_t __get_PRIMASK(void) { return irq; }
static void __disable_irq(void) { irq=1; }
static void __set_PRIMASK(uint32_t v) { irq=v; }
static void __DMB(void) {}
static uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *p,uint8_t ep) { (void)p;(void)ep;return rx_length; }
static uint8_t USBD_LL_PrepareReceive(USBD_HandleTypeDef *p,uint8_t ep,uint8_t *b,uint16_t n) {
(void)p;(void)ep; assert(n==64); memset(b,0xEE,n); ++rearmed; return 0; }
'''
tests = r'''
int main(void) {
  USBD_HID_HandleTypeDef h={0}; USBD_HandleTypeDef dev={{&h},0};
  uint8_t report[64]={0x10,0xC0,'L','C',1,23,1}, out[63]; unsigned i;
  memcpy(h.OutEpReport,report,64); rx_length=64;
  assert(USBD_HID_DataOut(&dev,1)==0 && rearmed==1);
  assert(USBD_HID_GetNextCalibrationCommand(out)); assert(memcmp(out,report+1,63)==0);
  assert(irq==0); irq=1; assert(!USBD_HID_GetNextCalibrationCommand(out)); assert(irq==1); irq=0;
  assert(IO4_OutputPacketCount==1 && IO4_LastOutputLength==64 && IO4_LastOutputCmd==0xC0);
  for(i=0;i<2;++i) {
    memcpy(h.OutReport,report+i,64-i); h.OutReportLength=(uint16_t)(64-i);
    h.OutReportType=2; h.OutReportId=0x10; rx_length=64-i;
    assert(USBD_HID_EP0_RxReady(&dev)==0 && h.OutReportType==0);
    assert(USBD_HID_GetNextCalibrationCommand(out)); assert(memcmp(out,report+1,63)==0);
  }
  h.OutReportType=2; h.OutReportLength=64; rx_length=2;
  USBD_HID_EP0_RxReady(&dev); assert(IO4_MalformedOutputCount==1);
  assert(!USBD_HID_GetNextCalibrationCommand(out));
  IO4_HandleOutputReport(report,63,0,0);
  IO4_HandleOutputReport(report,64,0x11,1);
  report[0]=0x11; IO4_HandleOutputReport(report,64,0,0); report[0]=0x10;
  assert(IO4_MalformedOutputCount==4);
  report[1]=0x55; IO4_HandleOutputReport(report,64,0,0); report[1]=0xC0;
  assert(!USBD_HID_GetNextCalibrationCommand(out));
  for(i=0;i<5;++i) { report[5]=(uint8_t)i; IO4_HandleOutputReport(report,64,0,0); }
  assert(IO4_CalDrops==1);
  for(i=0;i<4;++i) { assert(USBD_HID_GetNextCalibrationCommand(out)); assert(out[4]==i); }
  assert(!USBD_HID_GetNextCalibrationCommand(out));
  assert(IO4_MalformedOutputCount==4);
  puts("PASS: actual USB parser/queue/EP0/DataOut bodies, 64/63-byte EP0, short/rejected frames, owned copy, IRQ restore, FIFO overflow");
  return 0;
}
'''
out = Path(sys.argv[1]).resolve(); out.mkdir(parents=True,exist_ok=True)
source = out/'usb_routes.c'; exe = out/'usb_routes.exe'
source.write_text(prefix+'\n'+ '\n'.join(function(n) for n in (
    'IO4_HandleOutputReport','USBD_HID_GetNextCalibrationCommand',
    'USBD_HID_EP0_RxReady','USBD_HID_DataOut'))+'\n'+tests)
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I',str(p/'Core/Inc'),str(source),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
