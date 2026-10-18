#ifndef LEVER_CALIBRATION_H
#define LEVER_CALIBRATION_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LEVER_CAL_SAMPLES 200U
#define LEVER_CAL_MIN_HALF_SPAN 64U
#define LEVER_CAL_MAX_SPREAD 64U
#define LEVER_CAL_COMMAND_SIZE 63U /* Report ID already removed */
#define LEVER_CAL_STATUS_SIZE 29U  /* Input Report bytes 35..63 */
#define LEVER_CAL_COMMAND 0xC0U
typedef struct {
    uint16_t left, center, right;
    /* ADC counts on EACH side of center. Inclusive neutral band; remaining
       travel is rescaled continuously to the exact endpoints. Default zero. */
    uint16_t deadzone;
    uint8_t invert, valid;
} LeverCalibration_t;
typedef enum {
    LEVER_CAL_BEGIN = 1, LEVER_CAL_LEFT, LEVER_CAL_CENTER, LEVER_CAL_RIGHT,
    LEVER_CAL_COMMIT, LEVER_CAL_CANCEL, LEVER_CAL_QUERY, LEVER_CAL_DEADZONE,
    LEVER_CAL_SAVE, LEVER_CAL_COMMIT_SAVE
} LeverCal_Operation;
typedef enum {
    LEVER_CAL_OK = 0, LEVER_CAL_SAMPLING, LEVER_CAL_BAD_COMMAND,
    LEVER_CAL_NO_SESSION, LEVER_CAL_BUSY, LEVER_CAL_INCOMPLETE,
    LEVER_CAL_BAD_ANCHORS, LEVER_CAL_BAD_DEADZONE, LEVER_CAL_UNSTABLE,
    LEVER_CAL_ADC_ERROR, LEVER_CAL_TIMEOUT, LEVER_CAL_SAVING,
    LEVER_CAL_FLASH_ERROR, LEVER_CAL_NO_CONFIG, LEVER_CAL_FLASH_UNSUPPORTED,
    LEVER_CAL_FLASH_VERIFY_ERROR
} LeverCal_Result;
/* All APIs run in main context, never call from USB/ADC interrupts.
   Init loads the newest valid Flash record; Commit alone remains RAM-only. */
void LeverCal_Init(void);
void LeverCal_Begin(void);
void LeverCal_CaptureLeft(uint16_t adc);
void LeverCal_CaptureCenter(uint16_t adc);
void LeverCal_CaptureRight(uint16_t adc);
bool LeverCal_Commit(void);
void LeverCal_Cancel(void);
bool LeverCal_IsValid(void);
const LeverCalibration_t *LeverCal_GetConfig(void);
uint16_t LeverCal_Convert(uint16_t adc);
/* Normalised 63 bytes: C0 'L' 'C' 01 seq op paramLE16 zeros[55].
   Returns false for unrelated IO4 commands, without changing state. */
bool LeverCal_HandleCommand(const uint8_t *data, uint16_t len, uint32_t now);
/* At most one observation per millisecond; no catch-up samples/delays.
   adc_available is false if DMA/ADC has stopped or reported an error. */
void LeverCal_Task(uint16_t adc, uint32_t now, bool adc_available);
void LeverCal_WriteStatus(uint8_t status[LEVER_CAL_STATUS_SIZE]);
bool LeverCal_SavePending(void);
/* Call only AFTER a SAVING status report has completed its USB IN transfer.
   Synchronous HAL Flash operation may pause USB reporting during sector erase. */
void LeverCal_ProcessSave(void);
void LeverCal_SaveNoticeTimedOut(void);
#ifdef __cplusplus
}
#endif
#endif
