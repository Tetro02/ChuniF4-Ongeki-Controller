#ifndef LEVER_STORAGE_BACKEND_H
#define LEVER_STORAGE_BACKEND_H
#include <stdint.h>
#include <stdbool.h>
/* Backend is replaceable by a NOR Flash model for fault-injection tests. */
bool LeverStoreHW_Ready(void);
uint32_t LeverStoreHW_ReadWord(uint32_t address);
bool LeverStoreHW_Begin(void);
void LeverStoreHW_End(void);
bool LeverStoreHW_Erase(uint8_t sector_index); /* 0 => sector6, 1 => sector7 */
bool LeverStoreHW_Program(uint32_t address, uint32_t value);
#endif
