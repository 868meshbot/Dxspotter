// DXSpotter — Log.h
// Copyright 2026 DXSpotter Contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thin wrapper around Serial.printf for structured logging.

#pragma once
#include <Arduino.h>

#define DXS_LOG(tag, fmt, ...) \
    Serial.printf("[%-10s] " fmt "\r\n", tag, ##__VA_ARGS__)
