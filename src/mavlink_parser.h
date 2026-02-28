#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Lightweight MAVLink v1 parser
//
// Extracts GLOBAL_POSITION_INT (message ID 33) which carries the plane's
// GPS coordinates, altitude, and heading from any ArduPilot-based autopilot.
//
// MAVLink v1 frame layout:
//   STX(0xFE) | LEN | SEQ | SYS | COMP | MSGID | PAYLOAD[LEN] | CKA | CKB
//
// Checksum: CRC-16/MCRF4XX (X25) over LEN+SEQ+SYS+COMP+MSGID+PAYLOAD,
//           then CRC_EXTRA byte appended, result split into CKA (low), CKB (high).
// ─────────────────────────────────────────────────────────────────────────────

#define MAVLINK_STX                 0xFEu
#define MSG_GLOBAL_POSITION_INT     33u
#define CRC_EXTRA_GLOBAL_POS_INT    104u   // CRC_EXTRA for message ID 33
#define PAYLOAD_LEN_GLOBAL_POS_INT  28u    // fixed payload size for this message

// GLOBAL_POSITION_INT payload layout (28 bytes, MAVLink v1 wire order):
//   Offset  Size  Field
//   0       4     time_boot_ms  (uint32, ms)
//   4       4     lat           (int32,  1E7 deg)
//   8       4     lon           (int32,  1E7 deg)
//   12      4     alt           (int32,  mm MSL)
//   16      4     relative_alt  (int32,  mm above home)
//   20      2     vx            (int16,  cm/s)
//   22      2     vy            (int16,  cm/s)
//   24      2     vz            (int16,  cm/s)
//   26      2     hdg           (uint16, centi-degrees, 0=North)

struct PlaneGPS {
    double   lat;         // degrees
    double   lon;         // degrees
    float    alt;         // metres, MSL
    float    relAlt;      // metres above home / takeoff point
    bool     valid;
    uint32_t lastUpdate;  // millis() timestamp of last good packet
};

class MAVLinkParser {
public:
    explicit MAVLinkParser(HardwareSerial &serial) : _ser(serial) {}

    // Call every loop iteration. Populates `out` when a valid packet arrives.
    void update(PlaneGPS &out) {
        while (_ser.available()) {
            if (_parseByte(static_cast<uint8_t>(_ser.read()))) {
                _decodeGlobalPositionInt(out);
            }
        }
    }

private:
    HardwareSerial &_ser;

    enum State : uint8_t {
        S_IDLE, S_LEN, S_SEQ, S_SYS, S_COMP, S_MSGID, S_PAYLOAD, S_CKA, S_CKB
    };

    State   _state   = S_IDLE;
    uint8_t _len     = 0;
    uint8_t _msgId   = 0;
    uint8_t _pIdx    = 0;
    uint8_t _payload[PAYLOAD_LEN_GLOBAL_POS_INT];
    uint16_t _crc    = 0xFFFFu;
    uint8_t _ckaRx   = 0;

    // CRC-16/MCRF4XX (X25) accumulate one byte
    static uint16_t _x25(uint16_t crc, uint8_t d) {
        uint8_t tmp = d ^ (crc & 0xFFu);
        tmp ^= static_cast<uint8_t>(tmp << 4);
        return static_cast<uint16_t>(
            (crc >> 8) ^ (static_cast<uint16_t>(tmp) << 8)
                       ^ (static_cast<uint16_t>(tmp) << 3)
                       ^ (tmp >> 4)
        );
    }

    // Returns true when a complete, valid GLOBAL_POSITION_INT packet is ready.
    bool _parseByte(uint8_t c) {
        switch (_state) {

            case S_IDLE:
                if (c == MAVLINK_STX) {
                    _crc = 0xFFFFu;
                    _state = S_LEN;
                }
                break;

            case S_LEN:
                _len = c;
                _crc = _x25(_crc, c);
                _state = S_SEQ;
                break;

            case S_SEQ:
                _crc = _x25(_crc, c);
                _state = S_SYS;
                break;

            case S_SYS:
                _crc = _x25(_crc, c);
                _state = S_COMP;
                break;

            case S_COMP:
                _crc = _x25(_crc, c);
                _state = S_MSGID;
                break;

            case S_MSGID:
                _msgId = c;
                _crc   = _x25(_crc, c);
                _pIdx  = 0;
                _state = (_len > 0) ? S_PAYLOAD : S_CKA;
                break;

            case S_PAYLOAD:
                if (_pIdx < sizeof(_payload)) {
                    _payload[_pIdx] = c;
                }
                _pIdx++;
                _crc = _x25(_crc, c);
                if (_pIdx >= _len) {
                    _state = S_CKA;
                }
                break;

            case S_CKA:
                // Finalise CRC with message-specific CRC_EXTRA before comparing
                if (_msgId == MSG_GLOBAL_POSITION_INT) {
                    _crc = _x25(_crc, CRC_EXTRA_GLOBAL_POS_INT);
                }
                _ckaRx = c;
                _state = S_CKB;
                break;

            case S_CKB: {
                _state = S_IDLE;
                uint8_t ckaCalc = static_cast<uint8_t>(_crc & 0xFFu);
                uint8_t ckbCalc = static_cast<uint8_t>((_crc >> 8) & 0xFFu);
                return (_ckaRx          == ckaCalc  &&
                        c               == ckbCalc  &&
                        _msgId          == MSG_GLOBAL_POSITION_INT &&
                        _len            == PAYLOAD_LEN_GLOBAL_POS_INT);
            }
        }
        return false;
    }

    void _decodeGlobalPositionInt(PlaneGPS &out) {
        int32_t lat, lon, alt, relAlt;
        memcpy(&lat,    _payload + 4,  4);
        memcpy(&lon,    _payload + 8,  4);
        memcpy(&alt,    _payload + 12, 4);
        memcpy(&relAlt, _payload + 16, 4);

        out.lat        = static_cast<double>(lat) * 1e-7;
        out.lon        = static_cast<double>(lon) * 1e-7;
        out.alt        = static_cast<float>(alt)    * 0.001f;
        out.relAlt     = static_cast<float>(relAlt) * 0.001f;
        out.valid      = true;
        out.lastUpdate = millis();
    }
};
