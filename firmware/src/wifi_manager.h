// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Rabbit discovery, first-time provisioning and the bridge's own AP.
//
// ESP32 constraint: one radio, so STA and soft-AP must share a channel.
// We run WIFI_AP_STA throughout; while the STA side is joined to the
// rabbit's setup network the AP follows that channel, then returns to
// AP_CHANNEL once provisioning is done.
namespace wifimgr {

void begin();
void loop();
String apSsid();
bool paired();
// Scans for a rabbit in setup mode. Returns found SSID or "".
String scanForRabbit(String *bssidOut = nullptr, int *channelOut = nullptr);
// Joins the rabbit's setup network and dumps its config page to the log.
bool probe();
// Full provisioning: join setup network, read info, push our AP settings.
bool pair();
void requestPair();  // run pair() from the loop (safe to call from handlers)
void forget();
void kickRabbit(uint32_t offMs);  // drop the AP so a rabbit with a stale session reboots
void statusJson(JsonObject o);

// Home Wi-Fi ("public side"). Empty ssid = forget.
void setLan(const String &ssid, const String &pass);
void requestLanScan();  // result arrives as a "lanScan" event
bool lanConnected();
void pauseLan(uint32_t ms);  // debug: drop home Wi-Fi for a while (simulated outage)
// True if a request arrived over the rabbit's network (vs home Wi-Fi)
bool fromRabbitNetwork(IPAddress localIp);

}  // namespace wifimgr
