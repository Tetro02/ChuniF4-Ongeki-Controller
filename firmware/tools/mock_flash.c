#include "lever_storage.h"
#include "lever_storage_backend.h"
#include "mock_flash.h"
#include <assert.h>
#include <string.h>

static uint32_t flash[2U * LEVER_STORE_SECTOR_SIZE / 4U];
static bool initialized, supported = true, fail_erase, fail_begin, corrupt, ambiguous, unlocked;
static int fail_after = -1;
static uint32_t writes, erases, reads;
static uint32_t index_at(uint32_t address)
{
    assert(address >= LEVER_STORE_BASE && address < LEVER_STORE_END && (address & 3U) == 0U);
    return (address - LEVER_STORE_BASE) / 4U;
}
void MockFlash_Reset(void)
{
    memset(flash, 0xFF, sizeof(flash)); initialized = supported = true;
    fail_erase = fail_begin = corrupt = ambiguous = unlocked = false;
    writes = erases = reads = 0U; fail_after = -1;
}
void MockFlash_Supported(bool value) { supported = value; }
void MockFlash_FailAfter(int words) { fail_after = words; }
void MockFlash_FailErase(bool value) { fail_erase = value; }
void MockFlash_FailBegin(bool value) { fail_begin = value; }
void MockFlash_CorruptProgram(bool value) { corrupt = value; }
void MockFlash_AmbiguousMarker(bool value) { ambiguous = value; }
void MockFlash_Poke(uint32_t a, uint32_t v) { flash[index_at(a)] = v; }
uint32_t MockFlash_Peek(uint32_t a) { return flash[index_at(a)]; }
uint32_t MockFlash_Writes(void) { return writes; }
uint32_t MockFlash_Erases(void) { return erases; }
uint32_t MockFlash_Reads(void) { return reads; }
void MockFlash_FillSector(uint8_t sector)
{
    uint32_t slot, i, base = sector * LEVER_STORE_SECTOR_SIZE / 4U;
    for (slot = 0U; slot < LEVER_STORE_SLOTS; ++slot) {
        bool blank = true;
        for (i = 0U; i < 8U; ++i) { if (flash[base + slot*8U + i] != 0xFFFFFFFFUL) { blank = false; } }
        if (blank) { flash[base + slot*8U] = 0U; }
    }
}
bool LeverStoreHW_Ready(void)
{
    if (!initialized) { MockFlash_Reset(); }
    return supported;
}
uint32_t LeverStoreHW_ReadWord(uint32_t address)
{
    assert(supported); ++reads;
    return flash[index_at(address)];
}
bool LeverStoreHW_Begin(void)
{
    if (fail_begin) { return false; }
    assert(!unlocked); unlocked = true; return true;
}
void LeverStoreHW_End(void) { assert(unlocked); unlocked = false; }
bool LeverStoreHW_Erase(uint8_t sector)
{
    assert(unlocked && sector < 2U); ++erases;
    memset(&flash[sector * LEVER_STORE_SECTOR_SIZE / 4U], 0xFF,
           fail_erase ? LEVER_STORE_SECTOR_SIZE/2U : LEVER_STORE_SECTOR_SIZE);
    return !fail_erase;
}
bool LeverStoreHW_Program(uint32_t address, uint32_t value)
{
    uint32_t *word = &flash[index_at(address)];
    assert(unlocked); ++writes;
    assert((*word & value) == value); /* NOR Flash cannot change a 0 back to 1 */
    if (fail_after == 0) { *word &= value | 0xFFFF0000UL; return false; }
    if (fail_after > 0) { --fail_after; }
    *word &= value;
    if (corrupt && value != 0U) { *word &= value - 1U; corrupt = false; }
    return !(ambiguous && (address & 31U) == 28U);
}
