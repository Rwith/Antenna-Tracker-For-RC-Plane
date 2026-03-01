#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// CRSF (Crossfire Serial Protocol) parser for ExpressLRS (ELRS) modules
//
// Used with: BetaFPV ELRS 915 MHz backpack (and any ELRS TX module)
//
// CRSF frame structure:
//   [ADDR][LEN][TYPE][PAYLOAD...][CRC8]
//   ADDR : device address (0xC8 = FC, 0xEE = TX module, 0xEA = handset)
//   LEN  : number of bytes that follow (TYPE + PAYLOAD + CRC), i.e. payload_len + 2
//   TYPE : frame type
//   CRC8 : CRC8/DVB-S2 (poly 0xD5, init 0x00) over TYPE + PAYLOAD bytes only
//
// CRSF_FRAMETYPE_GPS (0x02) – 15-byte payload, big-endian:
//   Bytes 0-3  : Latitude    (int32,  degrees × 1e7)
//   Bytes 4-7  : Longitude   (int32,  degrees × 1e7)
//   Bytes 8-9  : Groundspeed (uint16, km/h × 10)
//   Bytes 10-11: Heading     (uint16, degrees × 100)
//   Bytes 12-13: Altitude    (uint16, metres + 1000 m offset)
//   Byte  14   : Satellites  (uint8)
//
// CRSF_FRAMETYPE_LINK_STATISTICS (0x14) – 10-byte payload:
//   Byte 0 : Uplink RSSI antenna 1  (uint8, -dBm; 0 = no signal)
//   Byte 1 : Uplink RSSI antenna 2  (uint8, -dBm)
//   Byte 2 : Uplink link quality    (uint8, 0–100 %)
//   Byte 3 : Uplink SNR             (int8,  dB)
//   Byte 4 : Active antenna         (uint8, 0 or 1)
//   Byte 5 : RF mode                (uint8, rate index)
//   Byte 6 : Uplink TX power        (uint8, index: 0=0,1=10,2=25,3=100,4=500,
//                                            5=1000,6=2000,7=250,8=50 mW)
//   Byte 7 : Downlink RSSI          (uint8, -dBm)
//   Byte 8 : Downlink link quality  (uint8, 0–100 %)
//   Byte 9 : Downlink SNR           (int8,  dB)
//
// UART: 420000 baud, 8N1, non-inverted (direct connection to ESP32)
// ─────────────────────────────────────────────────────────────────────────────

#define CRSF_BAUD_RATE          420000u

#define CRSF_FRAMETYPE_GPS          0x02u
#define CRSF_GPS_PAYLOAD_LEN        15u

#define CRSF_FRAMETYPE_LINK_STATS   0x14u
#define CRSF_LINK_STATS_PAYLOAD_LEN 10u

// Internal payload buffer must hold the largest frame type we decode
#define CRSF_MAX_PAYLOAD_LEN        CRSF_GPS_PAYLOAD_LEN   // GPS (15) > link stats (10)

#define CRSF_MAX_FRAME_LEN          64u    // maximum any CRSF frame can be

// Valid CRSF device addresses – any of these can appear as the first byte
static constexpr uint8_t CRSF_VALID_ADDRS[] = {
    0xC8u,   // Flight controller
    0xEEu,   // TX module
    0xEAu,   // Radio handset
    0xEFu,   // ELRS Lua script
    0xCEu,   // Broadcast
};

struct PlaneGPS {
    double   lat;         // degrees
    double   lon;         // degrees
    float    alt;         // metres MSL
    float    relAlt;      // metres above home (not in CRSF GPS; set = alt)
    float    speedKmh   = 0.0f;  // groundspeed (km/h)
    float    headingDeg = 0.0f;  // true heading (degrees, 0 = N)
    bool     valid      = false;
    uint32_t lastUpdate = 0;     // millis() when last valid packet was received
};

struct LinkStats {
    int8_t   uplinkRSSI  = 0;    // active-antenna RSSI (dBm, negative)
    uint8_t  uplinkLQ    = 0;    // uplink link quality (0–100 %)
    int8_t   uplinkSNR   = 0;    // uplink SNR (dB)
    uint8_t  txPowerIdx  = 0;    // TX power index (see comment block above)
    uint8_t  rfMode      = 0;    // RF mode/rate index
    bool     valid       = false; // true once at least one frame has been received
    uint32_t lastUpdate  = 0;
};

class CRSFParser {
public:
    explicit CRSFParser(HardwareSerial &serial) : _ser(serial) {}

    // Call every loop iteration.  Dispatches to the appropriate decoder on
    // each complete, CRC-valid frame.
    void update(PlaneGPS &gps, LinkStats &ls) {
        while (_ser.available()) {
            uint8_t ft = _parseByte(static_cast<uint8_t>(_ser.read()));
            if      (ft == 1u) _decodeGPS(gps);
            else if (ft == 2u) _decodeLinkStats(ls);
        }
    }

private:
    HardwareSerial &_ser;

    enum State : uint8_t { S_ADDR, S_LEN, S_TYPE, S_PAYLOAD, S_CRC };

