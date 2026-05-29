#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BRANCH_ID                 "SEN"
#define SENSOR_FW_VERSION         "1.0.0"

#define MAX_LINE_LEN              200
#define UPSTREAM_BAUD             230400

#define STM32_UART_INST           uart0
#define STM32_TX_PIN              12
#define STM32_RX_PIN              13
#define DEBUG_UART_INST           uart1
#define DEBUG_TX_PIN              16

#define PPS_GPIO                  10

#define I2C_BUS_INST              i2c0
#define I2C_SDA_PIN               4
#define I2C_SCL_PIN               5
#define I2C_BAUD                  400000

#define IMU_INT1_GPIO             6

#define EN_OUTPUT_PERIOD_US       100000
#define SB_OUTPUT_PERIOD_MS       10000
#define IMU_PERIOD_US             10000
#define BARO_PERIOD_MS            100
#define SCD41_PERIOD_MS           5000
#define SGP41_PERIOD_MS           1000
#define SCD41_VALIDITY_MS         6000
#define SGP41_CONDITIONING_MS     10000

#define ENV_RING_CAP              32

#define I2C_ADDR_ICM42688         0x68
#define I2C_ADDR_LIS3MDL          0x1C
#define I2C_ADDR_BMP390           0x77
#define I2C_ADDR_SCD41            0x62
#define I2C_ADDR_SGP41            0x59

#define TIME_QUALITY_PPS          0
#define TIME_QUALITY_DEGRADED     1

typedef struct {
    uint64_t local_timer_us;
    float    heading_deg;
    float    pitch_deg;
    float    roll_deg;
    float    ax, ay, az;
    float    mx, my, mz;
} env_sample_t;
