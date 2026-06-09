// DXSpotter — HamAlertApi.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HTTPS client for managing HamAlert.org triggers (list / add / delete), a port
// of the DX2HamAlert importer script. Logs into hamalert.org with the account
// username + web password, captures the PHPSESSID session cookie, then drives
// the same /ajax endpoints the website uses. All work runs on one worker task.

#include "HamAlertApi.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace dxs {

// Browser User-Agent — HamAlert (like several feeds) is picky about non-browser
// clients; this matches the importer script.
static constexpr const char* UA =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/139.0.0.0 Safari/537.36";

static constexpr const char* URL_LOGIN  = "https://hamalert.org/login";
static constexpr const char* URL_LIST   = "https://hamalert.org/ajax/triggers";
static constexpr const char* URL_UPDATE = "https://hamalert.org/ajax/trigger_update";
static constexpr const char* URL_DELETE = "https://hamalert.org/ajax/trigger_delete";

// ── State (worker-owned; UI reads via the mutex) ──────────────────────
static SemaphoreHandle_t       s_mutex     = nullptr;
static TaskHandle_t            s_task      = nullptr;
static volatile int            s_reqOp     = 0;        // 0 none, 1 list, 2 add, 3 delete
static char                    s_reqArg[28] = {0};     // callsign (add) or _id (delete)
static char                    s_reqComment[48] = {0}; // trigger comment (add)
static volatile HamAlertApi::Status s_status = HamAlertApi::IDLE;
static char                    s_msg[64]   = {0};

static HATrigger               s_triggers[HA_MAX_TRIGGERS];
static int                     s_trigCount = 0;
static char                    s_userId[28] = {0};     // Mongo account id from trigger list

// ── Helpers ────────────────────────────────────────────────────────────
static void _setResult(HamAlertApi::Status st, const char* msg) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status = st;
    strncpy(s_msg, msg, sizeof(s_msg) - 1);
    s_msg[sizeof(s_msg) - 1] = '\0';
    xSemaphoreGive(s_mutex);
}

static String _urlEncode(const char* s) {
    String out;
    char buf[4];
    for (const char* p = s; *p; ++p) {
        char c = *p;
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            out += buf;
        }
    }
    return out;
}

// POST /login with form creds; capture the PHPSESSID Set-Cookie into cookieOut
// as "PHPSESSID=...". Returns false if no cookie came back.
static bool _login(WiFiClientSecure& ssl, char* cookieOut, size_t sz) {
    const auto& cfg = config::get();
    HTTPClient http;
    if (!http.begin(ssl, URL_LOGIN)) return false;
    http.setTimeout(12000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);  // keep the login response's Set-Cookie
    const char* collect[] = { "Set-Cookie" };
    http.collectHeaders(collect, 1);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Origin",       "https://hamalert.org");
    http.addHeader("Referer",      "https://hamalert.org/login");
    http.addHeader("User-Agent",   UA);

    String body = "username=" + _urlEncode(cfg.hamAlertUser) +
                  "&password=" + _urlEncode(cfg.hamAlertPass);
    int code = http.POST(body);
    String setCookie = http.header("Set-Cookie");
    http.end();

    int semi = setCookie.indexOf(';');
    if (semi > 0) setCookie = setCookie.substring(0, semi);
    setCookie.trim();
    DXS_LOG("HamAlertApi", "login code=%d cookie=%s", code,
            setCookie.length() ? "yes" : "none");
    if (setCookie.length() == 0) return false;
    strncpy(cookieOut, setCookie.c_str(), sz - 1);
    cookieOut[sz - 1] = '\0';
    return cookieOut[0] != '\0';
}

