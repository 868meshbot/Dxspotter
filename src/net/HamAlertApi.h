// DXSpotter — HamAlertApi.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Manages HamAlert.org *triggers* (configured callsign alerts) over the site's
// AJAX web API — list, add and delete — mirroring the DX2HamAlert importer
// script. This is distinct from HamAlertFeed (which streams triggered spots
// over telnet): the API talks HTTPS to hamalert.org and authenticates with the
// HamAlert account/web password (config.hamAlertPass), NOT the telnet password.
//
//   login          POST https://hamalert.org/login           (form, sets PHPSESSID)
//   list triggers  GET  https://hamalert.org/ajax/triggers    (JSON array)
//   add  trigger   POST https://hamalert.org/ajax/trigger_update (JSON template)
//   delete trigger POST https://hamalert.org/ajax/trigger_delete (form id=<_id>)
//
// All network work runs on a single background worker task; the UI fires async
// requests and polls status().

#pragma once
#include <Arduino.h>

namespace dxs {

static constexpr int HA_MAX_TRIGGERS = 40;

struct HATrigger {
    char id      [28];   // Mongo ObjectId (24 hex chars)
    char callsign[20];
    char comment [40];
};

class HamAlertApi {
public:
    enum Status { IDLE, BUSY, OK, ERR };

    static void init();

    // Async requests — wake the worker task, then poll status() until != BUSY.
    static void requestList();
    // comment == nullptr/"" → a default "DXSpotter" comment is used.
    static void requestAdd(const char* callsign, const char* comment = nullptr);
    static void requestDelete(const char* id);

    static Status      status();      // result of the most recent request
    static const char* message();     // short human-readable status/error text

    // Copy the triggers fetched by the most recent successful LIST. Returns count.
    static int getTriggers(HATrigger* buf, int maxCount);

private:
    static void _worker(void*);
};

}  // namespace dxs
