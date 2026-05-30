/*
 * sd_diskio.c — FatFS disk I/O glue for SDMMC1 on STM32H7.
 *
 * Minimal polled-mode implementation: each FatFS read/write goes through
 * HAL_SD_ReadBlocks / HAL_SD_WriteBlocks with a timeout. SDMMC IRQ is
 * enabled (see Core/Src/stm32h7xx_hal_msp.c) so HAL_SD_GetCardState
 * polls don't deadlock if the card stalls.
 */

#include "ff.h"
#include "diskio.h"
#include "main.h"

#define SD_TIMEOUT 5000U

static volatile DSTATUS Stat = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return Stat;
}

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    Stat = STA_NOINIT;
    /* HAL_SD_Init is done from pal_sd_mount before f_mount calls back into
     * disk_initialize. If we got here, SD state should be READY. */
    if (HAL_SD_GetState(&hsd1) == HAL_SD_STATE_READY) {
        Stat &= ~STA_NOINIT;
    }
    return Stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    (void)pdrv;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (HAL_SD_ReadBlocks(&hsd1, (uint8_t *)buff, sector, count, SD_TIMEOUT) != HAL_OK) {
        return RES_ERROR;
    }
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > SD_TIMEOUT) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
    (void)pdrv;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)buff, sector, count, SD_TIMEOUT) != HAL_OK) {
        return RES_ERROR;
    }
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > SD_TIMEOUT) return RES_ERROR;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void)pdrv;
    if (Stat & STA_NOINIT) return RES_NOTRDY;
    HAL_SD_CardInfoTypeDef info;
    DRESULT res = RES_ERROR;
    switch (cmd) {
        case CTRL_SYNC:
            res = RES_OK;
            break;
        case GET_SECTOR_COUNT:
            HAL_SD_GetCardInfo(&hsd1, &info);
            *(DWORD *)buff = info.LogBlockNbr;
            res = RES_OK;
            break;
        case GET_SECTOR_SIZE:
            HAL_SD_GetCardInfo(&hsd1, &info);
            *(WORD *)buff = info.LogBlockSize;
            res = RES_OK;
            break;
        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1;
            res = RES_OK;
            break;
        default:
            res = RES_PARERR;
            break;
    }
    return res;
}

DWORD get_fattime(void) {
    /* FatFS timestamp — return a constant; the SD log path uses the
     * session_id from app_main for human-readable directory naming. */
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)5 << 21) | ((DWORD)29 << 16)
         | ((DWORD)0 << 11) | ((DWORD)0 << 5) | ((DWORD)0 >> 1);
}
