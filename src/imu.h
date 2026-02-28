#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────────────────────
// MPU6050 IMU driver – used for tilt (elevation) feedback
// I2C address: 0x68 (AD0 pin LOW) or 0x69 (AD0 pin HIGH)
//
// Mounting orientation (CRITICAL – match this exactly):
//
//   Fix the MPU6050 to the tilt/elevation platform with:
//     X axis → pointing forward along the antenna barrel
//     Z axis → pointing upward when the antenna is horizontal (0° elevation)
//
//   Illustration (side view):
//
//     antenna → =====[MPU6050]====>
//                      X→  Z↑
//
//   Elevation formula used:
//     elev = atan2(accel_z, accel_x) * RAD_TO_DEG
//
//   At 0° elevation:  gravity ≈ −Z  →  az ≈ −1g, ax ≈ 0   → atan2(−1,0) = −90°?
//   Hmm, let's think again.
//   At 0° elevation (horizontal):   gravity is downward = −Z direction of sensor
//     ax ≈ 0,  ay ≈ 0,  az ≈ −16384  (pointing down = −Z)
//   At 90° elevation (vertical up):  gravity is downward = −X direction of sensor
//     ax ≈ −16384,  ay ≈ 0,  az ≈ 0
//
//   elevation = atan2(−az, −ax) * RAD_TO_DEG  gives:
//     horizontal:  atan2(16384,  0)     = 90°  ← wrong, we want 0°
//
//   Simpler: elevation = atan2(accel_x, −accel_z) * RAD_TO_DEG
//     horizontal:  atan2(0,  16384)  = 0°   ✓
//     vertical:    atan2(−16384, 0)  = −90° ✗  (sign depends on which way X tilts)
//
//   With X pointing forward and antenna tilting UP means X tilts toward gravity:
//     vertical up: ax ≈ −16384
//     elevation = atan2(−ax, −az) * RAD_TO_DEG
//     horizontal: atan2(0,  16384) = 0°   ✓
//     vertical:   atan2(16384, 0)  = 90°  ✓
//
//   See ELEV_FORMULA below.
// ─────────────────────────────────────────────────────────────────────────────

class IMU {
public:
    // Returns false if sensor not found on I2C bus.
    bool begin() {
        Wire.beginTransmission(MPU_ADDR);
        if (Wire.endTransmission() != 0) return false;

        // Wake up the MPU6050 (clear SLEEP bit in PWR_MGMT_1)
        _writeReg(REG_PWR_MGMT_1, 0x00u);
        delay(50);

        // Accelerometer range: ±2g (0x00) – maximises resolution for slow tilt
        _writeReg(REG_ACCEL_CONFIG, 0x00u);
        delay(10);

        return true;
    }

    // Returns elevation angle in degrees [MIN_TILT_DEG, MAX_TILT_DEG]
    // 0° = antenna horizontal, 90° = pointing straight up
    float getElevation() {
        int16_t ax, ay, az;
        _readAccel(ax, ay, az);

        // See mounting note above for derivation
        float elev = atan2f(static_cast<float>(-ax),
                            static_cast<float>(-az)) * RAD_TO_DEG;

        return constrain(elev, MIN_TILT_DEG, MAX_TILT_DEG);
    }

private:
    static constexpr uint8_t MPU_ADDR       = 0x68u;
    static constexpr uint8_t REG_ACCEL_CONFIG = 0x1Cu;
    static constexpr uint8_t REG_ACCEL_XOUT  = 0x3Bu;  // first accel register
    static constexpr uint8_t REG_PWR_MGMT_1  = 0x6Bu;

    void _writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }

    // Reads raw 16-bit signed accelerometer values (big-endian, MSB first)
    void _readAccel(int16_t &ax, int16_t &ay, int16_t &az) {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(REG_ACCEL_XOUT);
        Wire.endTransmission(false);
        Wire.requestFrom(MPU_ADDR, 6u);

        if (Wire.available() >= 6) {
            ax = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
            ay = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
            az = static_cast<int16_t>((Wire.read() << 8) | Wire.read());
        } else {
            ax = ay = az = 0;
        }
    }
};
