#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef MKII_STM32_UNIT
#define MKII_STM32_UNIT 1
#endif

#ifndef MKII_FW_VERSION
#define MKII_FW_VERSION "1.0.1"
#endif

#define MAX_LINE_LEN              200
#define BRANCH_UART_BAUD          230400
#define STM32_AG_HB_INTERVAL_MS   10000
#define PPS_TIMEOUT_US            2000000ULL
#define TM_EMIT_BUDGET_MS         100
#define SD_FLUSH_INTERVAL_MS      1000

#define BRANCH_RX_RING_SZ         1024

#if MKII_STM32_UNIT == 1
#define N_BRANCH_UARTS            5
#define BRANCH_IDX_W24            0
#define BRANCH_IDX_W5G            1
#define BRANCH_IDX_BLE            2
#define BRANCH_IDX_DOT154         3
#define BRANCH_IDX_SEN            4
#define UNIT_ID_STR               "1"
#elif MKII_STM32_UNIT == 2
#define N_BRANCH_UARTS            4
#define BRANCH_IDX_MTC            0
#define BRANCH_IDX_VHF            1
#define BRANCH_IDX_UHF            2
#define BRANCH_IDX_FPV            3
#define UNIT_ID_STR               "2"
#else
#error "MKII_STM32_UNIT must be 1 or 2"
#endif

#define MODE_STANDALONE 0
#define MODE_CONNECTED  1

#define TIME_QUALITY_PPS      0
#define TIME_QUALITY_DEGRADED 1

extern const char *const branch_names[N_BRANCH_UARTS];
