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
void statusJson(JsonObject o);

}  // namespace wifimgr
