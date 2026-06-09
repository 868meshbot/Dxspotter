// DXSpotter — HolyClusterFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real-time DX spot feed via Holy Cluster WebSocket API.
// Provides both display spots and a band×continent activity heatmap.
//
// Protocol:
//   wss://holycluster.iarc.org/spots_ws
//   → send {"initial":true}          receive {"type":"initial","spots":[...]}
//   → send {"last_time":<unix_ts>}   receive {"type":"update","spots":[...]}

#pragma once
#include <Arduino.h>

namespace dxs {

static constexpr int HC_NUM_HF    = 10;  // 160,80,60,40,30,20,17,15,12,10
static constexpr int HC_NUM_VHF   = 6;   // 6,4,2,70cm,33cm,23cm
static constexpr int HC_NUM_BANDS = HC_NUM_HF + HC_NUM_VHF;  // 16 total
static constexpr int HC_NUM_CONTS = 6;  // EU,NA,AS,AF,OC,SA
static constexpr int HC_MAX_SPOTS = 30;
static constexpr int HC_RING_SIZE = 1000;  // rolling spot buffer for heatmap

extern const char* const HC_BAND_LABELS[HC_NUM_BANDS];  // "160m","80m",...
extern const char* const HC_CONT_LABELS[HC_NUM_CONTS];  // "EU","NA",...

struct HCSpot {
    char  dxCall [16];
    char  spotter[16];
    char  mode   [8];
    char  comment[40];
    char  dxContinent[4];
    char  utcTime[6];    // "HH:MM" derived from unix timestamp
    float freqKHz;
    bool  isDxpedition;
};

class HolyClusterFeed {
public:
    static void init();

    // Trigger an incremental WS request for spots newer than last received
    static void requestFetch();

    static bool     hasNewData();
    static int      getSpots(HCSpot* buf, int maxCount);
    static uint32_t lastFetchMs();
    static int      lastStatus();  // -1=disconnected, 0=connecting, 200=streaming

    // Band×continent activity in last 60 min (spot count, 0-255)
    static void getHeatmap(uint8_t out[HC_NUM_BANDS][HC_NUM_CONTS]);
    static bool heatmapUpdated();  // true once after new spots arrive

private:
    static void _wsTask(void*);
};

}  // namespace dxs
