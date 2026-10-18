#include "lever_calibration.h"
#include "lever_storage.h"
#include <string.h>

static LeverCalibration_t active, working;
static uint8_t session, captured, point, seq, operation, result;
static uint8_t capture_seq, capture_op;
static uint16_t count, raw, sample_min, sample_max;
static uint32_t sum, started, sampled;
static LeverCalibration_t saved_config, save_candidate;
static uint8_t saved_valid, loaded_at_boot, flash_supported, save_pending;
static uint8_t save_seq, save_op;

static bool same_config(const LeverCalibration_t *a, const LeverCalibration_t *b)
{
    return a->valid && b->valid && a->left == b->left && a->center == b->center &&
           a->right == b->right && a->deadzone == b->deadzone;
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
void LeverCal_Init(void)
{
    LeverStore_Result stored;
    memset(&active, 0, sizeof(active));
    saved_valid = loaded_at_boot = save_pending = 0U;
    stored = LeverStore_Load(&saved_config);
    flash_supported = stored != LEVER_STORE_UNSUPPORTED ? 1U : 0U;
    if (stored == LEVER_STORE_OK) {
        active = saved_config; saved_valid = loaded_at_boot = 1U;
    }
    LeverCal_Cancel();
    seq = operation = result = 0U;
    raw = 0U;
}
void LeverCal_Begin(void)
{
    working = active;
    working.left = working.center = working.right = 0U;
    working.valid = working.invert = 0U;
    captured = point = 0U; count = 0U; session = 1U;
}
static void capture(uint8_t which, uint16_t adc)
{
    if (!session || point || save_pending || adc > 4095U) { return; }
    if (which == 1U) { working.left = adc; }
    if (which == 2U) { working.center = adc; }
    if (which == 4U) { working.right = adc; }
    captured |= which;
}
void LeverCal_CaptureLeft(uint16_t adc) { capture(1U, adc); }
void LeverCal_CaptureCenter(uint16_t adc) { capture(2U, adc); }
void LeverCal_CaptureRight(uint16_t adc) { capture(4U, adc); }
static uint8_t validate(void)
{
    int32_t l = (int32_t)working.center - working.left;
    int32_t r = (int32_t)working.right - working.center;
    if (!((l > 0 && r > 0) || (l < 0 && r < 0))) { return LEVER_CAL_BAD_ANCHORS; }
    if (l < 0) { l = -l; r = -r; }
    if (l < (int32_t)LEVER_CAL_MIN_HALF_SPAN ||
        r < (int32_t)LEVER_CAL_MIN_HALF_SPAN) { return LEVER_CAL_BAD_ANCHORS; }
    if (working.deadzone >= l || working.deadzone >= r) { return LEVER_CAL_BAD_DEADZONE; }
    return LEVER_CAL_OK;
}
bool LeverCal_Commit(void)
{
    if (!session || point || save_pending || captured != 7U || validate() != LEVER_CAL_OK) { return false; }
    working.invert = (working.left > working.right) ? 1U : 0U;
    working.valid = 1U; active = working; session = 0U;
    return true;
}
void LeverCal_Cancel(void)
{
    working = active; session = captured = point = save_pending = 0U; count = 0U;
    result = LEVER_CAL_OK;
}
bool LeverCal_IsValid(void) { return active.valid != 0U; }
const LeverCalibration_t *LeverCal_GetConfig(void) { return &active; }
uint16_t LeverCal_Convert(uint16_t adc)
{
    int32_t x, left_span, right_span, dz;
    if (adc > 4095U) { adc = 4095U; }
    if (!active.valid) { return (uint16_t)((uint32_t)adc * 65535U / 4095U); }
    /* Physical travel: left negative, right positive, also when inverted.
       Preserve the asymmetric 32768 / 32767 output halves. */
    x = (int32_t)adc - active.center;
    left_span = (int32_t)active.center - active.left;
    right_span = (int32_t)active.right - active.center;
    if (active.invert) { x = -x; left_span = -left_span; right_span = -right_span; }
    dz = active.deadzone;
    if (x <= -left_span) { return 0U; }
    if (x >= right_span) { return 65535U; }
    if (x >= -dz && x <= dz) { return 32768U; }
    if (x < -dz) { return (uint16_t)(((x + left_span) * 32768) / (left_span - dz)); }
    return (uint16_t)(32768 + ((x - dz) * 32767) / (right_span - dz));
}
bool LeverCal_HandleCommand(const uint8_t *d, uint16_t len, uint32_t now)
{
    uint16_t param, i;
    uint8_t op;
    if (d == 0 || len != LEVER_CAL_COMMAND_SIZE || d[0] != LEVER_CAL_COMMAND ||
        d[1] != 'L' || d[2] != 'C') { return false; }
    seq = d[4]; operation = op = d[5]; result = LEVER_CAL_BAD_COMMAND;
    if (d[3] != 1U) { return true; }
    for (i = 8U; i < len; ++i) { if (d[i] != 0U) { return true; } }
    param = (uint16_t)(d[6] | ((uint16_t)d[7] << 8));
    if (op < LEVER_CAL_BEGIN || op > LEVER_CAL_COMMIT_SAVE ||
        (op != LEVER_CAL_DEADZONE && param != 0U)) { return true; }
    result = LEVER_CAL_OK;
    if (op == LEVER_CAL_CANCEL) { LeverCal_Cancel(); return true; }
    if (op == LEVER_CAL_QUERY) { return true; }
    if (point || save_pending) { result = LEVER_CAL_BUSY; return true; }
    if (op == LEVER_CAL_BEGIN) { LeverCal_Begin(); return true; }
    if (op == LEVER_CAL_SAVE) {
        if (session) { result = LEVER_CAL_BUSY; return true; }
        if (!active.valid) { result = LEVER_CAL_NO_CONFIG; return true; }
        if (!flash_supported) { result = LEVER_CAL_FLASH_UNSUPPORTED; return true; }
        save_candidate = active;
        save_pending = 1U; save_seq = seq; save_op = op; result = LEVER_CAL_SAVING;
        return true;
    }
    if (!session) { result = LEVER_CAL_NO_SESSION; return true; }
    if (op == LEVER_CAL_DEADZONE) {
        if (param >= 4095U) { result = LEVER_CAL_BAD_DEADZONE; }
        else { working.deadzone = param; }
        return true;
    }
    if (op == LEVER_CAL_COMMIT || op == LEVER_CAL_COMMIT_SAVE) {
        if (captured != 7U) { result = LEVER_CAL_INCOMPLETE; return true; }
        result = validate();
        if (result == LEVER_CAL_OK) {
            if (op == LEVER_CAL_COMMIT) { (void)LeverCal_Commit(); }
            else if (!flash_supported) { result = LEVER_CAL_FLASH_UNSUPPORTED; }
            else {
                save_candidate = working;
                save_candidate.valid = 1U;
                save_candidate.invert = working.left > working.right ? 1U : 0U;
                save_pending = 1U; save_seq = seq; save_op = op; result = LEVER_CAL_SAVING;
            }
        }
        return true;
    }
    point = (uint8_t)(1U << (op - LEVER_CAL_LEFT));
    captured &= (uint8_t)~point; /* rejected recapture cannot reuse an old point */
    sum = 0U; count = 0U; sample_min = 4095U; sample_max = 0U;
    started = sampled = now; capture_seq = seq; capture_op = operation;
    result = LEVER_CAL_SAMPLING;
    return true;
}
static void finish(uint8_t code)
{
    point = 0U; seq = capture_seq; operation = capture_op; result = code;
}
void LeverCal_Task(uint16_t adc, uint32_t now, bool adc_available)
{
    uint8_t finished_point;
    raw = adc;
    if (!point) { return; }
    if (!adc_available || adc > 4095U) { finish(LEVER_CAL_ADC_ERROR); return; }
    if ((uint32_t)(now - started) > 2000U) { finish(LEVER_CAL_TIMEOUT); return; }
    if (now == sampled) { return; }
    sampled = now; sum += adc;
    if (adc < sample_min) { sample_min = adc; }
    if (adc > sample_max) { sample_max = adc; }
    ++count;
    if (count != LEVER_CAL_SAMPLES) { return; }
    finished_point = point;
    finish(LEVER_CAL_OK);
    if ((uint16_t)(sample_max - sample_min) > LEVER_CAL_MAX_SPREAD) { result = LEVER_CAL_UNSTABLE; return; }
    capture(finished_point, (uint16_t)((sum + LEVER_CAL_SAMPLES / 2U) / LEVER_CAL_SAMPLES));
}
void LeverCal_WriteStatus(uint8_t *s)
{
    s[0] = 'L'; s[1] = 'C'; s[2] = 1U;
    s[3] = seq; s[4] = operation; s[5] = result;
    s[6] = (uint8_t)((active.valid ? 1U : 0U) | (session ? 2U : 0U) |
                     (point ? 4U : 0U) | (active.invert ? 8U : 0U) |
                     ((saved_valid && same_config(&active, &saved_config)) ? 16U : 0U) |
                     (loaded_at_boot ? 32U : 0U) | (save_pending ? 64U : 0U) |
                     (flash_supported ? 128U : 0U));
    s[7] = captured; s[8] = point;
    put16(s + 9, count); put16(s + 11, raw);
    put16(s + 13, active.left); put16(s + 15, active.center);
    put16(s + 17, active.right); put16(s + 19, active.deadzone);
    put16(s + 21, working.left); put16(s + 23, working.center);
    put16(s + 25, working.right);
    s[27] = s[28] = 0U; /* main fills USB queue drop counter */
}

bool LeverCal_SavePending(void) { return save_pending != 0U; }

void LeverCal_SaveNoticeTimedOut(void)
{
    if (!save_pending) { return; }
    seq = save_seq; operation = save_op;
    save_pending = 0U; result = LEVER_CAL_TIMEOUT;
}

void LeverCal_ProcessSave(void)
{
    LeverStore_Result stored;
    if (!save_pending) { return; }
    stored = LeverStore_Save(&save_candidate);
    seq = save_seq; operation = save_op; save_pending = 0U;
    if (stored == LEVER_STORE_OK) {
        saved_config = save_candidate; saved_valid = 1U;
        if (save_op == LEVER_CAL_COMMIT_SAVE) {
            active = working = save_candidate; session = 0U;
        }
        result = LEVER_CAL_OK;
    } else if (stored == LEVER_STORE_UNSUPPORTED) { result = LEVER_CAL_FLASH_UNSUPPORTED; }
    else if (stored == LEVER_STORE_VERIFY_ERROR) { result = LEVER_CAL_FLASH_VERIFY_ERROR; }
    else { result = LEVER_CAL_FLASH_ERROR; }
}