// GET /ajax/triggers → fill s_triggers + s_userId. Returns count, or -1 on error
// (also when the session isn't authenticated and JSON parsing fails).
static int _fetchTriggers(WiFiClientSecure& ssl, const char* cookie) {
    HTTPClient http;
    if (!http.begin(ssl, URL_LIST)) return -1;
    http.setTimeout(12000);
    http.addHeader("Cookie",           cookie);
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("Referer",          "https://hamalert.org/triggers");
    http.addHeader("Accept",           "*/*");
    http.addHeader("User-Agent",       UA);
    int code = http.GET();
    if (code != 200) { http.end(); DXS_LOG("HamAlertApi", "list code=%d", code); return -1; }
    String body = http.getString();
    http.end();

    // Filter keeps memory bounded — we only need id/user_id/callsign/comment.
    JsonDocument filter;
    filter[0]["_id"]                   = true;
    filter[0]["user_id"]               = true;
    filter[0]["comment"]               = true;
    filter[0]["conditions"]["callsign"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (err) { DXS_LOG("HamAlertApi", "list JSON err: %s", err.c_str()); return -1; }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) { DXS_LOG("HamAlertApi", "list: not a JSON array"); return -1; }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_trigCount  = 0;
    s_userId[0]  = '\0';
    for (JsonObject it : arr) {
        if (s_trigCount >= HA_MAX_TRIGGERS) break;
        const char* id  = it["_id"]     | "";
        const char* uid = it["user_id"] | "";
        if (uid[0] && s_userId[0] == '\0')
            strncpy(s_userId, uid, sizeof(s_userId) - 1);

        // conditions.callsign may be a single string or an array of calls.
        String csStr;
        JsonVariant csv = it["conditions"]["callsign"];
        if (csv.is<JsonArray>()) {
            for (JsonVariant v : csv.as<JsonArray>()) {
                const char* one = v | "";
                if (!one[0]) continue;
                if (csStr.length()) csStr += ",";
                csStr += one;
            }
        } else {
            csStr = csv | "";
        }
        if (id[0] == '\0' || csStr.length() == 0) continue;

        HATrigger& t = s_triggers[s_trigCount++];
        memset(&t, 0, sizeof(t));
        strncpy(t.id,       id,             sizeof(t.id) - 1);
        strncpy(t.callsign, csStr.c_str(),  sizeof(t.callsign) - 1);
        strncpy(t.comment,  it["comment"] | "", sizeof(t.comment) - 1);
    }
    int n = s_trigCount;
    xSemaphoreGive(s_mutex);
    DXS_LOG("HamAlertApi", "list ok: %d triggers, user_id=%s", n,
            s_userId[0] ? "yes" : "none");
    return n;
}

// POST /ajax/trigger_update with the template payload (callsign substituted).
// Requires s_userId to have been populated by a prior _fetchTriggers().
static bool _add(WiFiClientSecure& ssl, const char* cookie, const char* callsign,
                 const char* comment) {
    JsonDocument doc;
    doc["user_id"] = s_userId;
    JsonObject cond = doc["conditions"].to<JsonObject>();
    cond["callsign"] = callsign;
    JsonArray dxcc = cond["spotterDxcc"].to<JsonArray>();
    static const int kDxcc[] = { 209, 223, 106, 245, 114, 122, 265, 279, 294 };
    for (int v : kDxcc) dxcc.add(v);
    JsonArray mode = cond["mode"].to<JsonArray>();
    for (const char* m : { "ssb", "psk", "ft8", "ft4" }) mode.add(m);
    JsonArray notSp = cond["notSpotter"].to<JsonArray>();
    for (const char* m : { "2E0INH", "G4IRN", "GM4WJA", "ON5KQ" }) notSp.add(m);
    JsonArray band = cond["band"].to<JsonArray>();
    for (const char* b : { "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m" })
        band.add(b);
    JsonArray act = doc["actions"].to<JsonArray>();
    act.add("app");
    act.add("telnet");   // include telnet so the alert also reaches the device's feed
    doc["comment"]    = (comment && comment[0]) ? comment : "DXSpotter";
    doc["matchCount"] = 0;
    doc["options"].to<JsonObject>();

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    if (!http.begin(ssl, URL_UPDATE)) return false;
    http.setTimeout(12000);
    http.addHeader("Content-Type",     "application/json");
    http.addHeader("Cookie",           cookie);
    http.addHeader("Origin",           "https://hamalert.org");
    http.addHeader("Referer",          "https://hamalert.org/triggers");
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("User-Agent",       UA);
    int code = http.POST(payload);
    String resp = http.getString();
    http.end();
    DXS_LOG("HamAlertApi", "add %s -> %d %.40s", callsign, code, resp.c_str());
    return code == 200;
}

