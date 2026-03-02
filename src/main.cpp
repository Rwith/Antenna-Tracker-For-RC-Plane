#include <Arduino.h>
#include <Wire.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

#include "config.h"
#include "crsf_parser.h"
#include "compass.h"
#include "tracker_math.h"
#include "servo_controller.h"
#include "web_server.h"

// ─── Hardware serial ports ────────────────────────────────────────────────────
HardwareSerial gpsSerial(1);   // UART1 → NEO-6M GPS
HardwareSerial elrsSerial(2);  // UART2 → BetaFPV ELRS 868 MHz Micro TX V2 (CRSF)

// ─── Component instances ──────────────────────────────────────────────────────
TinyGPSPlus     gps;
CRSFParser      crsfParser(elrsSerial);
Compass         compass;
ServoController tracker;
TrackerWebServer webServer;

// ─── State ────────────────────────────────────────────────────────────────────
PlaneGPS  planePos  = {};   // zero-init; valid=false by default
HomeGPS   homePos   = { 0, 0, 0, false };
LinkStats linkStats = {};

struct FlightAccum {
    float    maxSpeedKmh = 0.0f;
    float    maxAltM     = 0.0f;   // MSL
    float    maxRelAltM  = 0.0f;   // above home
    float    maxDistM    = 0.0f;
    bool     running     = false;  // timer active
    uint32_t startMs     = 0;
    uint32_t elapsedMs   = 0;
};
static FlightAccum fStats;

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);  // let USB-UART settle before first print
    Serial.println("\n[TRACKER] Antenna Tracker starting...");

    // I2C bus (compass only)
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

    // NEO-6M GPS
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    // BetaFPV ELRS 868 MHz Micro TX V2 – single CRSF pin, RX only
    elrsSerial.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, -1);

    // Compass (pan feedback)
    if (!compass.begin()) {
        Serial.println("[WARN]  QMC5883L not found – compass disabled (tracking unavailable).");
    } else {
        Serial.println("[OK]    QMC5883L compass ready.");
    }

    // Servos
    tracker.begin();
    Serial.println("[OK]    Servos attached.");

    // WiFi + web dashboard
    webServer.begin();

    Serial.println("[INFO]  Waiting for home GPS fix (need >=4 satellites)...");
}

