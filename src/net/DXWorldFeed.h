// DXSpotter — DXWorldFeed.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scrapes the DX-World.net "Featured DXpeditions" monthly timeline (served as an
// HTML/JS Gantt chart by hamradiotimeline.com) into the shared DXpedition struct.
// Page: https://www.hamradiotimeline.com/timeline/dxw_timeline_1_1.php
//
// The page is Cloudflare-fronted and bot-blocks non-browser User-Agents, so the
// fetch sends a desktop-browser UA + Referer.

#pragma once
#include <Arduino.h>
#include "DXpeditionFeed.h"   // reuse `struct DXpedition` and DXPED_MAX

namespace dxs {

class DXWorldFeed {
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
    static void _parse(const char* html);
};

}  // namespace dxs
