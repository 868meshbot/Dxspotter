// DXSpotter — DXFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fetches DX spots from a configurable HTTP/JSON source.
// Runs the HTTP fetch in a background FreeRTOS task to avoid blocking the UI.
//
// Supported sources:
//   DX_FEED_DXWATCH  — https://dxwatch.com/dxsd1/s.php?s=0&r=20 (JSON)
//   DX_FEED_DXHEAT   — https://dxheat.com/dxc/?entries=20 (JSON)
//   DX_FEED_CUSTOM   — user-defined URL (JSON, same format as dxwatch)

#pragma once
#include <Arduino.h>

namespace dxs {

static constexpr int DX_MAX_SPOTS = 30;

struct DXSpot {
    char dxCall [16];
    char spotter[16];
    char freq   [12];   // kHz as string, e.g. "14007.0"
    char comment[40];
    char utcTime[6];    // "HHmm" or "HH:MM"
    char mode   [8];    // CW, SSB, FT8, etc. (empty if unknown)
};

class DXFeed {
public:
    static void init();

    // Request a fresh fetch. Non-blocking — actual HTTP runs in background task.
    static void requestFetch();

    // Returns true if new spots are available since last getSpots() call.
    static bool hasNewData();

    // Fills buf with up to maxCount spots. Returns number actually filled.
    // Clears the hasNewData flag.
    static int getSpots(DXSpot* buf, int maxCount);

    // UTC millis() of last successful fetch (0 if never)
    static uint32_t lastFetchMs();

    // Last HTTP status code (200=OK, 0=never fetched, -1=error)
    static int lastStatus();

private:
    static void _fetchTask(void*);
    static void _parseDXWatch(const char* body);
    static void _parseDXHeat(const char* body);
    static void _parseDXScape(const char* body);  // dxscape.com HTML cluster pages
    static void _parseDXLite(const char* body);    // dxlite.g7vjr.org HTML cluster table
};

}  // namespace dxs
