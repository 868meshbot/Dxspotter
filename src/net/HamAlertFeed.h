// DXSpotter — HamAlertFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Streams a user's triggered spots from HamAlert in real time over its telnet
// DX-cluster feed.
// Host/port: hamalert.org:7300
// Auth: HamAlert account username + password (sent at the login/password prompts).
// Spots arrive as standard "DX de ..." cluster lines and are parsed live.

#pragma once
#include <Arduino.h>

namespace dxs {

static constexpr int HA_MAX_ALERTS = 25;

struct HamAlert {
    char dxCall  [16];
    char freq    [12];   // MHz as string e.g. "14.025"
    char mode    [8];
    char comment [48];
    char spotter [16];
    char utcTime [6];
    char source  [16];  // "HamAlert", cluster name, etc.
};

class HamAlertFeed {
public:
    static void init();
    static void requestFetch();
    static bool hasNewData();
    static int  getAlerts(HamAlert* buf, int maxCount);
    static uint32_t lastFetchMs();
    static int  lastStatus();

    // Monotonic count of all alerts inserted since boot — lets the UI detect a
    // genuinely new alert (vs. the hasNewData flag, which the list rebuild clears).
    static uint32_t totalInserted();
    // Copy the most recent alert into `out`. Returns false if none yet.
    static bool getLatest(HamAlert& out);

private:
    static void _fetchTask(void*);
    static void _parseJson(const char* body);
};

}  // namespace dxs
