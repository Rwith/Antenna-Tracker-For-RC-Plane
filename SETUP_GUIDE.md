# Step-by-Step Setup Guide

This guide walks you through building, wiring, flashing, and calibrating the antenna tracker from scratch.

---

## What You Need

| # | Component | Notes |
|---|-----------|-------|
| 1 | ESP32 DevKit v1 (38-pin) | Any clone works |
| 2 | NEO-6M GPS module | With ceramic antenna |
| 3 | BetaFPV ELRS 915 MHz TX backpack | Attaches to your radio transmitter |
| 4 | QMC5883L magnetometer breakout | I2C compass for pan feedback |
| 5 | 360° continuous-rotation servo | Pan (left/right) axis |
| 6 | 180° standard servo | Tilt (up/down) axis |
| 7 | 5 V BEC or regulated supply ≥ 2 A | Servo power — **do not power from ESP32** |
| 8 | USB-A to Micro-USB cable | Programming the ESP32 |
| 9 | Jumper wires | Male-to-female and male-to-male |
| 10 | 4.7 kΩ resistors × 2 | I2C pull-ups (if not on breakout board) |

Your **RC plane** must also have an ELRS receiver with GPS telemetry enabled.

---

## Step 1 — Install the Software Tools

### Option A — PlatformIO (recommended)

