"""Test the actual IO4_Task body with mocked USB/ADC and NOR Flash backend."""
from pathlib import Path
import re
import subprocess
import sys

p=Path(__file__).resolve().parents[1]
t=(p/'Core/Src/main.c').read_text(encoding='utf-8-sig')
match=re.search(r'static void IO4_Task\(void\)\s*\{',t)
start=match.start();end=t.index('{',start)+1;depth=1
while depth:
    if t[end]=='{':depth+=1
    if t[end]=='}':depth-=1
    end+=1
body=t[start:end]
prefix=r'''
#include "lever_calibration.h"
#include "mock_flash.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define USBD_STATE_CONFIGURED 1U
#define HAL_ADC_ERROR_NONE 0U
#define HAL_DMA_ERROR_NONE 0U
#define ADC_CR2_ADON 1U
#define DMA_SxCR_EN 1U
typedef struct { uint32_t CR; } Stream;
typedef struct { Stream *Instance; } DMA;
typedef struct { uint32_t CR2; } ADC;
typedef struct { uint32_t dev_state; } USB;
typedef struct { uint32_t connection_generation; } USBD_HID_IO4StatsTypeDef;
static Stream stream={1};static DMA hdma_adc1={&stream};static ADC adc={1};
#define ADC1 (&adc)
static int hadc1;
static USB hUsbDeviceFS={1};
static uint16_t adc_raw=2048;
static uint32_t tick,last_hid_tick,generation=1,sends;
static uint8_t ready=1,queued,command_buffer[63],sent_status[29];
static uint32_t HAL_GetTick(void) { return tick; }
static uint32_t HAL_ADC_GetError(int *unused) { (void)unused;return 0; }
static uint32_t HAL_DMA_GetError(DMA *unused) { (void)unused;return 0; }
static void USBD_HID_GetIO4Stats(USBD_HID_IO4StatsTypeDef *s) { s->connection_generation=generation; }
static uint8_t USBD_HID_GetNextCalibrationCommand(uint8_t *out) {
if(!queued) { return 0; }
memcpy(out,command_buffer,63);queued=0;return 1;
}
static uint8_t USBD_HID_CanSendReport(USB *unused) { (void)unused;return ready; }
static void IO4_SendInputReport(void) { ++sends;LeverCal_WriteStatus(sent_status);ready=0; }
static void request(uint8_t op) {
memset(command_buffer,0,63);command_buffer[0]=0xC0;command_buffer[1]='L';command_buffer[2]='C';
command_buffer[3]=1;command_buffer[4]=(uint8_t)tick;command_buffer[5]=op;queued=1;
}
'''
tests=r'''
int main(void) {
  uint32_t written;uint8_t s[29];
  MockFlash_Reset();LeverCal_Init();tick=1;IO4_Task(); /* observe USB generation first */
  LeverCal_Begin();LeverCal_CaptureLeft(300);LeverCal_CaptureCenter(2000);LeverCal_CaptureRight(3800);
  assert(LeverCal_Commit());
  ready=1;tick=2;request(9);IO4_Task();
  assert(LeverCal_SavePending() && sent_status[5]==LEVER_CAL_SAVING && MockFlash_Writes()==0);
  tick=3;IO4_Task();assert(MockFlash_Writes()==0);
  ready=1;tick=4;IO4_Task();assert(MockFlash_Writes()==8 && sent_status[5]==0 && (sent_status[6]&16));
  written=MockFlash_Writes();
  /* Cancel before the notice is acknowledged must suppress all storage writes. */
  LeverCal_Begin();LeverCal_CaptureLeft(3780);LeverCal_CaptureCenter(2063);LeverCal_CaptureRight(312);
  ready=0;tick=5;request(10);IO4_Task();assert(LeverCal_SavePending());
  ready=1;tick=6;IO4_Task();assert(sent_status[5]==LEVER_CAL_SAVING);
  ready=1;tick=7;request(6);IO4_Task();assert(!LeverCal_SavePending() && MockFlash_Writes()==written);
  /* No IN transfer available: timeout without ever touching Flash. */
  ready=0;tick=8;request(9);IO4_Task();
  tick=3010;IO4_Task();LeverCal_WriteStatus(s);
  assert(!LeverCal_SavePending() && s[5]==LEVER_CAL_TIMEOUT && MockFlash_Writes()==written);
  /* IN sent but not completed: same no-write timeout. */
  ready=1;tick=3011;request(9);IO4_Task();assert(LeverCal_SavePending());
  tick=6012;IO4_Task();LeverCal_WriteStatus(s);
  assert(!LeverCal_SavePending() && s[5]==LEVER_CAL_TIMEOUT && MockFlash_Writes()==written);
  /* Reset/re-enumeration discards the old queued operation. */
  ready=1;tick=6013;request(9);IO4_Task();assert(LeverCal_SavePending());
  ++generation;ready=1;tick=6014;IO4_Task();assert(!LeverCal_SavePending() && MockFlash_Writes()==written);
  puts("PASS: actual IO4_Task save handshake, no Flash before IN completion, cancel, both notice timeouts, USB reset cancellation");
  return 0;
}
'''
scratch=Path(sys.argv[1]).resolve();scratch.mkdir(parents=True,exist_ok=True)
source=scratch/'save_handshake.c';exe=scratch/'save_handshake.exe'
source.write_text(prefix+'\n'+body+'\n'+tests)
subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror',
    '-I',str(p/'Core/Inc'),'-I',str(p/'tools'),str(source),
    str(p/'Core/Src/lever_calibration.c'),str(p/'Core/Src/lever_storage.c'),
    str(p/'tools/mock_flash.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