// POST /ajax/trigger_delete with form body id=<_id>.
static bool _delete(WiFiClientSecure& ssl, const char* cookie, const char* id) {
    HTTPClient http;
    if (!http.begin(ssl, URL_DELETE)) return false;
    http.setTimeout(12000);
    http.addHeader("Content-Type",     "application/x-www-form-urlencoded");
    http.addHeader("Cookie",           cookie);
    http.addHeader("Origin",           "https://hamalert.org");
    http.addHeader("Referer",          "https://hamalert.org/triggers");
    http.addHeader("X-Requested-With", "XMLHttpRequest");
    http.addHeader("User-Agent",       UA);
    String body = String("id=") + id;
    int code = http.POST(body);
    String resp = http.getString();
    http.end();
    resp.trim();
    DXS_LOG("HamAlertApi", "delete %s -> %d %.40s", id, code, resp.c_str());
    return code == 200 && resp.indexOf("true") >= 0;
}

// ── Worker task ────────────────────────────────────────────────────────
void HamAlertApi::_worker(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int  op;
        char arg[sizeof(s_reqArg)];
        char comment[sizeof(s_reqComment)];
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        op = s_reqOp;
        strncpy(arg,     s_reqArg,     sizeof(arg));
        strncpy(comment, s_reqComment, sizeof(comment));
        s_reqOp = 0;
        xSemaphoreGive(s_mutex);
        if (op == 0) continue;

        if (WiFi.status() != WL_CONNECTED) { _setResult(ERR, "No WiFi"); continue; }
        const auto& cfg = config::get();
        if (cfg.hamAlertUser[0] == '\0' || cfg.hamAlertPass[0] == '\0') {
            _setResult(ERR, "Set HamAlert user + password");
            continue;
        }

        WiFiClientSecure ssl;
        ssl.setInsecure();
        char cookie[96] = {0};
        if (!_login(ssl, cookie, sizeof(cookie))) { _setResult(ERR, "Login failed"); continue; }

        bool ok = false;
        char msg[64] = {0};
        switch (op) {
        case 1: {  // list
            int n = _fetchTriggers(ssl, cookie);
            ok = (n >= 0);
            if (ok) snprintf(msg, sizeof(msg), "%d triggers", n);
            else    strncpy(msg, "Could not load triggers", sizeof(msg) - 1);
            break;
        }
        case 2: {  // add
            int n = _fetchTriggers(ssl, cookie);   // need user_id from the list
            if (n < 0 || s_userId[0] == '\0') {
                ok = false;
                strncpy(msg, "Login failed — check password", sizeof(msg) - 1);
            } else {
                ok = _add(ssl, cookie, arg, comment);
                snprintf(msg, sizeof(msg), ok ? "Added %s" : "Add failed", arg);
            }
            break;
        }
        case 3: {  // delete
            ok = _delete(ssl, cookie, arg);
            strncpy(msg, ok ? "Deleted" : "Delete failed", sizeof(msg) - 1);
            break;
        }
        }
        _setResult(ok ? OK : ERR, msg);
    }
}

// ── Public API ─────────────────────────────────────────────────────────
void HamAlertApi::init() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreate(_worker, "ha_api", 8192, nullptr, 1, &s_task);
    DXS_LOG("HamAlertApi", "Initialised");
}

static void _enqueue(int op, const char* arg, const char* comment) {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_reqOp = op;
    if (arg) { strncpy(s_reqArg, arg, sizeof(s_reqArg) - 1); s_reqArg[sizeof(s_reqArg) - 1] = '\0'; }
    else     { s_reqArg[0] = '\0'; }
    if (comment) { strncpy(s_reqComment, comment, sizeof(s_reqComment) - 1); s_reqComment[sizeof(s_reqComment) - 1] = '\0'; }
    else         { s_reqComment[0] = '\0'; }
    s_status = HamAlertApi::BUSY;
    strncpy(s_msg, "Working...", sizeof(s_msg) - 1);
    xSemaphoreGive(s_mutex);
    if (s_task) xTaskNotifyGive(s_task);
}

void HamAlertApi::requestList()                                       { _enqueue(1, nullptr, nullptr);  }
void HamAlertApi::requestAdd(const char* call, const char* comment)   { _enqueue(2, call, comment);     }
void HamAlertApi::requestDelete(const char* id)                       { _enqueue(3, id, nullptr);       }

HamAlertApi::Status HamAlertApi::status()  { return s_status; }
const char*         HamAlertApi::message() { return s_msg;    }

int HamAlertApi::getTriggers(HATrigger* buf, int maxCount) {
    if (!buf || maxCount <= 0) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    int n = s_trigCount < maxCount ? s_trigCount : maxCount;
    memcpy(buf, s_triggers, (size_t)n * sizeof(HATrigger));
    xSemaphoreGive(s_mutex);
    return n;
}

}  // namespace dxs