    State   _state        = S_ADDR;
    uint8_t _frameLen     = 0;   // value of the LEN field
    uint8_t _type         = 0;
    uint8_t _pIdx         = 0;
    uint8_t _payload[CRSF_MAX_PAYLOAD_LEN];
    uint8_t _computedCRC  = 0;

    // CRC8/DVB-S2: poly 0xD5, init 0x00
    // Covers TYPE + PAYLOAD bytes only (not ADDR or LEN).
    static uint8_t _crc8(uint8_t crc, uint8_t data) {
        crc ^= data;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1) ^ 0xD5u)
                                : static_cast<uint8_t>(crc << 1);
        }
        return crc;
    }

    static bool _isValidAddr(uint8_t b) {
        for (uint8_t a : CRSF_VALID_ADDRS) {
            if (b == a) return true;
        }
        return false;
    }

    // Returns: 0 = no complete frame yet, 1 = GPS frame ready, 2 = link stats ready.
    uint8_t _parseByte(uint8_t c) {
        switch (_state) {

            case S_ADDR:
                if (_isValidAddr(c)) {
                    _state = S_LEN;
                }
                break;

            case S_LEN:
                _frameLen = c;
                // Sanity check: frame must fit in buffer and be at least 3 bytes
                // (1 type + 0 payload + 1 CRC minimum)
                if (_frameLen < 2u || _frameLen > CRSF_MAX_FRAME_LEN) {
                    _state = S_ADDR;
                } else {
                    _state = S_TYPE;
                }
                break;

            case S_TYPE:
                _type        = c;
                _computedCRC = _crc8(0x00u, c);  // CRC starts fresh over TYPE
                _pIdx        = 0;
                // payload bytes = frameLen - 2  (subtract TYPE and CRC bytes)
                if ((_frameLen - 2u) > 0u) {
                    _state = S_PAYLOAD;
                } else {
                    _state = S_CRC;
                }
                break;

            case S_PAYLOAD: {
                uint8_t payloadLen = _frameLen - 2u;
                if (_pIdx < sizeof(_payload)) {
                    _payload[_pIdx] = c;
                }
                _computedCRC = _crc8(_computedCRC, c);
                _pIdx++;
                if (_pIdx >= payloadLen) {
                    _state = S_CRC;
                }
                break;
            }

            case S_CRC: {
                _state = S_ADDR;
                if (c != _computedCRC) return 0u;
                uint8_t payLen = _frameLen - 2u;
                if (_type == CRSF_FRAMETYPE_GPS        && payLen == CRSF_GPS_PAYLOAD_LEN)        return 1u;
                if (_type == CRSF_FRAMETYPE_LINK_STATS && payLen == CRSF_LINK_STATS_PAYLOAD_LEN) return 2u;
                return 0u;
            }
        }
        return 0u;
    }

    void _decodeLinkStats(LinkStats &out) {
        // Pick RSSI from the active antenna (byte 4 selects 0=ant1, 1=ant2)
        uint8_t rssiRaw  = (_payload[4] == 0u) ? _payload[0] : _payload[1];
        out.uplinkRSSI   = (rssiRaw == 0u) ? 0 : -static_cast<int8_t>(rssiRaw);
        out.uplinkLQ     = _payload[2];
        out.uplinkSNR    = static_cast<int8_t>(_payload[3]);
        out.rfMode       = _payload[5];
        out.txPowerIdx   = _payload[6];
        out.valid        = true;
        out.lastUpdate   = millis();
    }

    void _decodeGPS(PlaneGPS &out) {
        // All CRSF fields are big-endian
        int32_t  lat     = static_cast<int32_t>(
                               (uint32_t)_payload[0] << 24 |
                               (uint32_t)_payload[1] << 16 |
                               (uint32_t)_payload[2] <<  8 |
                                         _payload[3]);

        int32_t  lon     = static_cast<int32_t>(
                               (uint32_t)_payload[4] << 24 |
                               (uint32_t)_payload[5] << 16 |
                               (uint32_t)_payload[6] <<  8 |
                                         _payload[7]);

        uint16_t spdRaw  = static_cast<uint16_t>(
                               ((uint16_t)_payload[8]  << 8) | _payload[9]);
        uint16_t hdgRaw  = static_cast<uint16_t>(
                               ((uint16_t)_payload[10] << 8) | _payload[11]);
        uint16_t altRaw  = static_cast<uint16_t>(
                               ((uint16_t)_payload[12] << 8) | _payload[13]);

        uint8_t  sats    = _payload[14];

        out.lat        = static_cast<double>(lat) * 1e-7;
        out.lon        = static_cast<double>(lon) * 1e-7;
        out.alt        = static_cast<float>(altRaw) - 1000.0f;  // remove 1000 m offset
        out.relAlt     = out.alt;   // CRSF GPS has no relative-altitude field
        out.speedKmh   = spdRaw / 10.0f;
        out.headingDeg = hdgRaw / 100.0f;
        out.valid      = (sats >= 4);
        out.lastUpdate = millis();
    }
};
