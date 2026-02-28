#pragma once

#include <math.h>
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Tracker geometry: great-circle bearing, haversine distance, elevation angle
// ─────────────────────────────────────────────────────────────────────────────

static constexpr double EARTH_RADIUS_M = 6371000.0;

struct HomeGPS {
    double lat;    // degrees
    double lon;    // degrees
    float  alt;    // metres MSL
    bool   valid;
};

// Great-circle distance between two WGS-84 coordinates (metres).
inline double haversineDistance(double lat1, double lon1,
                                double lat2, double lon2) {
    double dLat = radians(lat2 - lat1);
    double dLon = radians(lon2 - lon1);
    double a = sin(dLat * 0.5) * sin(dLat * 0.5)
             + cos(radians(lat1)) * cos(radians(lat2))
             * sin(dLon * 0.5) * sin(dLon * 0.5);
    return EARTH_RADIUS_M * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// Forward bearing from point 1 → point 2, degrees [0, 360).
// 0 = North, 90 = East, 180 = South, 270 = West.
inline double calculateBearing(double lat1, double lon1,
                               double lat2, double lon2) {
    double dLon  = radians(lon2 - lon1);
    double rlat1 = radians(lat1);
    double rlat2 = radians(lat2);

    double y = sin(dLon) * cos(rlat2);
    double x = cos(rlat1) * sin(rlat2) - sin(rlat1) * cos(rlat2) * cos(dLon);

    double brng = degrees(atan2(y, x));
    if (brng < 0.0) brng += 360.0;
    return brng;
}

// Elevation angle from horizontal ground to a target above/below, degrees.
// distanceM : horizontal great-circle distance (metres)
// altDiffM  : plane altitude minus tracker altitude (metres, positive = above)
inline float calculateElevation(double distanceM, float altDiffM) {
    if (distanceM < 0.1) return 90.0f;   // directly overhead edge-case
    float elev = static_cast<float>(degrees(atan2(static_cast<double>(altDiffM),
                                                   distanceM)));
    return constrain(elev, 0.0f, 90.0f);
}

// Normalise an angle difference to [-180, +180].
// Used to find the shortest rotation direction for the pan servo.
inline float normalizeAngleDiff(float diff) {
    while (diff >  180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}
