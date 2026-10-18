#include "lever_calibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

static uint8_t s[29], seq;
static void cmd(uint8_t op, uint16_t value, uint32_t now)
{
    uint8_t d[63] = {0xC0, 'L', 'C', 1};
    d[4] = ++seq; d[5] = op; d[6] = (uint8_t)value; d[7] = (uint8_t)(value >> 8);
    assert(LeverCal_HandleCommand(d, 63, now));
    LeverCal_WriteStatus(s);
}
static uint16_t u16(unsigned n) { return (uint16_t)(s[n] | ((uint16_t)s[n+1] << 8)); }
static void capture_avg(uint8_t op, uint16_t value, uint32_t now)
{
    unsigned i;
    cmd(op, 0, now); assert(s[5] == LEVER_CAL_SAMPLING);
    for (i = 0; i < 200; ++i) {
        LeverCal_Task(value, now, true); /* same tick MUST NOT count twice */
        LeverCal_WriteStatus(s); assert(u16(9) == i);
        LeverCal_Task(value, ++now, true);
    }
    LeverCal_WriteStatus(s); assert(s[5] == LEVER_CAL_OK); assert(u16(9) == 200);
}
static void setup(uint16_t l, uint16_t c, uint16_t r, uint16_t dz)
{
    cmd(1, 0, 0); cmd(8, dz, 0);
    LeverCal_CaptureLeft(l); LeverCal_CaptureCenter(c); LeverCal_CaptureRight(r);
    cmd(5, 0, 0); assert(s[5] == 0);
}
int main(void)
{
    uint32_t x; uint16_t previous; uint8_t d[63] = {0};
    LeverCal_Init(); assert(!LeverCal_IsValid());
    assert(LeverCal_Convert(0) == 0 && LeverCal_Convert(4095) == 65535);
    assert(LeverCal_Convert(65535) == 65535);
    cmd(2, 0, 0); assert(s[5] == LEVER_CAL_NO_SESSION);
    cmd(1, 0, 0); cmd(5, 0, 0); assert(s[5] == LEVER_CAL_INCOMPLETE);
    capture_avg(2, 300, 0); capture_avg(3, 2010, 200); capture_avg(4, 3800, 400);
    cmd(5, 0, 601); assert(s[5] == 0 && LeverCal_IsValid());
    assert(LeverCal_Convert(300) == 0 && LeverCal_Convert(2010) == 32768 && LeverCal_Convert(3800) == 65535);
    previous = 0;
    for (x = 0; x <= 4095; ++x) { uint16_t v=LeverCal_Convert((uint16_t)x); assert(v>=previous); previous=v; }
    setup(3780, 2063, 312, 10);
    assert(LeverCal_GetConfig()->invert);
    assert(LeverCal_Convert(3780)==0 && LeverCal_Convert(2063)==32768 && LeverCal_Convert(312)==65535);
    assert(LeverCal_Convert(2053)==32768 && LeverCal_Convert(2073)==32768);
    assert(LeverCal_Convert(2052)>32768 && LeverCal_Convert(2074)<32768);
    previous = 65535;
    for (x = 0; x <= 4095; ++x) { uint16_t v=LeverCal_Convert((uint16_t)x); assert(v<=previous); previous=v; }
    setup(100, 1800, 4000, 20);
    assert(LeverCal_Convert(1780)==32768 && LeverCal_Convert(1820)==32768);
    assert(LeverCal_Convert(1779)<32768 && LeverCal_Convert(1821)>32768);
    cmd(1,0,0); LeverCal_CaptureLeft(0); LeverCal_CaptureCenter(20); LeverCal_CaptureRight(4095);
    cmd(5,0,0); assert(s[5]==LEVER_CAL_BAD_ANCHORS && LeverCal_GetConfig()->left==100);
    LeverCal_CaptureCenter(100); cmd(8,100,0); cmd(5,0,0); assert(s[5]==LEVER_CAL_BAD_DEADZONE);
    LeverCal_CaptureCenter(4095); cmd(5,0,0); assert(s[5]==LEVER_CAL_BAD_ANCHORS);
    LeverCal_CaptureLeft(65535); /* out of range cannot replace the existing point */
    cmd(6,0,0); assert(LeverCal_GetConfig()->left==100);
    cmd(1,0,0); cmd(2,0,0);
    cmd(5,0,0); assert(s[5]==LEVER_CAL_BUSY);
    for(x=1;x<=200;++x) { LeverCal_Task((x&1)?100:102,x,true); }
    LeverCal_WriteStatus(s); assert(s[5]==0 && s[4]==2 && u16(21)==101);
    cmd(2,0,201);
    for(x=202;x<=401;++x) { LeverCal_Task((x&1)?100:200,x,true); }
    LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_UNSTABLE && !(s[7]&1));
    cmd(5,0,402); assert(s[5]==LEVER_CAL_INCOMPLETE);
    cmd(2,0,500); LeverCal_Task(100,501,false); LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_ADC_ERROR);
    cmd(2,0,500); LeverCal_Task(5000,501,true); LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_ADC_ERROR);
    cmd(2,0,500); LeverCal_Task(100,2501,true); LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_TIMEOUT);
    capture_avg(2, 200, UINT32_MAX-50U); /* millisecond wrap */
    cmd(2,0,0); cmd(6,0,0); LeverCal_Task(0,1,true); LeverCal_WriteStatus(s); assert(s[4]==6 && !(s[6]&6));
    assert(!LeverCal_HandleCommand(d,63,0));
    d[0]=0xC0; d[1]='L'; d[2]='C'; d[3]=2;
    assert(!LeverCal_HandleCommand(d,62,0));
    assert(LeverCal_HandleCommand(d,63,0)); LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_BAD_COMMAND);
    d[3]=1; d[5]=1; d[62]=1; LeverCal_HandleCommand(d,63,0); LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_BAD_COMMAND);
    d[62]=0; d[6]=1; LeverCal_HandleCommand(d,63,0); LeverCal_WriteStatus(s); assert(s[5]==LEVER_CAL_BAD_COMMAND);
    cmd(1,0,0); cmd(8,4095,0); assert(s[5]==LEVER_CAL_BAD_DEADZONE);
    setup(0,64,4095,63); assert(LeverCal_Convert(63)==32768 && LeverCal_Convert(0)==0);
    LeverCal_Init(); assert(!LeverCal_IsValid());
    puts("PASS: mapping 8192 ADC points, anchors, reverse, deadzone, averaging, sequencing, errors, cancel, tick wrap, malformed commands");
    return 0;
}
