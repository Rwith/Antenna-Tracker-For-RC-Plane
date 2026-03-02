#pragma once

// ─── UART / Serial ───────────────────────────────────────────────────────────

// NEO-6M GPS module (UART1)
#define GPS_RX_PIN   16    // ESP32 GPIO16 ← NEO-6M TX
#define GPS_TX_PIN   17    // ESP32 GPIO17 → NEO-6M RX  (optional if read-only)
#define GPS_BAUD     9600

// BetaFPV ELRS 868/915 MHz Micro TX V2 – CRSF protocol (UART2)
// This module has a SINGLE "CRSF Serial Port" pin (half-duplex).
// Wire that pin → ESP32 GPIO18 (RX only).  No TX wire is needed for telemetry.
// Power the module via its XT30 plug (7–13 V LiPo) – not from ESP32 pins.
#define CRSF_RX_PIN  18    // ESP32-S3 GPIO18 ← ELRS "CRSF Serial Port" pin
#define CRSF_BAUD    420000  // CRSF standard baud rate – do not change

// ─── Servo Signal Pins ────────────────────────────────────────────────────────

#define PAN_SERVO_PIN   40   // Azimuth (left/right) servo signal
#define TILT_SERVO_PIN  41   // Elevation (up/down) servo signal

// ─── 360° Continuous Servo PWM Calibration (microseconds) ────────────────────
// 360° servos respond to SPEED, not position:
//   SERVO_STOP    = stationary
//   > SERVO_STOP  = clockwise / upward
//   < SERVO_STOP  = counter-clockwise / downward
//
// Each servo is slightly different. If yours drifts when stopped, adjust
// SERVO_STOP by ±10–30 µs until it holds still.
#define SERVO_STOP     1500   // µs – stopped (adjust per servo)
#define SERVO_MAX_CW   1900   // µs – full clockwise speed
#define SERVO_MAX_CCW  1100   // µs – full counter-clockwise speed

// ─── I2C Bus ──────────────────────────────────────────────────────────────────
// ESP32-S3 DevKitC-1 default I2C pins.
// Adjust if your board breaks these out differently.

#define I2C_SDA_PIN  8
#define I2C_SCL_PIN  9

// ─── Pan Axis – 360° Continuous Servo P-Controller ───────────────────────────
// Increase PAN_KP if tracking is sluggish; decrease if the pan oscillates.

#define PAN_DEADBAND     3.0f   // degrees – pan stops when within this of target
#define PAN_KP           4.0f   // proportional gain for pan axis

// Max speed offset from SERVO_STOP (µs). Caps servo speed at large pan errors.
#define MAX_SERVO_SPEED  350

// ─── Tilt Axis – 180° Positional Servo ────────────────────────────────────────
// The 180° servo moves directly to the commanded elevation angle.
// Calibrate the two endpoints to match your servo's physical range.
//   TILT_MIN_PWM → antenna horizontal (MIN_TILT_DEG)
//   TILT_MAX_PWM → antenna at maximum elevation (MAX_TILT_DEG)
#define TILT_MIN_PWM  1000   // µs at 0° elevation (horizontal)
#define TILT_MAX_PWM  2000   // µs at MAX_TILT_DEG elevation

// ─── Elevation Limits ────────────────────────────────────────────────────────

#define MIN_TILT_DEG    0.0f   // horizontal (plane at same altitude as tracker)
#define MAX_TILT_DEG   85.0f   // near-zenith limit

// ─── Safety Timeouts ─────────────────────────────────────────────────────────

#define GPS_TIMEOUT_MS         5000   // stop if home GPS data is older than this
#define CRSF_TIMEOUT_MS        3000   // stop if plane GPS data is older than this
#define MIN_PLANE_DISTANCE_M   5.0f   // ignore plane if closer than this (m)
                                      // prevents wild spinning when on the ground

// ─── WiFi (web dashboard) ─────────────────────────────────────────────────────
// The ESP32 hosts a live status page at http://<ip>/ once connected.
// Set WIFI_SSID / WIFI_PASSWORD to your network credentials.
// If the connection fails the tracker still operates normally.
#define WIFI_SSID        "WithersNet_2G"
#define WIFI_PASSWORD    "hookmeup"
#define WIFI_TIMEOUT_MS  15000   // ms to wait for association before giving up

// ─── Compass (QMC5883L) Calibration ─────────────────────────────────────────
// Run a compass calibration (rotate tracker 360°) to find hard-iron offsets.
// Uncalibrated values will work but may have a few degrees of error.
#define COMPASS_OFFSET_X    0.0f   // raw ADC offset on X axis
#define COMPASS_OFFSET_Y    0.0f   // raw ADC offset on Y axis

// Magnetic declination for your location (degrees, + East / – West).
// Look up your value at: https://www.magnetic-declination.com
#define COMPASS_DECLINATION  11.0f
