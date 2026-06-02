#include "core0_fusion.h"
#include "queues.h"
#include "icm42688.h"
#include "lis3mdl.h"
#include "MadgwickAHRS.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <math.h>
#include <string.h>

extern volatile float beta;
extern volatile float q0, q1, q2, q3;
extern volatile float madgwick_sample_freq;

static bool g_imu_ok = false;
static bool g_mag_ok = false;

void core0_fusion_init(bool imu_ok, bool mag_ok) {
    g_imu_ok = imu_ok;
    g_mag_ok = mag_ok;
    beta = 0.1f;
    q0 = 1.0f; q1 = 0.0f; q2 = 0.0f; q3 = 0.0f;
    madgwick_sample_freq = 1000000.0f / (float)IMU_PERIOD_US;
}

static void quaternion_to_euler(float *heading_deg, float *pitch_deg, float *roll_deg) {
    float sinr_cosp = 2.0f * (q0 * q1 + q2 * q3);
    float cosr_cosp = 1.0f - 2.0f * (q1 * q1 + q2 * q2);
    float roll = atan2f(sinr_cosp, cosr_cosp);

    float sinp = 2.0f * (q0 * q2 - q3 * q1);
    float pitch;
    if (sinp >= 1.0f) pitch = (float)M_PI / 2.0f;
    else if (sinp <= -1.0f) pitch = -(float)M_PI / 2.0f;
    else pitch = asinf(sinp);

    float siny_cosp = 2.0f * (q0 * q3 + q1 * q2);
    float cosy_cosp = 1.0f - 2.0f * (q2 * q2 + q3 * q3);
    float yaw = atan2f(siny_cosp, cosy_cosp);

    const float r2d = 180.0f / (float)M_PI;
    *roll_deg = roll * r2d;
    *pitch_deg = pitch * r2d;
    float h = yaw * r2d;
    if (h < 0) h += 360.0f;
    if (h >= 360.0f) h -= 360.0f;
    *heading_deg = h;
}

void core0_fusion_entry(void) {
    uint64_t next_tick_us = time_us_64() + IMU_PERIOD_US;
    uint32_t decim = 0;
    float last_mx = 0, last_my = 0, last_mz = 0;
    uint32_t mag_decim = 0;
    env_sample_t latest_mag_sample = {0};
    (void)latest_mag_sample;

    while (1) {
        uint64_t now = time_us_64();
        if (now < next_tick_us) {
            sleep_us((uint32_t)(next_tick_us - now));
        }
        next_tick_us += IMU_PERIOD_US;

        float ax = 0, ay = 0, az = 0;
        float gx = 0, gy = 0, gz = 0;
        bool imu_read_ok = false;
        if (g_imu_ok) {
            imu_read_ok = icm42688_read(&ax, &ay, &az, &gx, &gy, &gz);
        }

        if (g_mag_ok && (mag_decim++ % 10 == 0)) {
            lis3mdl_read(&last_mx, &last_my, &last_mz);
        }

        if (imu_read_ok) {
            MadgwickAHRSupdate(gx, gy, gz, ax, ay, az,
                               last_mx, last_my, last_mz);
        }

        if ((decim++ % 10) == 0) {
            env_sample_t s;
            s.local_timer_us = time_us_64();
            s.ax = ax; s.ay = ay; s.az = az;
            s.mx = last_mx; s.my = last_my; s.mz = last_mz;
            quaternion_to_euler(&s.heading_deg, &s.pitch_deg, &s.roll_deg);
            env_q_push(&s);
        }
    }
}
