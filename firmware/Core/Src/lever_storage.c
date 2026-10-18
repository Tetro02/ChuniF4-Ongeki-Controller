#include "lever_storage.h"
#include "lever_storage_backend.h"

#define STORE_MAGIC  0x4C43414CUL
#define STORE_SCHEMA 0x00010020UL /* version 1, 32 bytes */
#define STORE_COMMIT 0xC011CA1BUL

typedef struct {
    bool found;
    uint32_t sequence;
    uint8_t sector;
    uint32_t next[2];
    LeverCalibration_t config;
} StoreScan;

static uint32_t crc_words(const uint32_t *words)
{
    uint32_t crc = 0xFFFFFFFFUL, i, j, byte;
    for (i = 0U; i < 6U; ++i) {
        for (byte = 0U; byte < 4U; ++byte) {
            crc ^= (words[i] >> (byte * 8U)) & 0xFFU;
            for (j = 0U; j < 8U; ++j) {
                crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
            }
        }
    }
    return ~crc;
}

bool LeverStore_Validate(const LeverCalibration_t *c)
{
    int32_t l, r;
    if (c == 0 || !c->valid || c->left > 4095U || c->center > 4095U || c->right > 4095U) { return false; }
    l = (int32_t)c->center - c->left;
    r = (int32_t)c->right - c->center;
    if (!((l > 0 && r > 0) || (l < 0 && r < 0))) { return false; }
    if (l < 0) { l = -l; r = -r; }
    return l >= (int32_t)LEVER_CAL_MIN_HALF_SPAN && r >= (int32_t)LEVER_CAL_MIN_HALF_SPAN &&
           c->deadzone < l && c->deadzone < r;
}

static bool same(const LeverCalibration_t *a, const LeverCalibration_t *b)
{
    return a->left == b->left && a->center == b->center &&
           a->right == b->right && a->deadzone == b->deadzone;
}

static bool decode(const uint32_t *w, LeverCalibration_t *c)
{
    if (w[0] != STORE_MAGIC || w[1] != STORE_SCHEMA || w[5] != 0U ||
        w[7] != STORE_COMMIT || w[6] != crc_words(w)) { return false; }
    c->left = (uint16_t)w[3]; c->center = (uint16_t)(w[3] >> 16U);
    c->right = (uint16_t)w[4]; c->deadzone = (uint16_t)(w[4] >> 16U);
    c->valid = 1U; c->invert = (c->left > c->right) ? 1U : 0U;
    return LeverStore_Validate(c);
}

static bool read_record(uint32_t address, uint32_t *w)
{
    bool blank = true;
    uint32_t i;
    for (i = 0U; i < 8U; ++i) {
        w[i] = LeverStoreHW_ReadWord(address + i * 4U);
        if (w[i] != 0xFFFFFFFFUL) { blank = false; }
    }
    return blank;
}

static void scan(StoreScan *s)
{
    uint32_t slot, w[8], address, delta;
    uint8_t sector;
    LeverCalibration_t c;
    s->found = false; s->sequence = 0U; s->sector = 0U;
    s->next[0] = s->next[1] = 0U;
    for (sector = 0U; sector < 2U; ++sector) {
        for (slot = 0U; slot < LEVER_STORE_SLOTS; ++slot) {
            address = LEVER_STORE_BASE + sector * LEVER_STORE_SECTOR_SIZE + slot * LEVER_STORE_RECORD_SIZE;
            if (read_record(address, w)) { continue; }
            /* Never reuse any partially written slot, even if its marker is absent. */
            s->next[sector] = slot + 1U;
            if (!decode(w, &c)) { continue; }
            delta = w[2] - s->sequence;
            if (!s->found || (delta != 0U && delta < 0x80000000UL)) {
                s->found = true; s->sequence = w[2]; s->sector = sector; s->config = c;
            }
        }
    }
}

LeverStore_Result LeverStore_Load(LeverCalibration_t *c)
{
    StoreScan s;
    if (c == 0) { return LEVER_STORE_BAD_CONFIG; }
    if (!LeverStoreHW_Ready()) { return LEVER_STORE_UNSUPPORTED; }
    scan(&s);
    if (!s.found) { return LEVER_STORE_EMPTY; }
    *c = s.config;
    return LEVER_STORE_OK;
}

LeverStore_Result LeverStore_Save(const LeverCalibration_t *c)
{
    StoreScan s;
    uint32_t w[8], readback[8], address, i, slot;
    uint8_t target;
    bool erase = false;
    LeverCalibration_t verified;
    LeverStore_Result outcome = LEVER_STORE_IO_ERROR;
    if (!LeverStore_Validate(c)) { return LEVER_STORE_BAD_CONFIG; }
    if (!LeverStoreHW_Ready()) { return LEVER_STORE_UNSUPPORTED; }
    scan(&s);
    if (s.found && same(c, &s.config)) { return LEVER_STORE_OK; }
    target = s.found ? s.sector : 0U;
    if (s.next[target] >= LEVER_STORE_SLOTS) {
        target = (uint8_t)(1U - target);
        erase = s.next[target] != 0U;
    }
    slot = erase ? 0U : s.next[target];
    if (slot >= LEVER_STORE_SLOTS) { return LEVER_STORE_IO_ERROR; }
    address = LEVER_STORE_BASE + target * LEVER_STORE_SECTOR_SIZE + slot * LEVER_STORE_RECORD_SIZE;
    w[0] = STORE_MAGIC; w[1] = STORE_SCHEMA; w[2] = s.found ? s.sequence + 1U : 1U;
    w[3] = c->left | ((uint32_t)c->center << 16U);
    w[4] = c->right | ((uint32_t)c->deadzone << 16U);
    w[5] = 0U; w[6] = crc_words(w); w[7] = STORE_COMMIT;
    if (!LeverStoreHW_Begin()) { return LEVER_STORE_IO_ERROR; }
    if (erase) {
        if (!LeverStoreHW_Erase(target)) { goto done; }
        /* Read every word, not just the destination record, after a sector erase. */
        for (i = 0U; i < LEVER_STORE_SECTOR_SIZE; i += 4U) {
            if (LeverStoreHW_ReadWord(LEVER_STORE_BASE + target * LEVER_STORE_SECTOR_SIZE + i) != 0xFFFFFFFFUL) {
                outcome = LEVER_STORE_VERIFY_ERROR; goto done;
            }
        }
    }
    if (!read_record(address, readback)) { outcome = LEVER_STORE_VERIFY_ERROR; goto done; }
    for (i = 0U; i < 7U; ++i) {
        if (!LeverStoreHW_Program(address + i * 4U, w[i])) { goto done; }
        if (LeverStoreHW_ReadWord(address + i * 4U) != w[i]) { outcome = LEVER_STORE_VERIFY_ERROR; goto done; }
    }
    /* Commit only after the entire body has been read back, CRC included. */
    (void)read_record(address, readback);
    for (i = 0U; i < 7U; ++i) {
        if (w[i] != readback[i]) { outcome = LEVER_STORE_VERIFY_ERROR; goto done; }
    }
    /* A backend may report an error after the marker physically completed.
       The complete validated record, rather than that return code, is decisive. */
    (void)LeverStoreHW_Program(address + 28U, STORE_COMMIT);
    (void)read_record(address, readback);
    if (decode(readback, &verified) && readback[2] == w[2] && same(&verified, c)) {
        outcome = LEVER_STORE_OK;
    } else { outcome = LEVER_STORE_VERIFY_ERROR; }
done:
    LeverStoreHW_End();
    return outcome;
}
