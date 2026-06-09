// DXSpotter — ScreenDXSpots.cpp
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenDXSpots.h"
#include "ScreenLauncher.h"
#include "Theme.h"
#include "../utils/Config.h"
#include "../utils/Log.h"
#include "../net/DXFeed.h"
#include "../net/HolyClusterFeed.h"
#include "../net/WiFiMgr.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>      // toupper
#include <ctime>       // relative "Ago" column
#include <strings.h>   // strcasecmp

namespace dxs { namespace ui {

static constexpr int TOP_H = 28;
static constexpr int BOT_H = 30;   // bottom action bar

lv_obj_t* ScreenDXSpots::_screen    = nullptr;
lv_obj_t* ScreenDXSpots::_list      = nullptr;
lv_obj_t* ScreenDXSpots::_statusLbl = nullptr;

// ── Band / mode filter ────────────────────────────────────────────────
// A spot is shown when its band bit is set in s_bandMask (or the mask is 0 =
// "all bands") AND likewise for s_modeMask. Spots whose mode is unknown/empty
// (e.g. DXwatch HTTP feed) always pass the mode filter so they are not hidden.
struct BandDef { const char* label; float lo; float hi; };  // freq in kHz
static const BandDef kBands[] = {
    { "160m",   1800.f,   2000.f },
    { "80m",    3500.f,   4000.f },
    { "60m",    5330.f,   5410.f },
    { "40m",    7000.f,   7300.f },
    { "30m",   10100.f,  10150.f },
    { "20m",   14000.f,  14350.f },
    { "17m",   18068.f,  18168.f },
    { "15m",   21000.f,  21450.f },
    { "12m",   24890.f,  24990.f },
    { "10m",   28000.f,  29700.f },
    { "6m",    50000.f,   54000.f },
    { "4m",    70000.f,   70500.f },
    { "2m",   144000.f,  148000.f },
    { "70cm", 420000.f,  450000.f },
    { "23cm",1240000.f, 1300000.f },
    { "13cm",2300000.f, 2450000.f },
};
static constexpr int kNumBands = (int)(sizeof(kBands) / sizeof(kBands[0]));

static const char* const kModes[] = {
    "CW", "SSB", "FT8", "FT4", "RTTY", "PSK", "FM", "AM", "SSTV", "DIGI"
};
static constexpr int kNumModes = (int)(sizeof(kModes) / sizeof(kModes[0]));

// ── Mode inference from frequency (IARU R1 / RSGB band plan) ──────────
// Narrow "watering-hole" frequencies (centre ± window kHz) — checked first.
struct ModePt  { float f; float w; const char* mode; };
static const ModePt kModePts[] = {
    // FT8
    {  1840.f, 2.f, "FT8" }, {  3573.f, 1.5f, "FT8" }, {  5357.f, 2.f, "FT8" },
    {  7074.f, 2.f, "FT8" }, { 10136.f, 2.f,  "FT8" }, { 14074.f, 2.f, "FT8" },
    { 18100.f, 2.f, "FT8" }, { 21074.f, 2.f,  "FT8" }, { 24915.f, 1.5f,"FT8" },
    { 28074.f, 2.f, "FT8" }, { 50313.f, 3.f,  "FT8" }, { 50323.f, 2.f, "FT8" },
    {144174.f, 3.f, "FT8" },
    // FT4
    {  3575.f, 1.f, "FT4" }, {  7047.5f,2.f, "FT4" }, { 10140.f, 1.5f,"FT4" },
    { 14080.f, 2.f, "FT4" }, { 18104.f, 1.5f,"FT4" }, { 21140.f, 2.f, "FT4" },
    { 24919.f, 1.5f,"FT4" }, { 28180.f, 2.f, "FT4" }, { 50318.f, 2.f, "FT4" },
    // SSTV
    {  3735.f, 3.f, "SSTV" }, {  7165.f, 3.f, "SSTV" }, {  7170.f, 3.f, "SSTV" },
    { 14230.f, 3.f, "SSTV" }, { 21340.f, 3.f, "SSTV" }, { 28680.f, 3.f, "SSTV" },
};
static constexpr int kNumModePts = (int)(sizeof(kModePts) / sizeof(kModePts[0]));

// Broad band-plan segments [lo, hi) kHz — checked after the narrow points.
struct ModeSeg { float lo; float hi; const char* mode; };
static const ModeSeg kModeSegs[] = {
    {  1810.f,  1838.f, "CW" },  {  1838.f,  1843.f, "RTTY" }, {  1843.f,  2000.f, "SSB" },  // 160m
    {  3500.f,  3570.f, "CW" },  {  3570.f,  3600.f, "RTTY" }, {  3600.f,  3800.f, "SSB" },  // 80m
    {  5351.f,  5354.f, "CW" },  {  5354.f,  5366.f, "SSB" },                                // 60m
    {  7000.f,  7040.f, "CW" },  {  7040.f,  7060.f, "RTTY" }, {  7060.f,  7200.f, "SSB" },  // 40m
    { 10100.f, 10130.f, "CW" },  { 10130.f, 10150.f, "RTTY" },                               // 30m
    { 14000.f, 14070.f, "CW" },  { 14070.f, 14099.f, "RTTY" }, { 14099.f, 14350.f, "SSB" }, // 20m
    { 18068.f, 18095.f, "CW" },  { 18095.f, 18110.f, "RTTY" }, { 18110.f, 18168.f, "SSB" }, // 17m
    { 21000.f, 21070.f, "CW" },  { 21070.f, 21110.f, "RTTY" }, { 21110.f, 21450.f, "SSB" }, // 15m
    { 24890.f, 24915.f, "CW" },  { 24915.f, 24931.f, "RTTY" }, { 24931.f, 24990.f, "SSB" }, // 12m
    { 28000.f, 28070.f, "CW" },  { 28070.f, 28190.f, "RTTY" }, { 28190.f, 29200.f, "SSB" },
    { 29200.f, 29700.f, "FM" },                                                             // 10m
    { 50000.f, 50100.f, "CW" },  { 50100.f, 51000.f, "SSB" },  { 51000.f, 54000.f, "FM" },  // 6m
    { 70000.f, 70100.f, "CW" },  { 70100.f, 70250.f, "SSB" },  { 70250.f, 70500.f, "FM" },  // 4m
    {144000.f,144150.f, "CW" },  {144150.f,144500.f, "SSB" },  {144500.f,146000.f, "FM" },  // 2m
    {432000.f,432100.f, "CW" },  {432100.f,432500.f, "SSB" },  {433000.f,435000.f, "FM" },  // 70cm
};
static constexpr int kNumModeSegs = (int)(sizeof(kModeSegs) / sizeof(kModeSegs[0]));

static const char* _inferMode(float kHz) {
    if (kHz <= 0) return "";
    for (int i = 0; i < kNumModePts; i++)
        if (kHz >= kModePts[i].f - kModePts[i].w && kHz <= kModePts[i].f + kModePts[i].w)
            return kModePts[i].mode;
    for (int i = 0; i < kNumModeSegs; i++)
        if (kHz >= kModeSegs[i].lo && kHz < kModeSegs[i].hi)
            return kModeSegs[i].mode;
    return "";
}

// Mode reported by the feed, or inferred from frequency when the feed omits it.
static const char* _spotMode(const dxs::DXSpot& s) {
    if (s.mode[0]) return s.mode;
    return _inferMode((float)atof(s.freq));
}

static uint32_t  s_bandMask    = 0;   // 0 = show all bands
static uint32_t  s_modeMask    = 0;   // 0 = show all modes
static uint32_t  s_contMask    = 0;   // 0 = show all spotter continents ("DX de")
static lv_obj_t* s_filterPopup = nullptr;
static lv_obj_t* s_bandBtns[kNumBands] = {};
static lv_obj_t* s_modeBtns[kNumModes] = {};

static int _bandIndex(float kHz) {
    for (int i = 0; i < kNumBands; i++)
        if (kHz >= kBands[i].lo && kHz < kBands[i].hi) return i;
    return -1;
}

static int _modeIndex(const char* m) {
    if (!m || !m[0]) return -1;
    for (int i = 0; i < kNumModes; i++)
        if (strcasecmp(m, kModes[i]) == 0) return i;
    return -1;
}

// ── Spotter continent inference ("DX de" filter) ──────────────────────
// DX spots carry no continent for the *spotter*, so we infer it from the
// spotter's callsign prefix via a (deliberately pragmatic, not exhaustive)
// amateur prefix → continent table. Longest matching prefix wins, so broad
// blocks (e.g. "K" = USA/NA) can be overridden by narrower ones
// (e.g. "KH" = US Pacific/OC). Unknown prefixes return -1 and stay visible.
enum {
    CONT_EU = 0, CONT_NA, CONT_SA, CONT_AS, CONT_AF, CONT_OC, CONT_AN, CONT_COUNT
};
static const char* const kConts[CONT_COUNT] = {
    "EU", "NA", "SA", "AS", "AF", "OC", "AN"
};

struct PfxCont { const char* pfx; int cont; };
static const PfxCont kPfxConts[] = {
    // ── North America ──
    {"K", CONT_NA}, {"W", CONT_NA}, {"N", CONT_NA},
    {"AA", CONT_NA}, {"AB", CONT_NA}, {"AC", CONT_NA}, {"AD", CONT_NA},
    {"AE", CONT_NA}, {"AF", CONT_NA}, {"AG", CONT_NA}, {"AI", CONT_NA},
    {"AJ", CONT_NA}, {"AK", CONT_NA},
    {"KL", CONT_NA}, {"KP", CONT_NA}, {"KG4", CONT_NA}, {"AL", CONT_NA},
    {"NL", CONT_NA}, {"NP", CONT_NA}, {"WL", CONT_NA}, {"WP", CONT_NA},
    {"VE", CONT_NA}, {"VA", CONT_NA}, {"VO", CONT_NA}, {"VY", CONT_NA}, {"CY", CONT_NA},
    {"CF", CONT_NA}, {"CG", CONT_NA}, {"CH", CONT_NA}, {"CI", CONT_NA}, {"CJ", CONT_NA}, {"CK", CONT_NA},
    {"XE", CONT_NA}, {"XF", CONT_NA}, {"4A", CONT_NA}, {"4B", CONT_NA}, {"4C", CONT_NA},
    {"6D", CONT_NA}, {"6E", CONT_NA}, {"6F", CONT_NA}, {"6G", CONT_NA},
    {"6H", CONT_NA}, {"6I", CONT_NA}, {"6J", CONT_NA},
    {"TG", CONT_NA}, {"TI", CONT_NA}, {"HP", CONT_NA}, {"HR", CONT_NA},
    {"YN", CONT_NA}, {"YS", CONT_NA}, {"V3", CONT_NA},
    {"CO", CONT_NA}, {"CM", CONT_NA}, {"HH", CONT_NA}, {"HI", CONT_NA},
    {"6Y", CONT_NA}, {"8P", CONT_NA}, {"9Y", CONT_NA}, {"C6", CONT_NA},
    {"FG", CONT_NA}, {"FM", CONT_NA}, {"FS", CONT_NA}, {"FJ", CONT_NA}, {"FP", CONT_NA},
    {"J3", CONT_NA}, {"J6", CONT_NA}, {"J7", CONT_NA}, {"J8", CONT_NA},
    {"PJ", CONT_NA}, {"V2", CONT_NA}, {"V4", CONT_NA},
    {"VP2", CONT_NA}, {"VP5", CONT_NA}, {"VP9", CONT_NA}, {"ZF", CONT_NA}, {"OX", CONT_NA},
    // ── South America ──
    {"PY", CONT_SA}, {"PP", CONT_SA}, {"PQ", CONT_SA}, {"PR", CONT_SA}, {"PS", CONT_SA},
    {"PT", CONT_SA}, {"PU", CONT_SA}, {"PV", CONT_SA}, {"PW", CONT_SA}, {"PX", CONT_SA}, {"PZ", CONT_SA},
    {"ZV", CONT_SA}, {"ZW", CONT_SA}, {"ZX", CONT_SA}, {"ZY", CONT_SA}, {"ZZ", CONT_SA},
    {"LU", CONT_SA}, {"LO", CONT_SA}, {"LP", CONT_SA}, {"LQ", CONT_SA}, {"LR", CONT_SA},
    {"LS", CONT_SA}, {"LT", CONT_SA}, {"LV", CONT_SA}, {"LW", CONT_SA}, {"AY", CONT_SA}, {"AZ", CONT_SA},
    {"CE", CONT_SA}, {"CA", CONT_SA}, {"CB", CONT_SA}, {"CC", CONT_SA}, {"CD", CONT_SA},
    {"XQ", CONT_SA}, {"XR", CONT_SA}, {"3G", CONT_SA},
    {"CX", CONT_SA}, {"CV", CONT_SA},
    {"HK", CONT_SA}, {"HJ", CONT_SA}, {"5J", CONT_SA}, {"5K", CONT_SA},
    {"HC", CONT_SA}, {"HD", CONT_SA},
    {"OA", CONT_SA}, {"OB", CONT_SA}, {"OC", CONT_SA}, {"4T", CONT_SA},
    {"YV", CONT_SA}, {"YW", CONT_SA}, {"YX", CONT_SA}, {"YY", CONT_SA}, {"4M", CONT_SA},
    {"CP", CONT_SA}, {"ZP", CONT_SA}, {"8R", CONT_SA}, {"FY", CONT_SA}, {"P4", CONT_SA}, {"VP8", CONT_SA},
    // ── Europe ──
    {"G", CONT_EU}, {"M", CONT_EU}, {"2E", CONT_EU},
    {"F", CONT_EU}, {"I", CONT_EU},
    {"EA", CONT_EU}, {"EB", CONT_EU}, {"EC", CONT_EU}, {"ED", CONT_EU}, {"EE", CONT_EU},
    {"EF", CONT_EU}, {"EG", CONT_EU}, {"EH", CONT_EU},
    {"EA8", CONT_AF}, {"EA9", CONT_AF}, {"EB8", CONT_AF}, {"EC8", CONT_AF}, {"ED8", CONT_AF},
    {"CT", CONT_EU}, {"CU", CONT_EU}, {"CR", CONT_EU}, {"CQ", CONT_EU},
    {"CT3", CONT_AF}, {"CR3", CONT_AF}, {"CQ3", CONT_AF},
    {"ON", CONT_EU}, {"OO", CONT_EU}, {"OP", CONT_EU}, {"OQ", CONT_EU}, {"OR", CONT_EU}, {"OS", CONT_EU}, {"OT", CONT_EU},
    {"PA", CONT_EU}, {"PB", CONT_EU}, {"PC", CONT_EU}, {"PD", CONT_EU}, {"PE", CONT_EU},
    {"PF", CONT_EU}, {"PG", CONT_EU}, {"PH", CONT_EU}, {"PI", CONT_EU},
    {"OE", CONT_EU}, {"OK", CONT_EU}, {"OL", CONT_EU}, {"OM", CONT_EU},
    {"OH", CONT_EU}, {"OF", CONT_EU}, {"OG", CONT_EU}, {"OI", CONT_EU}, {"OJ", CONT_EU},
    {"OZ", CONT_EU}, {"OU", CONT_EU}, {"OV", CONT_EU}, {"OW", CONT_EU}, {"5P", CONT_EU}, {"5Q", CONT_EU}, {"OY", CONT_EU},
    {"LA", CONT_EU}, {"LB", CONT_EU}, {"LC", CONT_EU}, {"LG", CONT_EU}, {"LH", CONT_EU},
    {"LI", CONT_EU}, {"LJ", CONT_EU}, {"LK", CONT_EU}, {"LL", CONT_EU}, {"LM", CONT_EU}, {"LN", CONT_EU},
    {"JW", CONT_EU}, {"JX", CONT_EU},
    {"SM", CONT_EU}, {"SA", CONT_EU}, {"SB", CONT_EU}, {"SC", CONT_EU}, {"SD", CONT_EU}, {"SE", CONT_EU},
    {"SF", CONT_EU}, {"SG", CONT_EU}, {"SH", CONT_EU}, {"SI", CONT_EU}, {"SJ", CONT_EU}, {"SK", CONT_EU},
    {"SL", CONT_EU}, {"7S", CONT_EU}, {"8S", CONT_EU},
    {"ES", CONT_EU}, {"YL", CONT_EU}, {"LY", CONT_EU},
    {"SP", CONT_EU}, {"SN", CONT_EU}, {"SO", CONT_EU}, {"SQ", CONT_EU}, {"SR", CONT_EU}, {"3Z", CONT_EU}, {"HF", CONT_EU},
    {"HA", CONT_EU}, {"HG", CONT_EU}, {"HB", CONT_EU},
    {"S5", CONT_EU}, {"9A", CONT_EU}, {"E7", CONT_EU},
    {"YU", CONT_EU}, {"YT", CONT_EU}, {"YZ", CONT_EU}, {"ZA", CONT_EU}, {"Z3", CONT_EU}, {"Z6", CONT_EU}, {"4O", CONT_EU},
    {"LZ", CONT_EU}, {"YO", CONT_EU}, {"YP", CONT_EU}, {"YQ", CONT_EU}, {"YR", CONT_EU}, {"ER", CONT_EU},
    {"SV", CONT_EU}, {"SW", CONT_EU}, {"SX", CONT_EU}, {"SY", CONT_EU}, {"SZ", CONT_EU}, {"J4", CONT_EU},
    {"9H", CONT_EU}, {"T7", CONT_EU}, {"1A", CONT_EU}, {"TK", CONT_EU}, {"TF", CONT_EU}, {"3A", CONT_EU},
    {"UR", CONT_EU}, {"US", CONT_EU}, {"UT", CONT_EU}, {"UU", CONT_EU}, {"UV", CONT_EU},
    {"UW", CONT_EU}, {"UX", CONT_EU}, {"UY", CONT_EU}, {"UZ", CONT_EU},
    {"EM", CONT_EU}, {"EN", CONT_EU}, {"EO", CONT_EU},
    {"EU", CONT_EU}, {"EV", CONT_EU}, {"EW", CONT_EU},
    {"GI", CONT_EU}, {"GD", CONT_EU}, {"GJ", CONT_EU}, {"GU", CONT_EU}, {"GM", CONT_EU}, {"GW", CONT_EU},
    {"C3", CONT_EU}, {"ZB", CONT_EU},
    // ── Asia ──
    {"JA", CONT_AS}, {"JE", CONT_AS}, {"JF", CONT_AS}, {"JG", CONT_AS}, {"JH", CONT_AS},
    {"JI", CONT_AS}, {"JJ", CONT_AS}, {"JK", CONT_AS}, {"JL", CONT_AS}, {"JM", CONT_AS},
    {"JN", CONT_AS}, {"JO", CONT_AS}, {"JP", CONT_AS}, {"JQ", CONT_AS}, {"JR", CONT_AS}, {"JS", CONT_AS},
    {"7J", CONT_AS}, {"7K", CONT_AS}, {"7L", CONT_AS}, {"7M", CONT_AS}, {"7N", CONT_AS},
    {"8J", CONT_AS}, {"8N", CONT_AS}, {"JD", CONT_AS}, {"JY", CONT_AS},
    {"B", CONT_AS}, {"VR", CONT_AS}, {"XX9", CONT_AS},
    {"HL", CONT_AS}, {"DS", CONT_AS}, {"DT", CONT_AS}, {"HM", CONT_AS},
    {"6K", CONT_AS}, {"6L", CONT_AS}, {"6M", CONT_AS}, {"6N", CONT_AS},
    {"VU", CONT_AS}, {"AT", CONT_AS}, {"AU", CONT_AS}, {"AV", CONT_AS}, {"AW", CONT_AS}, {"AP", CONT_AS},
    {"9V", CONT_AS}, {"9M", CONT_AS}, {"9W", CONT_AS}, {"9K", CONT_AS}, {"9N", CONT_AS},
    {"HS", CONT_AS}, {"E2", CONT_AS},
    {"XV", CONT_AS}, {"3W", CONT_AS}, {"XU", CONT_AS}, {"XW", CONT_AS}, {"XZ", CONT_AS},
    {"4S", CONT_AS}, {"8Q", CONT_AS}, {"S2", CONT_AS}, {"A5", CONT_AS},
    {"4K", CONT_AS}, {"4L", CONT_AS}, {"EK", CONT_AS}, {"EX", CONT_AS}, {"EY", CONT_AS}, {"EZ", CONT_AS},
    {"UJ", CONT_AS}, {"UK", CONT_AS}, {"UL", CONT_AS}, {"UM", CONT_AS},
    {"UN", CONT_AS}, {"UO", CONT_AS}, {"UP", CONT_AS}, {"UQ", CONT_AS},
    {"TA", CONT_AS}, {"TC", CONT_AS}, {"YK", CONT_AS}, {"OD", CONT_AS}, {"YI", CONT_AS},
    {"EP", CONT_AS}, {"YA", CONT_AS}, {"E4", CONT_AS},
    {"4X", CONT_AS}, {"4Z", CONT_AS},
    {"HZ", CONT_AS}, {"7Z", CONT_AS}, {"8Z", CONT_AS}, {"7O", CONT_AS},
    {"A4", CONT_AS}, {"A6", CONT_AS}, {"A7", CONT_AS}, {"A9", CONT_AS}, {"5B", CONT_AS}, {"V8", CONT_AS},
    // ── Africa ──
    {"ZS", CONT_AF}, {"ZR", CONT_AF}, {"ZT", CONT_AF}, {"ZU", CONT_AF},
    {"5N", CONT_AF}, {"5R", CONT_AF}, {"5T", CONT_AF}, {"5U", CONT_AF}, {"5V", CONT_AF},
    {"5X", CONT_AF}, {"5Z", CONT_AF}, {"5A", CONT_AF}, {"5H", CONT_AF},
    {"6W", CONT_AF}, {"6V", CONT_AF}, {"6O", CONT_AF},
    {"7Q", CONT_AF}, {"7X", CONT_AF}, {"7P", CONT_AF},
    {"9G", CONT_AF}, {"9J", CONT_AF}, {"9L", CONT_AF}, {"9Q", CONT_AF}, {"9U", CONT_AF}, {"9X", CONT_AF},
    {"C5", CONT_AF}, {"C9", CONT_AF}, {"D2", CONT_AF}, {"D4", CONT_AF},
    {"EL", CONT_AF}, {"ET", CONT_AF}, {"J2", CONT_AF}, {"J5", CONT_AF}, {"E3", CONT_AF},
    {"S0", CONT_AF}, {"S7", CONT_AF}, {"S9", CONT_AF}, {"ST", CONT_AF}, {"SU", CONT_AF},
    {"T5", CONT_AF}, {"TJ", CONT_AF}, {"TL", CONT_AF}, {"TN", CONT_AF}, {"TR", CONT_AF},
    {"TT", CONT_AF}, {"TU", CONT_AF}, {"TY", CONT_AF}, {"TZ", CONT_AF},
    {"V5", CONT_AF}, {"XT", CONT_AF}, {"Z2", CONT_AF}, {"Z8", CONT_AF}, {"A2", CONT_AF},
    {"3B", CONT_AF}, {"3C", CONT_AF}, {"3V", CONT_AF}, {"3X", CONT_AF}, {"3DA", CONT_AF},
    {"CN", CONT_AF}, {"FR", CONT_AF}, {"FH", CONT_AF}, {"FT", CONT_AF},
    // ── Oceania ──
    {"VK", CONT_OC}, {"ZL", CONT_OC}, {"ZK", CONT_OC}, {"ZM", CONT_OC},
    {"KH", CONT_OC}, {"AH", CONT_OC}, {"NH", CONT_OC}, {"WH", CONT_OC},
    {"FK", CONT_OC}, {"FO", CONT_OC}, {"FW", CONT_OC},
    {"YB", CONT_OC}, {"YC", CONT_OC}, {"YD", CONT_OC}, {"YE", CONT_OC}, {"YF", CONT_OC}, {"YG", CONT_OC}, {"YH", CONT_OC},
    {"DU", CONT_OC}, {"DV", CONT_OC}, {"DW", CONT_OC}, {"DX", CONT_OC}, {"DY", CONT_OC}, {"DZ", CONT_OC},
    {"P2", CONT_OC}, {"H4", CONT_OC}, {"A3", CONT_OC}, {"3D2", CONT_OC}, {"C2", CONT_OC},
    {"T2", CONT_OC}, {"T3", CONT_OC}, {"T8", CONT_OC}, {"V6", CONT_OC}, {"V7", CONT_OC}, {"VP6", CONT_OC},
    {"5W", CONT_OC}, {"E5", CONT_OC}, {"E6", CONT_OC}, {"YJ", CONT_OC},
    // ── German callsigns (DA-DR = Germany/EU) — listed after D* DX above so the
    //    longer DX prefixes (D2/D4 AF, DS/DT AS, DU.. OC) win where they overlap ──
    {"DA", CONT_EU}, {"DB", CONT_EU}, {"DC", CONT_EU}, {"DD", CONT_EU}, {"DF", CONT_EU},
    {"DG", CONT_EU}, {"DH", CONT_EU}, {"DJ", CONT_EU}, {"DK", CONT_EU}, {"DL", CONT_EU},
    {"DM", CONT_EU}, {"DN", CONT_EU}, {"DO", CONT_EU}, {"DP", CONT_EU}, {"DQ", CONT_EU}, {"DR", CONT_EU},
    // ── Antarctica (narrow, mostly research-station calls) ──
    {"RI1AN", CONT_AN}, {"DP0", CONT_AN}, {"DP1", CONT_AN},
};
static constexpr int kNumPfxConts = (int)(sizeof(kPfxConts) / sizeof(kPfxConts[0]));

static int _firstDigit(const char* s) {
    for (int i = 0; s[i]; i++) if (s[i] >= '0' && s[i] <= '9') return s[i] - '0';
    return -1;
}

// Returns a CONT_* index for the spotter's callsign, or -1 if unknown.
static int _spotterContinent(const char* call) {
    if (!call || !call[0]) return -1;

    // Take the operating prefix: stop at the first '/' (portable) or '-' (cluster SSID)
    char c[16];
    int j = 0;
    for (int i = 0; call[i] && call[i] != '/' && call[i] != '-' && j < 15; i++)
        c[j++] = (char)toupper((unsigned char)call[i]);
    c[j] = '\0';
    if (!c[0]) return -1;

    // Russia (R*, UA-UI*): the district digit splits European vs Asiatic Russia.
    bool ru = (c[0] == 'R') || (c[0] == 'U' && c[1] >= 'A' && c[1] <= 'I');
    if (ru) {
        int d = _firstDigit(c);
        return (d == 0 || d == 8 || d == 9) ? CONT_AS : CONT_EU;
    }

    // Longest matching prefix wins (broad blocks overridden by narrower ones).
    int best = -1, bestLen = 0;
    for (int i = 0; i < kNumPfxConts; i++) {
        int pl = (int)strlen(kPfxConts[i].pfx);
        if (pl > bestLen && strncmp(c, kPfxConts[i].pfx, pl) == 0) {
            best    = kPfxConts[i].cont;
            bestLen = pl;
        }
    }
    return best;
}

static bool _spotPasses(const dxs::DXSpot& s) {
    if (s_bandMask) {
        int bi = _bandIndex((float)atof(s.freq));
        if (bi < 0 || !(s_bandMask & (1u << bi))) return false;
    }
    if (s_modeMask) {
        int mi = _modeIndex(_spotMode(s));   // reported mode, else inferred from freq
        // Still-unknown mode → keep visible rather than hide it
        if (mi >= 0 && !(s_modeMask & (1u << mi))) return false;
    }
    if (s_contMask) {
        int ci = _spotterContinent(s.spotter);   // inferred from spotter callsign prefix
        // Unknown continent → keep visible rather than hide it
        if (ci >= 0 && !(s_contMask & (1u << ci))) return false;
    }
    return true;
}

// ── show() ────────────────────────────────────────────────────────────
void ScreenDXSpots::show() {
    if (!_screen) {
        _screen = lv_obj_create(nullptr);
        lv_obj_set_size(_screen, DXS_SCREEN_W, DXS_SCREEN_H);
        lv_obj_set_style_bg_color(_screen, theme::BG, 0);
        lv_obj_set_style_pad_all(_screen, 0, 0);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

        _buildTopBar(_screen);
        _buildList(_screen);
        _buildBottomBar(_screen);
    }

    _rebuildList();
    lv_scr_load(_screen);

    // Kick off a fresh fetch (HC WebSocket is always streaming; HTTP feeds poll)
    if (config::get().dxFeedSource != DX_FEED_HOLYCLUSTER) {
        DXFeed::requestFetch();
    }
    DXS_LOG("UI", "DX Spots shown");
}

// ── onNewData() ───────────────────────────────────────────────────────
void ScreenDXSpots::onNewData() {
    if (_screen && lv_scr_act() == _screen) {
        _rebuildList();
    }
}

// ── _buildTopBar() ────────────────────────────────────────────────────
void ScreenDXSpots::_buildTopBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, TOP_H);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG_CARD, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 4, 0);
    lv_obj_set_style_pad_ver(bar, 2, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Home button
    lv_obj_t* homeBtn = lv_btn_create(bar);
    lv_group_remove_obj(homeBtn);
    lv_obj_set_height(homeBtn, TOP_H - 6);
    lv_obj_set_style_bg_color(homeBtn, theme::BG, 0);
    lv_obj_set_style_bg_color(homeBtn, theme::PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(homeBtn, theme::BORDER, 0);
    lv_obj_set_style_border_width(homeBtn, 1, 0);
    lv_obj_set_style_radius(homeBtn, 4, 0);
    lv_obj_set_style_shadow_width(homeBtn, 0, 0);
    lv_obj_set_style_pad_hor(homeBtn, 6, 0);
    lv_obj_add_event_cb(homeBtn, _onHomeClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* homeLbl = lv_label_create(homeBtn);
    lv_label_set_text(homeLbl, LV_SYMBOL_HOME);
    lv_obj_set_style_text_color(homeLbl, theme::ACCENT, 0);
    lv_obj_set_style_text_font(homeLbl, &lv_font_montserrat_10, 0);
    lv_obj_center(homeLbl);

    // Title
    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  DX Spots");
    lv_obj_set_style_text_color(title, theme::TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    // Spacer
    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_pad_all(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    // Status label (spot count / source)
    _statusLbl = lv_label_create(bar);
    lv_label_set_text(_statusLbl, "...");
    lv_obj_set_style_text_color(_statusLbl, theme::TEXT_MUTED, 0);
    lv_obj_set_style_text_font(_statusLbl, &lv_font_montserrat_10, 0);
}

// ── _buildList() ──────────────────────────────────────────────────────
void ScreenDXSpots::_buildList(lv_obj_t* parent) {
    _list = lv_obj_create(parent);
    lv_obj_set_size(_list, DXS_SCREEN_W, DXS_SCREEN_H - TOP_H - BOT_H);
    lv_obj_align(_list, LV_ALIGN_TOP_LEFT, 0, TOP_H);
    lv_obj_set_style_bg_color(_list, theme::BG, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_radius(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_AUTO);
}

// ── Column widths (must add up to ~DXS_SCREEN_W = 320) ───────────────
// Comment + Time are shown in the per-spot detail popup (tap a row); the list
// carries Freq / DX / Spotter / Mode (Mode is reported, else inferred).
// Widths sum to ~306 (row usable width = 320 - 2*4px pad = 312).
//   Freq 56 + Mode 38 + DX 84 + Spotter 78 + Ago 50 = 306
static constexpr int COL_FREQ    = 56;
static constexpr int COL_MODE    = 38;
static constexpr int COL_DXCALL  = 84;
static constexpr int COL_SPOTTER = 78;
static constexpr int COL_AGO     = 50;
static constexpr int ROW_H       = 22;

// Convert a spot's UTC time ("HHMM" or "HH:MM", feed-dependent) to a compact
// relative age ("now", "5m", "3h"). Falls back to raw HH:MM if the clock isn't
// NTP-synced. No date in the feed, so ages are taken modulo 24h.
static void _agoText(const char* hm, char* out, size_t n) {
    int d[4], nd = 0;
    for (int i = 0; hm && hm[i] && nd < 4; i++)
        if (isdigit((unsigned char)hm[i])) d[nd++] = hm[i] - '0';
    if (nd < 4) { snprintf(out, n, "-"); return; }
    int hh = d[0] * 10 + d[1];
    int mm = d[2] * 10 + d[3];
    if (hh > 23 || mm > 59) { snprintf(out, n, "-"); return; }

    time_t t = time(nullptr);
    if (t < 1000000000L) { snprintf(out, n, "%02d:%02d", hh, mm); return; }
    struct tm g; gmtime_r(&t, &g);
    int diff = (g.tm_hour * 60 + g.tm_min) - (hh * 60 + mm);
    if (diff < 0) diff += 1440;
    if      (diff < 1)  snprintf(out, n, "now");
    else if (diff < 60) snprintf(out, n, "%dm", diff);
    else                snprintf(out, n, "%dh", diff / 60);
}

// Spots currently displayed (in list order) so a row tap can open its detail.
static dxs::DXSpot s_shown[DX_MAX_SPOTS];
static int         s_shownCount  = 0;
static lv_obj_t*   s_detailPopup = nullptr;

static lv_obj_t* makeCell(lv_obj_t* row, int w, const char* text,
                           lv_color_t col, const lv_font_t* font) {
    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_width(lbl, w);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(lbl, col, 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    return lbl;
}

// ── Band colour by frequency (kHz) ───────────────────────────────────
static lv_color_t _freqColor(float kHz) {
    // HF bands
    if (kHz >= 1800  && kHz <  2000)  return lv_color_hex(0x00CED1); // 160m teal
    if (kHz >= 3500  && kHz <  4000)  return lv_color_hex(0xCD853F); // 80m  brown
    if (kHz >= 5330  && kHz <  5410)  return lv_color_hex(0x4169E1); // 60m  navy-blue
    if (kHz >= 7000  && kHz <  7300)  return dxs::theme::ACCENT;     // 40m  accent (default)
    if (kHz >= 10100 && kHz < 10150)  return lv_color_hex(0xFFD700); // 30m  yellow
    if (kHz >= 14000 && kHz < 14350)  return dxs::theme::RED;        // 20m  red
    if (kHz >= 18068 && kHz < 18168)  return lv_color_hex(0xCC66FF); // 17m  purple
    if (kHz >= 21000 && kHz < 21450)  return lv_color_hex(0x6495ED); // 15m  blue
    if (kHz >= 24890 && kHz < 24990)  return dxs::theme::ACCENT;     // 12m  accent
    if (kHz >= 28000 && kHz < 29700)  return dxs::theme::ORANGE;     // 10m  orange
    // VHF/UHF
    if (kHz >= 50000  && kHz < 54000)  return lv_color_hex(0xFF69B4); // 6m   pink
    if (kHz >= 70000  && kHz < 70500)  return lv_color_hex(0x778899); // 4m   grey-blue
    if (kHz >= 144000 && kHz < 300000) return dxs::theme::GREEN;      // 2m VHF green
    if (kHz >= 300000)                 return lv_color_hex(0x87CEEB); // UHF  light blue
    return dxs::theme::TEXT;  // unknown band
}

static void makeHeaderRow(lv_obj_t* list) {
    using namespace dxs::theme;
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_obj_set_style_bg_color(row, BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    makeCell(row, COL_FREQ,    "Freq",    ACCENT, &lv_font_montserrat_10);
    makeCell(row, COL_MODE,    "Mode",    ACCENT, &lv_font_montserrat_10);
    makeCell(row, COL_DXCALL,  "DX",      ACCENT, &lv_font_montserrat_10);
    makeCell(row, COL_SPOTTER, "Spotter", ACCENT, &lv_font_montserrat_10);
    makeCell(row, COL_AGO,     "Ago",     ACCENT, &lv_font_montserrat_10);
}

// ── Per-spot detail popup (opened by tapping a row) ───────────────────
static void _closeSpotDetail() {
    if (s_detailPopup) {
        lv_obj_del_async(s_detailPopup);   // deleted from a child's callback → async
        s_detailPopup = nullptr;
    }
}

static void _onDetailClose(lv_event_t*) { _closeSpotDetail(); }

static void _showSpotDetail(const dxs::DXSpot& s) {
    using namespace theme;
    if (s_detailPopup) return;

    float kHz = (float)atof(s.freq);

    lv_obj_t* bg = lv_obj_create(lv_scr_act());
    s_detailPopup = bg;
    lv_obj_set_size(bg, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(bg);
    lv_obj_set_size(card, 288, 200);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, BG, 0);
    lv_obj_set_style_border_color(card, BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // DX call — headline
    lv_obj_t* call = lv_label_create(card);
    lv_label_set_text(call, s.dxCall[0] ? s.dxCall : "?");
    lv_obj_set_style_text_color(call, TEXT, 0);
    lv_obj_set_style_text_font(call, &lv_font_montserrat_14, 0);

    // Frequency + band
    int bi = _bandIndex(kHz);
    char line[64];
    if (kHz >= 1000.f)
        snprintf(line, sizeof(line), "%.3f MHz  (%s)",
                 (double)(kHz / 1000.f), bi >= 0 ? kBands[bi].label : "?");
    else
        snprintf(line, sizeof(line), "%s kHz", s.freq);
    lv_obj_t* freqLbl = lv_label_create(card);
    lv_label_set_text(freqLbl, line);
    lv_obj_set_style_text_color(freqLbl, _freqColor(kHz), 0);
    lv_obj_set_style_text_font(freqLbl, &lv_font_montserrat_14, 0);

    auto mkField = [&](const char* label, const char* value) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s  %s", label, (value && value[0]) ? value : "-");
        lv_obj_t* l = lv_label_create(card);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_text(l, buf);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(l, TEXT_MUTED, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    };
    char modeBuf[24] = {0};
    if (s.mode[0]) {
        snprintf(modeBuf, sizeof(modeBuf), "%s", s.mode);
    } else {
        const char* inf = _inferMode(kHz);
        if (inf[0]) snprintf(modeBuf, sizeof(modeBuf), "%s (inferred)", inf);
    }
    mkField("Spotter:", s.spotter);
    mkField("Mode:",    modeBuf[0] ? modeBuf : nullptr);
    mkField("Time:",    s.utcTime[0] ? s.utcTime : nullptr);
    mkField("Comment:", s.comment);

    // Close button (bottom of card)
    lv_obj_t* closeBtn = lv_btn_create(card);
    lv_group_remove_obj(closeBtn);
    lv_obj_set_width(closeBtn, lv_pct(100));
    lv_obj_set_height(closeBtn, 28);
    lv_obj_set_style_bg_color(closeBtn, PRIMARY, 0);
    lv_obj_set_style_bg_color(closeBtn, ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_radius(closeBtn, 4, 0);
    lv_obj_set_style_border_width(closeBtn, 0, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_add_event_cb(closeBtn, _onDetailClose, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cl = lv_label_create(closeBtn);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE "  Close");
    lv_obj_set_style_text_color(cl, TEXT, 0);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_12, 0);
    lv_obj_center(cl);
}

static void _onSpotRowClick(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < s_shownCount) _showSpotDetail(s_shown[idx]);
}

static void makeSpotRow(lv_obj_t* list, const dxs::DXSpot& spot, int rowIdx) {
    using namespace dxs::theme;
    lv_obj_t* row = lv_obj_create(list);
    lv_obj_set_size(row, lv_pct(100), ROW_H);
    lv_color_t bg = (rowIdx & 1) ? BG_CARD : BG;
    lv_obj_set_style_bg_color(row, bg, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, PRIMARY, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_pad_ver(row, 2, 0);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Tap → open detail popup for this spot
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, _onSpotRowClick, LV_EVENT_CLICKED, (void*)(intptr_t)rowIdx);

    // Frequency: convert kHz string to MHz for display, colour by band
    char freqBuf[14];
    float kHz = (float)atof(spot.freq);
    if (kHz >= 1000.f)
        snprintf(freqBuf, sizeof(freqBuf), "%.3f", (double)(kHz / 1000.f));
    else
        snprintf(freqBuf, sizeof(freqBuf), "%s", spot.freq);

    // Mode: reported by the feed, or inferred from the frequency (reported shown
    // in accent, inferred-only shown muted). Helps scanning/filtering.
    const char* mode = _spotMode(spot);

    char agoBuf[10];
    _agoText(spot.utcTime, agoBuf, sizeof(agoBuf));

    makeCell(row, COL_FREQ, freqBuf, _freqColor(kHz), &lv_font_montserrat_10);
    makeCell(row, COL_MODE, mode[0] ? mode : "-",
             spot.mode[0] ? ACCENT : TEXT_MUTED, &lv_font_montserrat_10);
    makeCell(row, COL_DXCALL,  spot.dxCall,  TEXT,       &lv_font_montserrat_10);
    makeCell(row, COL_SPOTTER, spot.spotter, TEXT_MUTED, &lv_font_montserrat_10);
    makeCell(row, COL_AGO,     agoBuf,       TEXT_MUTED, &lv_font_montserrat_10);
}

// ── _rebuildList() ────────────────────────────────────────────────────
void ScreenDXSpots::_rebuildList() {
    if (!_list) return;
    lv_obj_clean(_list);

    if (!WiFiMgr::instance().isConnected()) {
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg, LV_SYMBOL_WIFI " No WiFi connection.\nConfigure SSID in Settings.");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "No WiFi");
        return;
    }

    // Holy Cluster uses WebSocket; all other sources use HTTP polling
    bool useHC = (config::get().dxFeedSource == DX_FEED_HOLYCLUSTER);

    // static: these arrays are large (~3 KB each) and _rebuildList() only runs
    // from the LVGL tick context, so static storage is safe here.
    static DXSpot spots[DX_MAX_SPOTS];
    int n = 0;

    if (useHC) {
        static HCSpot hc[HC_MAX_SPOTS];
        int hn = HolyClusterFeed::getSpots(hc, HC_MAX_SPOTS);
        n = hn < DX_MAX_SPOTS ? hn : DX_MAX_SPOTS;
        for (int i = 0; i < n; i++) {
            memset(&spots[i], 0, sizeof(DXSpot));
            strncpy(spots[i].dxCall,  hc[i].dxCall,  sizeof(spots[i].dxCall)  - 1);
            strncpy(spots[i].spotter, hc[i].spotter, sizeof(spots[i].spotter) - 1);
            strncpy(spots[i].mode,    hc[i].mode,    sizeof(spots[i].mode)    - 1);
            strncpy(spots[i].comment, hc[i].comment, sizeof(spots[i].comment) - 1);
            strncpy(spots[i].utcTime, hc[i].utcTime, sizeof(spots[i].utcTime) - 1);
            // HC freq is already kHz; store as string for makeSpotRow()
            snprintf(spots[i].freq, sizeof(spots[i].freq), "%.1f", (double)hc[i].freqKHz);
        }
    } else {
        n = DXFeed::getSpots(spots, DX_MAX_SPOTS);
    }

    if (n == 0) {
        int status = useHC ? HolyClusterFeed::lastStatus() : DXFeed::lastStatus();
        const char* msg_str;
        if (useHC) {
            msg_str = (status < 0)  ? "Connecting to Holy Cluster..."
                    : (status == 0) ? "Connecting..."
                                    : "No spots received yet";
        } else {
            msg_str = (status == 0) ? "Fetching spots..." : "No spots available";
        }
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg, msg_str);
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
        if (_statusLbl) lv_label_set_text(_statusLbl, "0 spots");
        return;
    }

    makeHeaderRow(_list);
    int shown = 0;
    for (int i = 0; i < n; i++) {
        if (!_spotPasses(spots[i])) continue;
        s_shown[shown] = spots[i];           // keep a copy so a row tap can show details
        makeSpotRow(_list, spots[i], shown);
        shown++;
    }
    s_shownCount = shown;

    if (shown == 0) {
        lv_obj_t* msg = lv_label_create(_list);
        lv_label_set_text(msg, "No spots match the active filter");
        lv_obj_set_style_text_color(msg, theme::TEXT_MUTED, 0);
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
        lv_obj_align(msg, LV_ALIGN_TOP_MID, 0, 20);
    }

    if (_statusLbl) {
        char buf[28];
        bool filtered = (s_bandMask || s_modeMask || s_contMask);
        if (filtered)
            snprintf(buf, sizeof(buf), "%d/%d%s", shown, n, useHC ? " HC" : "");
        else
            snprintf(buf, sizeof(buf), "%d spots%s", n, useHC ? " (HC)" : "");
        lv_label_set_text(_statusLbl, buf);
    }
}

// ── Filter popup ──────────────────────────────────────────────────────
static lv_obj_t* _mkWrap(lv_obj_t* par) {
    lv_obj_t* w = lv_obj_create(par);
    lv_obj_set_width(w, lv_pct(100));
    lv_obj_set_height(w, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(w, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w, 0, 0);
    lv_obj_set_style_pad_all(w, 0, 0);
    lv_obj_set_style_pad_row(w, 4, 0);
    lv_obj_set_style_pad_column(w, 4, 0);
    lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(w, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(w, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return w;
}

static void _onFilterToggle(lv_event_t* e) {
    int code      = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* btn = lv_event_get_target(e);
    bool on       = lv_obj_has_state(btn, LV_STATE_CHECKED);
    uint32_t bit  = 1u << (code & 0xFF);
    uint32_t* mask = (code & 0x200) ? &s_modeMask : &s_bandMask;
    if (on) *mask |= bit;
    else    *mask &= ~bit;
}

static lv_obj_t* _mkChip(lv_obj_t* par, const char* txt, bool checked, int code,
                         lv_event_cb_t cb) {
    lv_obj_t* b = lv_btn_create(par);
    lv_group_remove_obj(b);
    lv_obj_set_size(b, 44, 26);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
    if (checked) lv_obj_add_state(b, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, theme::BG_CARD, 0);
    lv_obj_set_style_bg_color(b, theme::ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, theme::BORDER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_VALUE_CHANGED, (void*)(intptr_t)code);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, theme::TEXT, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_center(l);
    return b;
}

static void _closeFilter() {
    if (s_filterPopup) {
        lv_obj_del_async(s_filterPopup);   // deleted from a child's callback → async
        s_filterPopup = nullptr;
    }
    for (int i = 0; i < kNumBands; i++) s_bandBtns[i] = nullptr;
    for (int i = 0; i < kNumModes; i++) s_modeBtns[i] = nullptr;
}

static void _onFilterReset(lv_event_t*) {
    s_bandMask = 0;
    s_modeMask = 0;
    for (int i = 0; i < kNumBands; i++)
        if (s_bandBtns[i]) lv_obj_clear_state(s_bandBtns[i], LV_STATE_CHECKED);
    for (int i = 0; i < kNumModes; i++)
        if (s_modeBtns[i]) lv_obj_clear_state(s_modeBtns[i], LV_STATE_CHECKED);
}

static void _onFilterDone(lv_event_t*) {
    _closeFilter();
    ScreenDXSpots::onNewData();   // rebuild the (active) list with the new filter
}

static void _onFilterClick(lv_event_t*) {
    if (s_filterPopup) return;
    using namespace theme;

    lv_obj_t* bg = lv_obj_create(lv_scr_act());
    s_filterPopup = bg;
    lv_obj_set_size(bg, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(bg);
    lv_obj_set_size(card, 300, 228);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, BG, 0);
    lv_obj_set_style_border_color(card, BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    auto mkHeading = [](lv_obj_t* par, const char* t) {
        lv_obj_t* l = lv_label_create(par);
        lv_label_set_text(l, t);
        lv_obj_set_style_text_color(l, theme::ACCENT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    };

    mkHeading(card, "Band");
    lv_obj_t* bandWrap = _mkWrap(card);
    for (int i = 0; i < kNumBands; i++)
        s_bandBtns[i] = _mkChip(bandWrap, kBands[i].label,
                                s_bandMask & (1u << i), 0x100 | i, _onFilterToggle);

    mkHeading(card, "Mode");
    lv_obj_t* modeWrap = _mkWrap(card);
    for (int i = 0; i < kNumModes; i++)
        s_modeBtns[i] = _mkChip(modeWrap, kModes[i],
                                s_modeMask & (1u << i), 0x200 | i, _onFilterToggle);

    // Action row: Reset (show all) | Done (apply + close)
    lv_obj_t* actions = _mkWrap(card);
    lv_obj_set_style_pad_top(actions, 4, 0);
    auto mkAction = [](lv_obj_t* par, const char* txt, lv_event_cb_t cb, lv_color_t c) {
        lv_obj_t* b = lv_btn_create(par);
        lv_group_remove_obj(b);
        lv_obj_set_height(b, 28);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_color(b, c, 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, theme::TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
    };
    mkAction(actions, "Reset", _onFilterReset, BG_CARD);
    mkAction(actions, LV_SYMBOL_OK " Done", _onFilterDone, PRIMARY);
}

// ── "DX de" popup — filter by spotter (DE) continent ──────────────────
static lv_obj_t* s_contPopup           = nullptr;
static lv_obj_t* s_contBtns[CONT_COUNT] = {};

static void _onContToggle(lv_event_t* e) {
    int idx       = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* btn = lv_event_get_target(e);
    bool on       = lv_obj_has_state(btn, LV_STATE_CHECKED);
    if (idx < 0 || idx >= CONT_COUNT) return;
    if (on) s_contMask |=  (1u << idx);
    else    s_contMask &= ~(1u << idx);
}

static void _closeDxDe() {
    if (s_contPopup) {
        lv_obj_del_async(s_contPopup);   // deleted from a child's callback → async
        s_contPopup = nullptr;
    }
    for (int i = 0; i < CONT_COUNT; i++) s_contBtns[i] = nullptr;
}

static void _onDxDeReset(lv_event_t*) {
    s_contMask = 0;
    for (int i = 0; i < CONT_COUNT; i++)
        if (s_contBtns[i]) lv_obj_clear_state(s_contBtns[i], LV_STATE_CHECKED);
}

static void _onDxDeDone(lv_event_t*) {
    _closeDxDe();
    ScreenDXSpots::onNewData();   // rebuild the (active) list with the new filter
}

static void _onDxDeClick(lv_event_t*) {
    if (s_contPopup) return;
    using namespace theme;

    lv_obj_t* bg = lv_obj_create(lv_scr_act());
    s_contPopup = bg;
    lv_obj_set_size(bg, DXS_SCREEN_W, DXS_SCREEN_H);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* card = lv_obj_create(bg);
    lv_obj_set_size(card, 300, 170);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, BG, 0);
    lv_obj_set_style_border_color(card, BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t* heading = lv_label_create(card);
    lv_label_set_text(heading, "DX de  (spotter continent)");
    lv_obj_set_style_text_color(heading, ACCENT, 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_12, 0);

    lv_obj_t* wrap = _mkWrap(card);
    for (int i = 0; i < CONT_COUNT; i++)
        s_contBtns[i] = _mkChip(wrap, kConts[i], s_contMask & (1u << i), i, _onContToggle);

    lv_obj_t* actions = _mkWrap(card);
    lv_obj_set_style_pad_top(actions, 4, 0);
    auto mkAction = [](lv_obj_t* par, const char* txt, lv_event_cb_t cb, lv_color_t c) {
        lv_obj_t* b = lv_btn_create(par);
        lv_group_remove_obj(b);
        lv_obj_set_height(b, 28);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_color(b, c, 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, theme::TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
    };
    mkAction(actions, "Reset", _onDxDeReset, BG_CARD);
    mkAction(actions, LV_SYMBOL_OK " Done", _onDxDeDone, PRIMARY);
}

// ── _buildBottomBar() — Refresh | Filter | DX de ──────────────────────
void ScreenDXSpots::_buildBottomBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, DXS_SCREEN_W, BOT_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto mkHalf = [](lv_obj_t* par, const char* txt, lv_event_cb_t cb, bool divider) {
        lv_obj_t* b = lv_btn_create(par);
        lv_group_remove_obj(b);
        lv_obj_set_height(b, BOT_H);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_bg_color(b, theme::PRIMARY, 0);
        lv_obj_set_style_bg_color(b, theme::ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        if (divider) {
            // 1px separator between the two halves
            lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, 0);
            lv_obj_set_style_border_color(b, theme::BG, 0);
            lv_obj_set_style_border_width(b, 1, 0);
        }
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, theme::TEXT, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_center(l);
    };

    mkHalf(bar, LV_SYMBOL_REFRESH " Refresh", _onRefreshClick, false);
    mkHalf(bar, LV_SYMBOL_LIST    " Filter",  _onFilterClick,  true);
    mkHalf(bar, LV_SYMBOL_GPS     " DX de",   _onDxDeClick,    true);
}

// ── Event handlers ────────────────────────────────────────────────────
void ScreenDXSpots::_onHomeClick(lv_event_t*) { ScreenLauncher::show(); }

void ScreenDXSpots::_onRefreshClick(lv_event_t*) {
    if (_statusLbl) lv_label_set_text(_statusLbl, "...");
    if (config::get().dxFeedSource == DX_FEED_HOLYCLUSTER) {
        HolyClusterFeed::requestFetch();
    } else {
        DXFeed::requestFetch();
    }
}

}}  // namespace dxs::ui
