// DXSpotter — SolarFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fetches solar weather data from hamqsl.com XML API.
// Source: https://www.hamqsl.com/solarxml.php

#pragma once
#include <Arduino.h>

namespace dxs {

struct SolarData {
    int   sfi;              // Solar Flux Index
    int   aindex;           // A-index (daily)
    int   kindex;           // K-index (3-hour)
    char  xray[8];          // X-ray flux class e.g. "A2.5"
    int   solarWindKmS;     // Solar wind speed km/s
    float magneticField;    // Interplanetary magnetic field (nT)
    int   aurora;           // Aurora latitude (degrees)
    // Propagation conditions: "Good", "Fair", "Poor"
    char  b80_40_day  [8];
    char  b80_40_night[8];
    char  b30_20_day  [8];
    char  b30_20_night[8];
    char  b17_15_day  [8];
    char  b17_15_night[8];
    char  b12_10_day  [8];
    char  b12_10_night[8];
    uint32_t fetchedAt;     // millis() of last successful fetch
};

class SolarFeed {
public:
    static void init();

    // Request a fresh fetch. Non-blocking — runs in background task.
    static void requestFetch();

    static bool hasNewData();

    // Fills data and clears hasNewData flag. Returns false if no data yet.
    static bool getData(SolarData& out);

    static int lastStatus();

private:
    static void _fetchTask(void*);
    static void _parseXml(const char* body);
    // Returns value for named XML tag, or defaultVal if not found.
    static int  _xmlInt  (const char* xml, const char* tag, int defaultVal = 0);
    static float _xmlFloat(const char* xml, const char* tag, float defaultVal = 0.f);
    static bool _xmlStr  (const char* xml, const char* tag, char* out, size_t sz);
    // Fills one band condition field from calculatedconditions XML.
    static bool _xmlBand (const char* xml, const char* name, const char* time, char* out, size_t sz);
};

}  // namespace dxs
