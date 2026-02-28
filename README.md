# Antenna Tracker for RC Planes

An open-source ESP32 firmware that automatically points a directional antenna toward your RC plane using CRSF GPS telemetry. A 360° continuous-rotation servo drives the pan axis in a closed-loop proportional controller, while a 180° positional servo drives the tilt axis directly to the calculated elevation angle.

---

## How It Works

```
┌──────────────────────────────────────────────────────────────────┐
│                         ESP32 firmware                           │
│                                                                  │
│  NEO-6M GPS ──▶ TinyGPS++ ──▶ Home position (lat/lon/alt)       │
│                                       │                          │
│  ELRS backpack ──▶ CRSF parser ──▶ Plane position (lat/lon/alt) │
│                                       │                          │
│              haversine distance + bearing + elevation angle      │
│                                       │                          │
│  QMC5883L compass ──▶ current pan angle ──▶ pan P-controller    │
│                                       │                          │
│  Pan:  PWM → 360° continuous servo (closed-loop)                │
│  Tilt: PWM → 180° positional servo   (open-loop, direct angle)  │
└──────────────────────────────────────────────────────────────────┘
```

1. The **NEO-6M GPS** gives the tracker's own position.
2. The **BetaFPV ELRS 915 MHz backpack** receives CRSF `GPS` frames (frame type 0x02) from the plane's ELRS receiver at 420,000 baud.
3. The firmware computes the required **bearing** (azimuth) and **elevation angle**.
4. A **QMC5883L magnetometer** on the pan platform provides the current heading for the pan P-controller.
5. The **180° tilt servo** moves directly to the commanded elevation angle — no IMU or sensor required.

---

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32 DevKit (any 38-pin) | |
| NEO-6M GPS module | Ground station position |
| BetaFPV ELRS 915 MHz backpack | CRSF GPS telemetry from plane |
| QMC5883L magnetometer | Pan / azimuth feedback |
| 360° continuous-rotation servo | Pan axis |
| 180° standard servo | Tilt axis (direct angle control) |
| 5–6 V supply ≥ 2 A | Servo power |

---

## Quick Start

### 1. Install PlatformIO

```bash
pip install platformio
```

Or install the PlatformIO IDE extension for VS Code.

### 2. Clone and open

```bash
git clone <repo-url>
cd Antenna-Tracker-For-RC-Plane
```

### 3. Configure

Edit **`src/config.h`** to match your wiring:

```c
// UART pins
#define GPS_RX_PIN   16
#define CRSF_RX_PIN  18     // ELRS backpack TX → ESP32 GPIO 18
// CRSF_BAUD is fixed at 420000 – do not change

// Servo pins
#define PAN_SERVO_PIN   25
#define TILT_SERVO_PIN  26

// Pan servo stop point (adjust if servo drifts when stopped)
#define SERVO_STOP  1500

// 180° tilt servo endpoints – calibrate to your servo
#define TILT_MIN_PWM  1000   // µs → antenna horizontal (0° elevation)
#define TILT_MAX_PWM  2000   // µs → antenna at MAX_TILT_DEG elevation

// Magnetic declination for your location
// Find at https://www.magnetic-declination.com
#define COMPASS_DECLINATION  0.0f
```

### 4. Build and flash

```bash
pio run --target upload
```

### 5. Monitor

```bash
pio device monitor
```

Expected output once running:

```
[TRACKER] Antenna Tracker starting...
[OK]    QMC5883L compass ready.
[OK]    Servos attached.
[INFO]  Waiting for home GPS fix (need ≥4 satellites)...
[HOME]  GPS locked: 51.5074000, -0.1278000  alt=12.3 m  sats=8
[TRACK] dist=342m  bearing=247.3°  elev=8.1°  pan=246.8°
```

---

## Wiring

See [`hardware/wiring.md`](hardware/wiring.md) for the full wiring guide and ASCII diagram.

Quick summary:

