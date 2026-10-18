#include "lever_storage.h"
#include "mock_flash.h"
#include <assert.h>
#include <stdio.h>

static const LeverCalibration_t a = {300U, 2000U, 3800U, 8U, 0U, 1U};
static const LeverCalibration_t b = {3780U, 2063U, 312U, 12U, 1U, 1U};
static const LeverCalibration_t c = {100U, 1800U, 4000U, 0U, 0U, 1U};
static uint8_t status[29], seq;
static void expect(const LeverCalibration_t *wanted)
{
    LeverCalibration_t found;
    assert(LeverStore_Load(&found) == LEVER_STORE_OK);
    assert(found.left == wanted->left && found.center == wanted->center && found.right == wanted->right && found.deadzone == wanted->deadzone);
    assert(found.valid && found.invert == (wanted->left > wanted->right));
}
static void cmd(uint8_t op)
{
    uint8_t d[63] = {0xC0,'L','C',1};
    d[4] = ++seq; d[5] = op;
    assert(LeverCal_HandleCommand(d, sizeof(d), 0U));
    LeverCal_WriteStatus(status);
}
static void draft(const LeverCalibration_t *cfg)
{
    uint8_t d[63] = {0xC0,'L','C',1};
    cmd(1);
    LeverCal_CaptureLeft(cfg->left); LeverCal_CaptureCenter(cfg->center); LeverCal_CaptureRight(cfg->right);
    d[4] = ++seq; d[5] = 8U; d[6] = (uint8_t)cfg->deadzone; d[7] = (uint8_t)(cfg->deadzone >> 8U);
    (void)LeverCal_HandleCommand(d, sizeof(d), 0U);
}
static uint32_t reference_crc(uint32_t address)
{
    uint32_t result = 0xFFFFFFFFUL, offset, j, k;
    for (offset = 0U; offset < 24U; offset += 4U) {
        uint32_t word = MockFlash_Peek(address + offset);
        for (j = 0U; j < 4U; ++j) {
            result ^= (word >> (8U*j)) & 255U;
            for (k=0U;k<8U;++k) { result = (result & 1U) ? (result >> 1U)^0xEDB88320UL : result >> 1U; }
        }
    }
    return ~result;
}
static void sequence_at(uint32_t address, uint32_t value)
{
    MockFlash_Poke(address+8U,value);
    MockFlash_Poke(address+24U,reference_crc(address));
}
int main(void)
{
    LeverCalibration_t cfg;
    uint32_t writes, word, base = LEVER_STORE_BASE;
    int cut;
    MockFlash_Reset();
    assert(LeverStore_Load(&cfg) == LEVER_STORE_EMPTY);
    cfg = a; cfg.center = cfg.left;
    assert(LeverStore_Save(&cfg) == LEVER_STORE_BAD_CONFIG && MockFlash_Writes() == 0U);
    assert(LeverStore_Save(&a) == LEVER_STORE_OK); expect(&a);
    /* Independently generated with Python zlib.crc32 over the 24 LE bytes. */
    assert(MockFlash_Peek(base+24U) == 0x45E4E7F0UL);
    assert(MockFlash_Writes() == 8U && MockFlash_Erases() == 0U);
    assert(LeverStore_Save(&a) == LEVER_STORE_OK && MockFlash_Writes() == 8U);
    assert(LeverStore_Save(&b) == LEVER_STORE_OK); expect(&b);
    /* Every word, including CRC / marker / schema, is independently protected. */
    for (word = 0U; word < 8U; ++word) {
        uint32_t address = base+32U+word*4U, original = MockFlash_Peek(address);
        MockFlash_Poke(address,original^1U); expect(&a); MockFlash_Poke(address,original);
    }
    /* A CRC-correct record with illegal calibration values is still rejected. */
    MockFlash_Poke(base+32U+12U, (5000U | (2063UL << 16U)));
    MockFlash_Poke(base+32U+24U,reference_crc(base+32U)); expect(&a);
    for (cut=0;cut<8;++cut) {
        MockFlash_Reset(); assert(LeverStore_Save(&a) == LEVER_STORE_OK);
        MockFlash_FailAfter(cut); assert(LeverStore_Save(&b) != LEVER_STORE_OK);
        MockFlash_FailAfter(-1); expect(&a);
        LeverCal_Init(); assert(LeverCal_GetConfig()->left == a.left);
        assert(LeverStore_Save(&b) == LEVER_STORE_OK); expect(&b);
    }
    for (cut=0;cut<8;++cut) {
        MockFlash_Reset(); MockFlash_FailAfter(cut);
        assert(LeverStore_Save(&a) != LEVER_STORE_OK);
        MockFlash_FailAfter(-1); assert(LeverStore_Load(&cfg) == LEVER_STORE_EMPTY);
        LeverCal_Init(); assert(!LeverCal_IsValid());
        assert(LeverStore_Save(&a) == LEVER_STORE_OK); expect(&a);
    }
    MockFlash_Reset(); assert(LeverStore_Save(&a) == LEVER_STORE_OK);
    MockFlash_AmbiguousMarker(true); assert(LeverStore_Save(&b) == LEVER_STORE_OK); expect(&b);
    MockFlash_AmbiguousMarker(false);
    MockFlash_Reset(); assert(LeverStore_Save(&a) == LEVER_STORE_OK);
    MockFlash_CorruptProgram(true); assert(LeverStore_Save(&b) == LEVER_STORE_VERIFY_ERROR); expect(&a);
    MockFlash_FailBegin(true); assert(LeverStore_Save(&b) == LEVER_STORE_IO_ERROR); expect(&a);
    MockFlash_FailBegin(false);
    /* Force sector rotation without performing thousands of unnecessary saves. */
    MockFlash_FillSector(0U); assert(LeverStore_Save(&b) == LEVER_STORE_OK); expect(&b);
    assert(MockFlash_Erases() == 0U); MockFlash_FillSector(1U);
    MockFlash_FailErase(true); assert(LeverStore_Save(&c) == LEVER_STORE_IO_ERROR); expect(&b);
    MockFlash_FailErase(false); assert(LeverStore_Save(&c) == LEVER_STORE_OK); expect(&c);
    assert(MockFlash_Erases() == 2U);
    /* Unsigned serial-number comparison across 32-bit wrap. */
    MockFlash_Reset(); assert(LeverStore_Save(&a) == LEVER_STORE_OK);
    sequence_at(base,0xFFFFFFFEUL); assert(LeverStore_Save(&b) == LEVER_STORE_OK);
    assert(MockFlash_Peek(base+32U+8U) == 0xFFFFFFFFUL);
    assert(LeverStore_Save(&c) == LEVER_STORE_OK); assert(MockFlash_Peek(base+64U+8U) == 0U); expect(&c);
    /* The public command path publishes pending calibration only AFTER durable success. */
    MockFlash_Reset(); LeverCal_Init(); draft(&a); cmd(10);
    assert(status[5] == LEVER_CAL_SAVING && LeverCal_SavePending() && !LeverCal_IsValid());
    assert(MockFlash_Writes() == 0U); LeverCal_ProcessSave(); LeverCal_WriteStatus(status);
    assert(status[5] == LEVER_CAL_OK && (status[6]&16U) && !(status[6]&64U)); expect(&a);
    LeverCal_Init(); assert(LeverCal_GetConfig()->left == a.left); LeverCal_WriteStatus(status);
    assert((status[6]&48U) == 48U && LeverCal_Convert(a.center) == 32768U);
    draft(&b); cmd(10); MockFlash_FailAfter(2); LeverCal_ProcessSave(); LeverCal_WriteStatus(status);
    assert(status[5] == LEVER_CAL_FLASH_ERROR && LeverCal_GetConfig()->left == a.left && (status[6]&2U));
    MockFlash_FailAfter(-1); cmd(10); LeverCal_ProcessSave(); expect(&b);
    LeverCal_Init(); assert(LeverCal_GetConfig()->invert && LeverCal_Convert(b.left) == 0U);
    draft(&c); cmd(5); assert(status[5] == 0U && !(status[6]&16U));
    cmd(9); assert(LeverCal_SavePending()); LeverCal_ProcessSave(); expect(&c);
    writes = MockFlash_Writes(); cmd(9); LeverCal_ProcessSave(); assert(MockFlash_Writes() == writes);
    draft(&a); cmd(10); cmd(6); LeverCal_ProcessSave(); expect(&c); assert(MockFlash_Writes() == writes);
    draft(&a); cmd(10); LeverCal_SaveNoticeTimedOut(); LeverCal_WriteStatus(status);
    assert(status[5] == LEVER_CAL_TIMEOUT && !LeverCal_SavePending() && MockFlash_Writes() == writes);
    MockFlash_Reset(); MockFlash_Supported(false); LeverCal_Init();
    assert(MockFlash_Reads() == 0U && !LeverCal_IsValid());
    draft(&a); cmd(10); assert(status[5] == LEVER_CAL_FLASH_UNSUPPORTED);
    cmd(5); cmd(9); assert(status[5] == LEVER_CAL_FLASH_UNSUPPORTED && MockFlash_Writes() == 0U);
    puts("PASS: Flash journal load/save, idempotence, corruption in all 8 words, invalid anchors with valid CRC, all 8 torn-write boundaries, failed/partial erase, sector rotation, sequence wrap, reboot restore, transactional save, cancel, timeout, unsupported MCU");
    return 0;
}
