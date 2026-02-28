#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────────────────────
// QMC5883L 3-axis magnetometer driver (I2C address 0x0D)
//
// Used for closed-loop pan (azimuth) feedback.
// Mount the QMC5883L FLAT and LEVEL on the rotating pan platform so the
// X and Y axes remain horizontal as the tracker rotates.
//
// Calibration:
//   Hard-iron offsets (COMPASS_OFFSET_X / Y in config.h) remove DC bias
//   from nearby magnets and ferrous metal.  To calibrate: rotate the
//   tracker slowly through a full 360°, note the min/max of X and Y,
//   then set offsets = (max + min) / 2 for each axis.
// ─────────────────────────────────────────────────────────────────────────────

class Compass {
public:
    // Returns false if the sensor is not found on the I2C bus.
    bool begin() {
        Wire.beginTransmission(QMC_ADDR);
        if (Wire.endTransmission() != 0) return false;

        // Set/Reset period register – must be written before CR1
        _writeReg(REG_SET_RESET, 0x01);

        // Control register 1:
        //   OSR = 512 (0b11 << 6), RNG = 8 G (0b01 << 4),
        //   ODR = 200 Hz (0b11 << 2), MODE = Continuous (0b01)
        _writeReg(REG_CTRL1, 0x1Du);

        // Control register 2: disable INT pin, disable pointer roll-over
        _writeReg(REG_CTRL2, 0x40u);

        delay(10);
        return true;
    }

    // Returns compass heading in degrees [0, 360)
    // 0° = North, 90° = East, 180° = South, 270° = West
    float getHeading() {
        int16_t x, y, z;
        _readRaw(x, y, z);

        // Apply hard-iron calibration offsets
        float fx = static_cast<float>(x) - COMPASS_OFFSET_X;
        float fy = static_cast<float>(y) - COMPASS_OFFSET_Y;

        float heading = atan2f(fy, fx) * RAD_TO_DEG;
        heading += COMPASS_DECLINATION;

        // Normalise to [0, 360)
        if (heading <   0.0f) heading += 360.0f;
        if (heading >= 360.0f) heading -= 360.0f;

        return heading;
    }

private:
    static constexpr uint8_t QMC_ADDR     = 0x0Du;
    static constexpr uint8_t REG_DATA     = 0x00u;  // first data register (X_LSB)
    static constexpr uint8_t REG_CTRL1    = 0x09u;
    static constexpr uint8_t REG_CTRL2    = 0x0Au;
    static constexpr uint8_t REG_SET_RESET= 0x0Bu;

    void _writeReg(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(QMC_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }

    void _readRaw(int16_t &x, int16_t &y, int16_t &z) {
        Wire.beginTransmission(QMC_ADDR);
        Wire.write(REG_DATA);
        Wire.endTransmission(false);          // restart (no stop bit)
        Wire.requestFrom(QMC_ADDR, 6u);

        if (Wire.available() >= 6) {
            uint8_t xl = Wire.read(), xh = Wire.read();
            uint8_t yl = Wire.read(), yh = Wire.read();
            uint8_t zl = Wire.read(), zh = Wire.read();

            x = static_cast<int16_t>((xh << 8) | xl);
            y = static_cast<int16_t>((yh << 8) | yl);
            z = static_cast<int16_t>((zh << 8) | zl);
        } else {
            x = y = z = 0;
        }
    }
};
