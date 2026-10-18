#ifndef MOCK_FLASH_H
#define MOCK_FLASH_H
#include <stdint.h>
#include <stdbool.h>
void MockFlash_Reset(void);
void MockFlash_Supported(bool value);
void MockFlash_FailAfter(int words);
void MockFlash_FailErase(bool value);
void MockFlash_FailBegin(bool value);
void MockFlash_CorruptProgram(bool value);
void MockFlash_AmbiguousMarker(bool value);
void MockFlash_FillSector(uint8_t sector);
void MockFlash_Poke(uint32_t address, uint32_t value);
uint32_t MockFlash_Peek(uint32_t address);
uint32_t MockFlash_Writes(void);
uint32_t MockFlash_Erases(void);
uint32_t MockFlash_Reads(void);
#endif