1. Download and install [Visual Studio Code](https://code.visualstudio.com/).
2. Open VS Code → click the **Extensions** icon (left sidebar).
3. Search for **PlatformIO IDE** and click **Install**.
4. Restart VS Code when prompted.

### Option B — Arduino IDE 2.x

1. Download [Arduino IDE 2](https://www.arduino.cc/en/software).
2. Open **File → Preferences** and add this URL to "Additional boards manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools → Board → Boards Manager**, search **esp32** and install.
4. Open **Sketch → Include Library → Manage Libraries** and install:
   - `TinyGPSPlus` by Mikal Hart
   - `ESP32Servo` by Kevin Harrington

---

## Step 2 — Download the Project

```bash
git clone https://github.com/<your-repo>/Antenna-Tracker-For-RC-Plane.git
cd Antenna-Tracker-For-RC-Plane
```

Or click **Code → Download ZIP** on GitHub and extract it.

**If using Arduino IDE:** rename `src/main.cpp` to `main.ino` and move all the `.h` files into the same folder as `main.ino`.

---

## Step 3 — Wire the Hardware

Wire everything according to the table below. All pin numbers can be changed in `src/config.h` if needed.

### NEO-6M GPS → ESP32 (UART1)

| NEO-6M | ESP32 GPIO | Notes |
|--------|-----------|-------|
| TX | **GPIO 16** | GPS data into ESP32 |
| RX | GPIO 17 | Optional (read-only is fine) |
| VCC | 3.3 V | |
| GND | GND | |

### BetaFPV ELRS Backpack → ESP32 (UART2)

| ELRS Backpack | ESP32 GPIO | Notes |
|--------------|-----------|-------|
| TX | **GPIO 18** | CRSF telemetry into ESP32 |
| RX | GPIO 19 | Optional |
| VCC | 3.3 V or 5 V | Check your module's rating |
| GND | GND | |

### QMC5883L Compass → ESP32-S3 (I2C)

| QMC5883L | ESP32-S3 GPIO | Notes |
|----------|--------------|-------|
| SDA | **GPIO 8** | Add 4.7 kΩ pull-up to 3.3 V if not on board |
| SCL | **GPIO 9** | Add 4.7 kΩ pull-up to 3.3 V if not on board |
| VCC | 3.3 V | |
| GND | GND | |

### Servos → ESP32 + External Power

| Servo | Signal pin | Power |
|-------|-----------|-------|
| Pan (360°) | **GPIO 40** | External 5–6 V supply |
| Tilt (180°) | **GPIO 41** | External 5–6 V supply |

> **Important:** Connect servo GND to ESP32 GND to share a common ground. Never power servos from the ESP32 pins — current spikes will crash or damage it.

### Wiring Diagram

```
                    ┌──────────────────────────────────┐
                    │         ESP32-S3 DevKitC-1        │
                    │                                  │
NEO-6M GPS ─TX────▶│GPIO16  GPIO8───SDA──┐            │
           ◀RX─────│GPIO17  GPIO9───SCL──┤            │
           ──VCC───│3.3V               QMC5883L        │
           ──GND───│GND                               │
                    │                                  │
ELRS Backpack─TX──▶│GPIO18                            │
             ◀RX───│GPIO19                            │
             ─VCC──│3.3V/5V                           │
             ─GND──│GND                               │
                    │                                  │
Pan Servo ─signal──│GPIO40  (360° continuous)         │
Tilt Servo─signal──│GPIO41  (180° positional)         │
                    └──────────────────────────────────┘

Servo VCC/GND → external 5–6 V BEC (GND shared with ESP32)
```

---

## Step 4 — Edit the Configuration

Open **`src/config.h`** and change these settings:

### WiFi (required for the web dashboard)

```c
#define WIFI_SSID      "your-network-name"
#define WIFI_PASSWORD  "your-password"
```

### Magnetic Declination

Look up your location at [magnetic-declination.com](https://www.magnetic-declination.com) and set:

```c
#define COMPASS_DECLINATION  3.5f   // example: 3.5° East
```

Positive = East, negative = West. Skipping this causes a constant bearing offset.

### Tilt Servo Endpoints (calibrate after first flash)

```c
#define TILT_MIN_PWM  1000   // µs – antenna horizontal (0° elevation)
#define TILT_MAX_PWM  2000   // µs – antenna at MAX_TILT_DEG
```

Adjust these after flashing to match your servo's actual travel — see Step 7.

---

## Step 5 — Flash the Firmware

### PlatformIO

1. Open the project folder in VS Code.
2. Wait for PlatformIO to initialise (bottom status bar).
3. Connect the ESP32 via USB.
4. Click the **Upload** button (→ arrow in the bottom toolbar).
5. PlatformIO downloads libraries and flashes automatically.

### Arduino IDE

1. Open `main.ino`.
2. Go to **Tools → Board → ESP32 Arduino → ESP32 Dev Module**.
3. Go to **Tools → Port** and select the ESP32 COM/tty port.
4. Click **Upload** (→ arrow).

---

## Step 6 — Check the Serial Monitor

Open the serial monitor at **115200 baud**.

Expected startup sequence:

```
[TRACKER] Antenna Tracker starting...
[OK]    QMC5883L compass ready.
[OK]    Servos attached.
[INFO]  Waiting for home GPS fix (need >=4 satellites)...
```

Once the GPS gets a fix (take the tracker outside):

```
[HOME]  GPS locked: 51.5074000, -0.1278000  alt=12.3 m  sats=8
```

Once the plane is in the air and ELRS telemetry is flowing:

```
[TRACK] dist=342m  bearing=247.3°  elev=8.1°  pan=246.8°  tilt=8.1°
```

**If you see `QMC5883L not found`** — check your I2C wiring and pull-up resistors.

---

## Step 7 — Calibrate the Tilt Servo

The 180° tilt servo needs its endpoint pulse widths matched to your servo.

1. With the tracker powered and Serial Monitor open, command the tilt to 0°:
   - Temporarily set `MAX_TILT_DEG 0` and reflash (or just observe startup position).
2. The antenna should point **horizontally**. If not, adjust `TILT_MIN_PWM` in ±50 µs steps.
3. Command the tilt to 85° (the default max). Adjust `TILT_MAX_PWM` until the antenna points nearly straight up.
4. Reflash after each change.

---

## Step 8 — Calibrate the Pan Servo Stop Point

The 360° pan servo must hold perfectly still when the tracker is on target. If it drifts:

1. Open `src/config.h`.
2. Adjust `SERVO_STOP` in ±10 µs steps (default 1500).
3. Reflash and check — repeat until stationary.

Typical values are 1480–1520 µs.

---

## Step 9 — Calibrate the Compass

The QMC5883L picks up hard-iron interference from nearby metal. Calibrating removes this error.

1. Take the assembled tracker outdoors, away from vehicles and metal structures.
2. Rotate it **slowly through a full 360°** while it's powered.
3. Temporarily add `Serial.printf("X:%d Y:%d\n", rawX, rawY);` to `compass.h → getHeading()` to log raw values.
4. Note the **minimum and maximum** X and Y values from the log.
5. In `src/config.h`:
   ```c
   #define COMPASS_OFFSET_X  (Xmax + Xmin) / 2.0f
   #define COMPASS_OFFSET_Y  (Ymax + Ymin) / 2.0f
   ```
6. Remove the debug printf and reflash.

---

## Step 10 — Plane-Side ELRS Setup

The tracker reads GPS telemetry sent back over the ELRS link from your plane.

### ArduPilot

Set the serial port connected to your ELRS receiver:

```
SERIALx_PROTOCOL = 23   (RCIN / CRSF)
```

ArduPilot automatically sends GPS, battery, and attitude frames through ELRS.

### Betaflight

Enable **CRSF** on the UART connected to the ELRS receiver, and turn on **GPS telemetry** in the Telemetry tab.

---

## Step 11 — First Test

1. Power on the tracker outdoors and wait for a GPS fix (blue LED on NEO-6M stops blinking rapidly).
2. Open the web dashboard at `http://<esp32-ip>/` (IP is printed in the serial monitor on boot).
3. Power on your plane and walk it away — the tracker should follow.
4. If the pan direction is reversed, swap the `SERVO_MAX_CW` and `SERVO_MAX_CCW` values in `config.h`.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `QMC5883L not found` | I2C wiring wrong | Check SDA/SCL pins and pull-ups |
| Pan servo drifts when stopped | Stop point off | Adjust `SERVO_STOP` ±10 µs |
| Pan tracks the wrong direction | Motor wiring reversed | Swap `SERVO_MAX_CW` / `SERVO_MAX_CCW` |
| Tilt doesn't reach horizontal | `TILT_MIN_PWM` wrong | Adjust up/down in 50 µs steps |
| No GPS fix | Antenna blocked | Move outdoors, wait 2–3 min |
| No CRSF data | ELRS telemetry not enabled | Check plane-side ELRS/ArduPilot config |
| Bearing offset constant | Wrong declination | Set `COMPASS_DECLINATION` for your location |
| ESP32 crashes when servos move | Servos drawing power from ESP32 | Power servos from external BEC |

---

## Tuning the Pan Controller

If tracking is sluggish or oscillates, adjust these in `src/config.h`:

| Parameter | Default | Effect |
|-----------|---------|--------|
| `PAN_KP` | 4.0 | Higher → faster pan. If it oscillates, lower this. |
| `PAN_DEADBAND` | 3.0° | Dead zone around target. Lower = tighter tracking. |
| `MAX_SERVO_SPEED` | 350 µs | Speed cap. Lower = slower max pan speed. |

Start with defaults — only tune if you see oscillation or sluggishness.
