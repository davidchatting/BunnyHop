// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Arduino.h>

// Persistent settings (NVS via Preferences)
namespace store {

void begin();
String get(const char *key, const String &def = "");
void set(const char *key, const String &value);
void remove(const char *key);
void clearAll();

}  // namespace store

// Keys
#define K_RABBIT_SN "sn"          // rabbit MAC as reported by the bootcode (12 hex chars)
#define K_SETUP_BSSID "setupbss"  // BSSID of the rabbit's setup-mode AP
#define K_SETUP_SSID "setupssid"  // e.g. "Nabaztag1D"
#define K_AP_SSID "apssid"        // our AP SSID, derived from the rabbit
#define K_XMPP_PW "xmpppw"        // XMPP password the rabbit registered with us
#define K_SETUP_INFO "setupinfo"  // identifying info scraped from the setup page
#define K_LAN_SSID "lanssid"      // home Wi-Fi network (public side)
#define K_LAN_PASS "lanpass"
