// DXSpotter — DXpeditionFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fetches active/upcoming DXpeditions from Holy Cluster REST API.
// Refreshes every 10 minutes via FreeRTOS background task.
// URL: https://holycluster.iarc.org/dxpeditions

#pragma once
#include <Arduino.h>

namespace dxs {

static constexpr int DXPED_MAX = 20;

struct DXpedition {
    char callsign[16];
    char entity  [40];
    char startDate[12];  // "YYYY-MM-DD" or first 10 chars of ISO string
    char endDate  [12];
    bool isActive;
};

class DXpeditionFeed {
public:
    static void init();

    // Request a fresh fetch. Non-blocking — actual HTTP runs in background task.
    static void requestFetch();

    static bool     hasNewData();
    static int      getDXpeditions(DXpedition* buf, int maxCount);
    static uint32_t lastFetchMs();
    static int      lastStatus();

private:
    static void _fetchTask(void*);
    static void _parse(const char* json);
};

}  // namespace dxs
