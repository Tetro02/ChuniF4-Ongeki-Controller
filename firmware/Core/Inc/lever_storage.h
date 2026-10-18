#ifndef LEVER_STORAGE_H
#define LEVER_STORAGE_H
#include "lever_calibration.h"

/* STM32F411CEU6, 512 KiB. Both linkers reserve these two 128 KiB sectors. */
#define LEVER_STORE_BASE       0x08040000UL
#define LEVER_STORE_SECTOR_SIZE 0x00020000UL
#define LEVER_STORE_END        0x08080000UL
#define LEVER_STORE_RECORD_SIZE 32U
#define LEVER_STORE_SLOTS      (LEVER_STORE_SECTOR_SIZE / LEVER_STORE_RECORD_SIZE)

typedef enum {
    LEVER_STORE_OK = 0, LEVER_STORE_EMPTY, LEVER_STORE_IO_ERROR,
    LEVER_STORE_VERIFY_ERROR, LEVER_STORE_UNSUPPORTED, LEVER_STORE_BAD_CONFIG
} LeverStore_Result;

bool LeverStore_Validate(const LeverCalibration_t *config);
/* Synchronous, main context only. Load never writes/erases any Flash. */
LeverStore_Result LeverStore_Load(LeverCalibration_t *config);
/* Idempotent for identical settings; commit marker is programmed last. */
LeverStore_Result LeverStore_Save(const LeverCalibration_t *config);

#endif