| ESP32 GPIO | Connected to |
|------------|-------------|
| 16 (RX1) | NEO-6M TX |
| 17 (TX1) | NEO-6M RX |
| 18 (RX2) | ELRS backpack TX (CRSF) |
| 19 (TX2) | ELRS backpack RX (optional) |
| 21 (SDA) | QMC5883L SDA |
| 22 (SCL) | QMC5883L SCL |
| 25 | Pan servo signal (360°) |
| 26 | Tilt servo signal (180°) |

---

## Calibration

### Pan servo stop point

The 360° pan servo needs its neutral pulse width tuned so it stops cleanly. If the pan servo drifts when it should be stopped:

1. Open serial monitor.
2. Adjust `SERVO_STOP` in `config.h` in ±10 µs steps until drift stops.
3. Typical range: 1480–1520 µs.

### Tilt servo endpoints

The 180° tilt servo is commanded via `TILT_MIN_PWM` and `TILT_MAX_PWM`:

1. Set `TILT_MIN_PWM` so the antenna sits **horizontal** at 0° elevation.
2. Set `TILT_MAX_PWM` so the antenna reaches `MAX_TILT_DEG` elevation.
3. Typical values: 1000–2000 µs, but vary by servo brand.

### Compass hard-iron calibration

1. Place the tracker outdoors, away from metal objects.
2. Slowly rotate it through a full 360°.
3. Note the minimum and maximum raw X and Y values (add temporary `Serial.printf` calls in `compass.h → getHeading()`).
4. Set in `config.h`:
   ```c
   #define COMPASS_OFFSET_X  (Xmax + Xmin) / 2.0f
   #define COMPASS_OFFSET_Y  (Ymax + Ymin) / 2.0f
   ```

### Magnetic declination

Look up your local declination at <https://www.magnetic-declination.com> and set `COMPASS_DECLINATION` (positive = East, negative = West).

---

## Tuning the Controller

Pan axis gains are in `src/config.h`. The tilt axis requires no tuning — the 180° servo positions itself.

| Parameter | Default | Effect |
|-----------|---------|--------|
| `PAN_KP` | 4.0 | Increase → faster pan, may oscillate |
| `PAN_DEADBAND` | 3.0° | Reduce → tighter tracking, more servo chatter |
| `MAX_SERVO_SPEED` | 350 µs | Caps maximum pan servo speed |

---

## ELRS / CRSF Compatibility

The tracker reads **CRSF GPS frames** (frame type `0x02`) delivered by the **BetaFPV ELRS 915 MHz backpack** via UART at **420,000 baud**.

### Plane-side setup (ArduPilot / Betaflight)
The ELRS receiver on the plane must be configured to forward GPS telemetry back over the RC link.  In ArduPilot, enable CRSF telemetry on the serial port connected to the ELRS RX:
```
SERIALx_PROTOCOL = 23   (RCIN)
```
ArduPilot will automatically send GPS, attitude, and battery frames to the ELRS RX, which relays them through the 915 MHz link to your backpack on the ground.

### What data is used
| CRSF frame | Type | Used for |
|---|---|---|
| GPS (0x02) | lat, lon, altitude, satellites | Bearing + elevation calculation |

The CRSF GPS altitude field carries **metres MSL with a 1000 m offset** (`raw_uint16 − 1000 = altitude_m`).

---

## Project Structure

```
├── platformio.ini          PlatformIO build config + dependencies
├── README.md
├── hardware/
│   └── wiring.md           Full wiring guide and diagrams
└── src/
    ├── main.cpp            Setup, main loop, data flow
    ├── config.h            All pin/constant configuration (edit this)
    ├── crsf_parser.h       CRSF frame parser for ELRS backpack (GPS frame type 0x02)
    ├── compass.h           QMC5883L driver (pan feedback)
    ├── tracker_math.h      Haversine distance, bearing, elevation calculations
    └── servo_controller.h  Pan P-controller (360°) + tilt direct-angle writer (180°)
```

---

## License

MIT – free to use, modify, and distribute.
