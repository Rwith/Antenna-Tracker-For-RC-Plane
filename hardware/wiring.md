# Wiring Guide – ESP32 Antenna Tracker

## Components

| # | Component | Purpose |
|---|-----------|---------|
| 1 | ESP32 DevKit v1 (38-pin) | Main controller |
| 2 | NEO-6M GPS module | Ground-station position |
| 3 | BetaFPV ELRS 915 MHz backpack | Receives CRSF GPS telemetry from plane |
| 4 | QMC5883L magnetometer | Pan (azimuth) position feedback |
| 5 | 360° continuous servo | Pan axis |
| 6 | 180° standard servo | Tilt axis (direct angle control – no sensor needed) |
| 7 | 5–6 V regulated supply (≥ 2 A) | Servo power |

---

## Pin Assignments

All pin numbers match the defaults in `src/config.h`.  Change the `#define`s there if you need to use different GPIOs.

### UART – NEO-6M GPS (UART1)

| ESP32 GPIO | NEO-6M pin | Wire colour (suggestion) |
|------------|-----------|--------------------------|
| GPIO 16 (RX1) | TX | Green |
| GPIO 17 (TX1) | RX | Yellow (optional – needed only if you send UBX config commands) |
| 3.3 V | VCC | Red |
| GND | GND | Black |

> The NEO-6M outputs NMEA sentences at 9600 baud by default.

### UART – BetaFPV ELRS 915 MHz Backpack (UART2)

The ELRS backpack outputs **CRSF protocol** at **420,000 baud**.
Signal is **non-inverted 3.3 V logic** – connect directly to the ESP32, no level shifter needed.

| ESP32 GPIO | ELRS backpack pin | Wire colour (suggestion) |
|------------|-------------------|--------------------------|
| GPIO 18 (RX2) | TX (backpack → ESP32) | Green |
| GPIO 19 (TX2) | RX (ESP32 → backpack) | Yellow (optional) |
| 5 V or 3.3 V | VCC (check your module's rating) | Red |
| GND | GND | Black |

> CRSF baud rate is fixed at 420,000 – `CRSF_BAUD` in `config.h` must not be changed.

**Where to find the TX pin on the BetaFPV ELRS backpack:**
The backpack exposes a UART header (often labelled `T` / `R` or `TX` / `RX`).
Connect the backpack's `TX` pin to ESP32 `GPIO 18`.  That is the only wire
required for receive-only telemetry.

### I2C – QMC5883L Compass

| ESP32 GPIO | QMC5883L | Wire colour |
|------------|----------|-------------|
| GPIO 21 (SDA) | SDA | Blue |
| GPIO 22 (SCL) | SCL | Yellow |
| 3.3 V | VCC | Red |
| GND | GND | Black |

I2C address (fixed in hardware): QMC5883L → **0x0D**

### Servo Signal Wires

| ESP32 GPIO | Servo | Note |
|------------|-------|------|
| GPIO 25 | Pan servo (signal) | 360° continuous, PWM 50 Hz |
| GPIO 26 | Tilt servo (signal) | 180° positional, PWM 50 Hz |

**Power the servos separately** from a 5–6 V supply capable of at least 1 A per servo.  Connect servo GND to ESP32 GND to share a common ground.

> Do **not** power servos from the ESP32 3.3 V or 5 V (VIN) pins – current spikes will brownout the MCU.

---

## Wiring Diagram (ASCII)

```
                        ┌──────────────────────────────────┐
                        │           ESP32 DevKit            │
                        │                                  │
 NEO-6M GPS ────TX─────▶│GPIO16  GPIO21────────────SDA──┐  │
             ◀──RX──────│GPIO17  GPIO22────────────SCL──┤  │
             ────VCC────│3.3V                            │  │
             ────GND────│GND                             │  │
                        │                                │  │
 ELRS Backpack───TX────▶│GPIO18       ┌──────────────────┘  │
              ◀──RX─────│GPIO19       │  I2C bus             │
              ────VCC───│3.3V/5V      │                      │
              ────GND───│GND          └─── QMC5883L (0x0D)  │
                        │                                  │
 Pan Servo ──signal─────│GPIO25  (360° continuous)         │
 Tilt Servo ─signal─────│GPIO26  (180° positional)         │
                        └──────────────────────────────────┘

 Pan/Tilt servos: signal from ESP32, power from 5-6V external supply.
 All GNDs connected together.
```

---

## Sensor Mounting

### QMC5883L – Pan platform

- Mount **flat and level** on the rotating pan deck.
- The **X/Y axes must remain horizontal** as the tracker rotates.
- Avoid mounting near motor magnets or ferrous bolts – they cause compass error.
- After assembly, calibrate: rotate tracker 360° and record X/Y min/max.
  Set `COMPASS_OFFSET_X = (Xmax + Xmin) / 2` and same for Y in `config.h`.

### Tilt servo – 180° positional

The 180° tilt servo is commanded directly to the calculated elevation angle.
No position sensor is required – the servo's internal pot handles positioning.

- **`TILT_MIN_PWM`** (default 1000 µs) = antenna horizontal (0° elevation)
- **`TILT_MAX_PWM`** (default 2000 µs) = antenna at `MAX_TILT_DEG` elevation

Adjust these two values in `config.h` to match your servo's actual endpoints.

---

## Power Budget (typical)

| Component | Current |
|-----------|---------|
| ESP32 | ~240 mA peak |
| NEO-6M GPS | ~45 mA |
| QMC5883L | ~0.5 mA |
| 360° pan servo (stall) | ~700 mA |
| 180° tilt servo (stall) | ~700 mA |
| **Total (both servos active)** | **~1.7 A** |

Use a 2 A or greater 5 V supply.  A 2S LiPo + 5 V BEC is a common field solution.

---

## Checklist Before First Power-On

- [ ] All GNDs connected together
- [ ] Servos powered from external 5–6 V supply (not ESP32)
- [ ] I2C pull-up resistors (4.7 kΩ to 3.3 V on SDA and SCL) – many breakout boards include these
- [ ] 100 µF capacitor across servo power rail to absorb current spikes
- [ ] `config.h` reviewed: baud rates, pin numbers, `COMPASS_DECLINATION`
- [ ] `TILT_MIN_PWM` / `TILT_MAX_PWM` verified against your servo's physical endpoints
- [ ] Serial monitor open at 115200 baud to watch startup messages
