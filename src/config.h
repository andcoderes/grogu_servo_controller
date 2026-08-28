#pragma once
#include <Arduino.h>

#define RECEIVER_LINK_TIMEOUT_MS  5000   // no ESP-NOW data from receiver = disconnected
#define HEARTBEAT_INTERVAL_MS     1000

// Fixed WiFi channel for the ESP-NOW link -- must match receiver's copy exactly.
#define ESPNOW_CHANNEL 1

// Generated from .env by scripts/load_secrets.py.
#include "secrets.h"  // PMK_KEY, LMK_KEY, RECEIVER_BOARD_MAC
