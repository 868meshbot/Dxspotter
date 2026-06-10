// DXSpotter — SatFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Upcoming amateur-satellite passes from hams.at.
//   GET https://hams.at/api/alerts/upcoming
//   Authorization: Bearer <hamsatKey>   (config.hamsatKey, a UUID)
// hams.at computes each pass (AOS/LOS, max elevation) for the observer location
// saved on the account linked to the key — no SGP4/TLE work happens on-device.
// Background task, 10-min periodic + on-demand (requestFetch).

#pragma once
#include <Arduino.h>
#include <time.h>

namespace dxs {

static constexpr int SAT_MAX_PASSES = 20;

struct SatPass {
    char   satName [20];
    char   callsign[16];
    char   mode    [12];
    int    satNumber;
    double mhz;            // downlink/uplink centre as reported (0 if absent)
    float  maxElevation;   // degrees (0 if absent)
    bool   workable;       // hams.at "is_workable" for this observer
    time_t aosUtc;         // 0 if unparsed
    time_t losUtc;         // 0 if unparsed
};

class SatFeed {
public:
    static void init();
    static void requestFetch();
    static bool hasNewData();
    static int  getPasses(SatPass* buf, int maxCount);
    static uint32_t lastFetchMs();
    static int  lastStatus();   // HTTP status; -1 transport error, 0 = pending

private:
    static void _fetchTask(void*);
    static void _parse(const char* json);
};

}  // namespace dxs
