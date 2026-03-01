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
// UART: 420000 baud, 8N1, non-inverted (direct connection to ESP32)
// ─────────────────────────────────────────────────────────────────────────────

#define CRSF_BAUD_RATE          420000u

#define CRSF_FRAMETYPE_GPS      0x02u
#define CRSF_GPS_PAYLOAD_LEN    15u
// LEN field value for a GPS frame = TYPE(1) + PAYLOAD(15) + CRC(1) = 17
#define CRSF_GPS_FRAME_LEN      17u

#define CRSF_MAX_FRAME_LEN      64u    // maximum any CRSF frame can be

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

class CRSFParser {
public:
    explicit CRSFParser(HardwareSerial &serial) : _ser(serial) {}

    // Call every loop iteration. Populates `out` on each valid GPS packet.
    void update(PlaneGPS &out) {
        while (_ser.available()) {
            if (_parseByte(static_cast<uint8_t>(_ser.read()))) {
                _decodeGPS(out);
            }
        }
    }

private:
    HardwareSerial &_ser;

    enum State : uint8_t { S_ADDR, S_LEN, S_TYPE, S_PAYLOAD, S_CRC };

    State   _state        = S_ADDR;
    uint8_t _frameLen     = 0;   // value of the LEN field
    uint8_t _type         = 0;
    uint8_t _pIdx         = 0;
    uint8_t _payload[CRSF_GPS_PAYLOAD_LEN];
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

    // Returns true when a complete, CRC-valid GPS frame has been received.
    bool _parseByte(uint8_t c) {
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

            case S_CRC:
                _state = S_ADDR;
                // Valid if: CRC matches, type is GPS, payload length is correct
                return (c              == _computedCRC          &&
                        _type          == CRSF_FRAMETYPE_GPS    &&
                        (_frameLen - 2u) == CRSF_GPS_PAYLOAD_LEN);
        }
        return false;
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
