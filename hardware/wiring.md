# Wiring Guide – ESP32 Antenna Tracker

## Components

| # | Component | Purpose |
|---|-----------|---------|
| 1 | ESP32 DevKit v1 (38-pin) | Main controller |
| 2 | NEO-6M GPS module | Ground-station position |
| 3 | SiK / RFD900 telemetry radio | Receives MAVLink from plane |
| 4 | QMC5883L magnetometer | Pan (azimuth) position feedback |
| 5 | MPU6050 6-axis IMU | Tilt (elevation) position feedback |
| 6 | 360° continuous servo × 2 | Pan axis + tilt axis |
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

### UART – MAVLink Telemetry Radio (UART2)

| ESP32 GPIO | Radio pin | Wire colour (suggestion) |
|------------|-----------|--------------------------|
| GPIO 18 (RX2) | TX | Green |
| GPIO 19 (TX2) | RX | Yellow (optional – bidirectional link) |
| 5 V | VCC | Red |
| GND | GND | Black |

> Set your radio baud rate in `config.h → MAV_BAUD`.  SiK radios default to 57600.

### I2C – QMC5883L Compass + MPU6050 IMU (shared bus)

Both sensors share the same two wires:

| ESP32 GPIO | QMC5883L | MPU6050 | Wire colour |
|------------|----------|---------|-------------|
| GPIO 21 (SDA) | SDA | SDA | Blue |
| GPIO 22 (SCL) | SCL | SCL | Yellow |
| 3.3 V | VCC | VCC | Red |
| GND | GND | GND | Black |

I2C addresses (fixed in hardware):
- QMC5883L → **0x0D**
- MPU6050   → **0x68** (AD0 pin tied LOW)

### Servo Signal Wires

| ESP32 GPIO | Servo | Note |
|------------|-------|------|
| GPIO 25 | Pan servo (signal) | PWM 50 Hz |
| GPIO 26 | Tilt servo (signal) | PWM 50 Hz |

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
 Telem Radio ────TX────▶│GPIO18       ┌──────────────────┘  │
              ◀──RX─────│GPIO19       │  I2C bus             │
              ────VCC───│5V (or ext.) │                      │
              ────GND───│GND          ├─── QMC5883L (0x0D)  │
                        │             └─── MPU6050  (0x68)  │
 Pan Servo ──signal─────│GPIO25                            │
 Tilt Servo ─signal─────│GPIO26                            │
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

### MPU6050 – Tilt platform

Mount on the **moving tilt arm** so it tilts with the antenna:

```
 Antenna direction ──▶   ═══════[MPU6050]════════▶  antenna
                                  X──▶  Z
                                        ↑
                          (Z points up when antenna is horizontal)
```

- **X axis** → forward along the antenna barrel
- **Z axis** → upward when the antenna is horizontal (0° elevation)
- If your elevation readings are inverted, try flipping the board 180° around the X axis.

---

## Power Budget (typical)

| Component | Current |
|-----------|---------|
| ESP32 | ~240 mA peak |
| NEO-6M GPS | ~45 mA |
| QMC5883L | ~0.5 mA |
| MPU6050 | ~4 mA |
| Each 360° servo (stall) | ~700 mA |
| **Total (both servos active)** | **~1.7 A** |

Use a 2 A or greater 5 V supply.  A 2S LiPo + 5 V BEC is a common field solution.

---

## Checklist Before First Power-On

- [ ] All GNDs connected together
- [ ] Servos powered from external 5–6 V supply (not ESP32)
- [ ] I2C pull-up resistors (4.7 kΩ to 3.3 V on SDA and SCL) – many breakout boards include these
- [ ] 100 µF capacitor across servo power rail to absorb current spikes
- [ ] `config.h` reviewed: baud rates, pin numbers, `COMPASS_DECLINATION`
- [ ] Serial monitor open at 115200 baud to watch startup messages
