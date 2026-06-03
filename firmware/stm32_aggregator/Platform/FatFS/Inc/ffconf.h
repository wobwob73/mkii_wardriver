/*
 * ffconf.h — FatFS R0.12c configuration (matches the version bundled
 * with STM32CubeH7 v1.11.x). Trimmed to what MKII sd_log uses: one
 * drive, long filenames in static buffers, read+write, 512-byte sector
 * size only (SD cards).
 *
 * Note: this file uses the underscore-prefix macros required by R0.12c.
 * If the STM32CubeH7 tag is bumped to a version that ships a newer
 * FatFS (R0.14+), regenerate this file with FF_-prefix macros.
 */
#pragma once

#define _FFCONF       68300
#define _FS_READONLY  0
#define _FS_MINIMIZE  0
#define _USE_STRFUNC  0
#define _USE_FIND     0
#define _USE_MKFS     0
#define _USE_FASTSEEK 0
#define _USE_EXPAND   0
#define _USE_CHMOD    0
#define _USE_LABEL    0
#define _USE_FORWARD  0

#define _CODE_PAGE    437
#define _USE_LFN      1
#define _MAX_LFN      128
#define _LFN_UNICODE  0
#define _STRF_ENCODE  3
#define _FS_RPATH     0

#define _VOLUMES      1
#define _STR_VOLUME_ID 0
#define _VOLUME_STRS  "RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define _MULTI_PARTITION 0
#define _MIN_SS       512
#define _MAX_SS       512
#define _USE_TRIM     0
#define _FS_NOFSINFO  0

#define _FS_TINY      0
#define _FS_EXFAT     0
#define _FS_NORTC     1
#define _NORTC_MON    5
#define _NORTC_MDAY   29
#define _NORTC_YEAR   2026
#define _FS_LOCK      0
#define _FS_REENTRANT 0
#define _FS_TIMEOUT   1000
#define _SYNC_t       HANDLE
#define _WORD_ACCESS  0