// ─── Main loop ────────────────────────────────────────────────────────────────
void loop() {

    // ── 1. Feed NEO-6M NMEA data into TinyGPS++ ──────────────────────────────
    while (gpsSerial.available()) {
        gps.encode(static_cast<char>(gpsSerial.read()));
    }

    // Update home position whenever we have a valid GPS fix
    if (gps.location.isValid() && gps.satellites.value() >= 4) {
        bool firstFix = !homePos.valid;
        homePos.lat   = gps.location.lat();
        homePos.lon   = gps.location.lng();
        homePos.alt   = gps.altitude.meters();
        homePos.valid = true;

        if (firstFix) {
            Serial.printf("[HOME]  GPS locked: %.7f, %.7f  alt=%.1f m  sats=%u\n",
                          homePos.lat, homePos.lon, homePos.alt,
                          gps.satellites.value());
        }
    }

    // ── 2. Feed CRSF telemetry from ELRS backpack ────────────────────────────
    crsfParser.update(planePos, linkStats);

    // ── 3. Compute target angles and drive servos ─────────────────────────────
    if (homePos.valid && planePos.valid) {
        uint32_t now      = millis();
        bool homeGpsOk    = (gps.location.age() < GPS_TIMEOUT_MS);
        bool crsfOk       = (now - planePos.lastUpdate < CRSF_TIMEOUT_MS);

        webServer.status.homeGpsOk = homeGpsOk;
        webServer.status.crsfOk    = crsfOk;

        if (homeGpsOk && crsfOk) {
            double dist = haversineDistance(homePos.lat, homePos.lon,
                                            planePos.lat, planePos.lon);

            if (dist >= webServer.minTrackDist) {
                double bearing   = calculateBearing(homePos.lat, homePos.lon,
                                                    planePos.lat, planePos.lon);
                float  altDiff   = planePos.alt - homePos.alt;
                float  elevation = calculateElevation(dist, altDiff);

                tracker.setTarget(static_cast<float>(bearing), elevation);

                webServer.status.tracking  = true;
                webServer.status.bearing   = static_cast<float>(bearing);
                webServer.status.elevation = elevation;
                webServer.status.distM     = static_cast<float>(dist);

                // Status log (once per second)
                static uint32_t lastLog = 0;
                if (now - lastLog >= 1000u) {
                    lastLog = now;
                    Serial.printf("[TRACK] dist=%.0fm  bearing=%.1f°  elev=%.1f°  "
                                  "pan=%.1f°  tilt=%.1f°\n",
                                  dist, bearing, elevation,
                                  compass.getHeading(),
                                  tracker.getTiltAngle());
                }

            } else {
                // Plane is too close – hold position to avoid spinning
                tracker.stop();
                webServer.status.tracking = false;
            }

        } else {
            // Stale data – stop servos for safety
            tracker.stop();
            webServer.status.tracking = false;

            static uint32_t lastWarn = 0;
            if (millis() - lastWarn >= 2000u) {
                lastWarn = millis();
                if (!homeGpsOk)  Serial.println("[WARN]  Home GPS timeout – servos stopped.");
                if (!crsfOk)     Serial.println("[WARN]  ELRS/CRSF timeout – servos stopped.");
            }
        }

    } else {
        // Not yet initialised – keep servos still
        tracker.stop();
        webServer.status.tracking = false;
    }

    // ── 4. Run the servo controller ───────────────────────────────────────────
    // Pan: closed-loop P-controller using compass heading.
    // Tilt: open-loop – 180° servo moves directly to the commanded angle.
    tracker.update(compass.getHeading());

    // ── 5. Update web dashboard status ───────────────────────────────────────
    webServer.status.homeValid  = homePos.valid;
    webServer.status.homeLat    = homePos.lat;
    webServer.status.homeLon    = homePos.lon;
    webServer.status.homeAlt    = homePos.alt;
    webServer.status.homeSats   = static_cast<uint8_t>(gps.satellites.value());
    webServer.status.planeValid   = planePos.valid;
    webServer.status.planeLat     = planePos.lat;
    webServer.status.planeLon     = planePos.lon;
    webServer.status.planeAlt     = planePos.alt;
    webServer.status.planeSpeed   = planePos.speedKmh;
    webServer.status.planeHeading = planePos.headingDeg;
    webServer.status.linkRSSI     = linkStats.uplinkRSSI;
    webServer.status.linkLQ       = linkStats.uplinkLQ;
    webServer.status.linkSNR      = linkStats.uplinkSNR;
    webServer.status.linkTxPwr    = linkStats.txPowerIdx;
    webServer.status.linkRfMode   = linkStats.rfMode;
    webServer.status.linkValid    = linkStats.valid;
    webServer.status.panAngle   = compass.getHeading();
    webServer.status.tiltAngle  = tracker.getTiltAngle();

    // ── 6. Flight statistics ──────────────────────────────────────────────────
    if (webServer.status.statsResetReq) {
        webServer.status.statsResetReq = false;
        fStats = FlightAccum{};
    }
    if (planePos.valid) {
        // Start timer on first detected movement (>5 km/h)
        if (!fStats.running && planePos.speedKmh > 5.0f) {
            fStats.running = true;
            fStats.startMs = millis();
        }
        if (fStats.running) {
            fStats.elapsedMs = millis() - fStats.startMs;
        }
        fStats.maxSpeedKmh = max(fStats.maxSpeedKmh, planePos.speedKmh);
        fStats.maxAltM     = max(fStats.maxAltM,     planePos.alt);
        if (homePos.valid) {
            fStats.maxRelAltM = max(fStats.maxRelAltM, planePos.alt - homePos.alt);
        }
    }
    fStats.maxDistM = max(fStats.maxDistM, webServer.status.distM);

    webServer.status.maxSpeedKmh = fStats.maxSpeedKmh;
    webServer.status.maxAltM     = fStats.maxAltM;
    webServer.status.maxRelAltM  = fStats.maxRelAltM;
    webServer.status.maxDistM    = fStats.maxDistM;
    webServer.status.flightMs    = fStats.elapsedMs;

    webServer.handle();
}
