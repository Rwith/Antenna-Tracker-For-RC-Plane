#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"
#include "tracker_math.h"

// ─────────────────────────────────────────────────────────────────────────────
// Servo controller
//
// Pan axis  – 360° continuous rotation servo (speed-controlled)
//   Closed-loop P-controller using QMC5883L compass heading as feedback.
//   PWM > SERVO_STOP → clockwise
//   PWM < SERVO_STOP → counter-clockwise
//   PWM = SERVO_STOP → stopped
//
// Tilt axis – 180° standard positional servo (angle-controlled)
//   No IMU or external sensor required.  Because we command the servo to a
//   specific PWM value, the resulting angle is fully determined by that value.
//   getTiltAngle() reverses the linear PWM→angle mapping to report the current
//   tilt angle without any additional hardware.
//   TILT_MIN_PWM µs → MIN_TILT_DEG (antenna horizontal)
//   TILT_MAX_PWM µs → MAX_TILT_DEG (antenna near-vertical)
//   Both endpoints are configurable in config.h.
// ─────────────────────────────────────────────────────────────────────────────

class ServoController {
public:
    void begin() {
        _panServo.attach(PAN_SERVO_PIN,   900, 2100);
        _tiltServo.attach(TILT_SERVO_PIN, 500, 2500);  // wider range for 180° servos
        stop();
    }

    // Set the desired pan (azimuth, degrees) and tilt (elevation, degrees).
    void setTarget(float panDeg, float tiltDeg) {
        _targetPan  = panDeg;
        _targetTilt = constrain(tiltDeg, MIN_TILT_DEG, MAX_TILT_DEG);
    }

    // Call every loop iteration with the current compass heading.
    // Tilt is written directly – no sensor reading required.
    void update(float currentPan) {
        float panError = normalizeAngleDiff(_targetPan - currentPan);
        _writePan(_computePanPWM(panError));
        _writeTiltAngle(_targetTilt);
    }

    // Stop pan servo; tilt holds its current position (positional servo).
    void stop() {
        _writePan(SERVO_STOP);
        // 180° servo holds position passively – no explicit stop needed
    }

    // Returns the current tilt elevation in degrees, derived from the last
    // commanded PWM.  No IMU or sensor needed – the 180° servo's position is
    // fully determined by the PWM we sent it.
    float getTiltAngle() const { return _targetTilt; }

private:
    Servo _panServo;
    Servo _tiltServo;

    float _targetPan  = 0.0f;
    float _targetTilt = 0.0f;

    // Pan: proportional speed controller for the 360° continuous servo.
    int _computePanPWM(float error) {
        if (fabsf(error) <= PAN_DEADBAND) return SERVO_STOP;

        float speed = PAN_KP * (fabsf(error) - PAN_DEADBAND);
        speed = constrain(speed, 0.0f, static_cast<float>(MAX_SERVO_SPEED));

        return SERVO_STOP + static_cast<int>((error > 0.0f) ? speed : -speed);
    }

    void _writePan(int pwm) {
        _panServo.writeMicroseconds(constrain(pwm, SERVO_MAX_CCW, SERVO_MAX_CW));
    }

    // Tilt: map elevation angle linearly to servo microseconds.
    void _writeTiltAngle(float elevation) {
        float clamped = constrain(elevation, MIN_TILT_DEG, MAX_TILT_DEG);
        float ratio   = (clamped - MIN_TILT_DEG) / (MAX_TILT_DEG - MIN_TILT_DEG);
        int   pwm     = TILT_MIN_PWM + static_cast<int>(ratio * (TILT_MAX_PWM - TILT_MIN_PWM));
        _tiltServo.writeMicroseconds(pwm);
    }
};
