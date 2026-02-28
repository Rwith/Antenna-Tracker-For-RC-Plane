#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>
#include "config.h"
#include "tracker_math.h"

// ─────────────────────────────────────────────────────────────────────────────
// Proportional controller for 360° continuous rotation servos
//
// 360° servos are speed-controlled (not position-controlled):
//   PWM > SERVO_STOP  → clockwise  / tilt up
//   PWM < SERVO_STOP  → counter-clockwise / tilt down
//   PWM = SERVO_STOP  → stopped
//
// The controller computes:
//   error = target – current (pan uses shortest-path angle difference)
//   |error| ≤ deadband  → stop
//   |error| > deadband  → speed = Kp × (|error| − deadband), capped at MAX_SERVO_SPEED
//
// Tune Kp and deadband in config.h.
// ─────────────────────────────────────────────────────────────────────────────

class ServoController {
public:
    void begin() {
        _panServo.attach(PAN_SERVO_PIN,  900, 2100);
        _tiltServo.attach(TILT_SERVO_PIN, 900, 2100);
        stop();
    }

    // Set the desired pan (azimuth) and tilt (elevation) angles in degrees.
    void setTarget(float panDeg, float tiltDeg) {
        _targetPan  = panDeg;
        _targetTilt = constrain(tiltDeg, MIN_TILT_DEG, MAX_TILT_DEG);
    }

    // Call every loop iteration with fresh sensor readings.
    void update(float currentPan, float currentTilt) {
        float panError  = normalizeAngleDiff(_targetPan - currentPan);
        float tiltError = _targetTilt - currentTilt;

        _writePan (_computePWM(panError,  PAN_DEADBAND,  PAN_KP));
        _writeTilt(_computePWM(tiltError, TILT_DEADBAND, TILT_KP));
    }

    // Immediately stop both servos (call on data timeout / safety).
    void stop() {
        _writePan(SERVO_STOP);
        _writeTilt(SERVO_STOP);
    }

private:
    Servo _panServo;
    Servo _tiltServo;

    float _targetPan  = 0.0f;
    float _targetTilt = 0.0f;

    // Convert angular error to a servo PWM value.
    //   error > 0 → CW / upward   → PWM > SERVO_STOP
    //   error < 0 → CCW / downward → PWM < SERVO_STOP
    //   |error| ≤ deadband → PWM = SERVO_STOP (stopped)
    int _computePWM(float error, float deadband, float kp) {
        if (fabsf(error) <= deadband) return SERVO_STOP;

        // Proportional speed (remove deadband gap for a smooth start)
        float speed = kp * (fabsf(error) - deadband);
        speed = constrain(speed, 0.0f, static_cast<float>(MAX_SERVO_SPEED));

        int pwm = SERVO_STOP + static_cast<int>((error > 0.0f) ? speed : -speed);
        return pwm;
    }

    void _writePan(int pwm) {
        _panServo.writeMicroseconds(constrain(pwm, SERVO_MAX_CCW, SERVO_MAX_CW));
    }

    void _writeTilt(int pwm) {
        _tiltServo.writeMicroseconds(constrain(pwm, SERVO_MAX_CCW, SERVO_MAX_CW));
    }
};
