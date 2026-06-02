/*
 * MadgwickAHRS.h
 *
 * Implementation of Madgwick's IMU and AHRS algorithms.
 * Public-domain port of the canonical SOH Madgwick reference (2010).
 */

#ifndef MADGWICK_AHRS_H
#define MADGWICK_AHRS_H

#ifdef __cplusplus
extern "C" {
#endif

extern volatile float beta;
extern volatile float q0, q1, q2, q3;

void MadgwickAHRSupdate(float gx, float gy, float gz,
                        float ax, float ay, float az,
                        float mx, float my, float mz);

void MadgwickAHRSupdateIMU(float gx, float gy, float gz,
                           float ax, float ay, float az);

#ifdef __cplusplus
}
#endif

#endif
