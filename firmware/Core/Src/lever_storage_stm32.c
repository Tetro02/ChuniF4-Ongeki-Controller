#include "main.h"
#include "lever_storage.h"
#include "lever_storage_backend.h"

#if !defined(STM32F411xE)
#error This storage layout is exclusively for STM32F411CEU6 with 512 KiB Flash
#endif

static uint32_t cache_was_enabled;
volatile uint32_t lever_flash_hal_error;
#if defined(__ARMCC_VERSION)
extern const uint8_t app_load_end[] __asm("Load$$LR$$LR_IROM1$$Limit");
#elif defined(__ICCARM__)
extern const uint8_t __lever_app_flash_limit[];
#else
#error Add an explicit application-image boundary check for this toolchain
#endif

bool LeverStoreHW_Ready(void)
{
#if defined(__ARMCC_VERSION)
    if ((uintptr_t)app_load_end > LEVER_STORE_BASE) { return false; }
#elif defined(__ICCARM__)
    if ((uintptr_t)__lever_app_flash_limit != LEVER_STORE_BASE) { return false; }
#endif
    /* F411 device ID and factory Flash-size register, before any sector reads. */
    return (DBGMCU->IDCODE & 0xFFFU) == 0x431U &&
           *(volatile const uint16_t *)FLASHSIZE_BASE == 512U;
}
uint32_t LeverStoreHW_ReadWord(uint32_t address)
{
    return *(volatile const uint32_t *)(uintptr_t)address;
}
bool LeverStoreHW_Begin(void)
{
    lever_flash_hal_error = 0U;
    if (!LeverStoreHW_Ready() || __get_IPSR() != 0U || __get_PRIMASK() != 0U) { return false; }
    if (HAL_FLASH_Unlock() != HAL_OK) { return false; }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                          FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    cache_was_enabled = FLASH->ACR & FLASH_ACR_DCEN;
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __DSB(); __ISB();
    return true;
}
void LeverStoreHW_End(void)
{
    __HAL_FLASH_DATA_CACHE_RESET();
    if (cache_was_enabled != 0U) { __HAL_FLASH_DATA_CACHE_ENABLE(); }
    (void)HAL_FLASH_Lock();
    __DSB(); __ISB();
}
bool LeverStoreHW_Erase(uint8_t sector_index)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t failed = 0xFFFFFFFFUL;
    HAL_StatusTypeDef status;
    if (sector_index > 1U) { return false; }
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = sector_index == 0U ? FLASH_SECTOR_6 : FLASH_SECTOR_7;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3; /* board VDD must be 2.7..3.6 V */
    status = HAL_FLASHEx_Erase(&erase, &failed);
    lever_flash_hal_error = HAL_FLASH_GetError();
    return status == HAL_OK && failed == 0xFFFFFFFFUL;
}
bool LeverStoreHW_Program(uint32_t address, uint32_t value)
{
    HAL_StatusTypeDef status;
    if (address < LEVER_STORE_BASE || address >= LEVER_STORE_END || (address & 3U) != 0U) { return false; }
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, value);
    lever_flash_hal_error = HAL_FLASH_GetError();
    __DSB(); __ISB();
    return status == HAL_OK;
}
