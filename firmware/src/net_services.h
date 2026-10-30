#pragma once
#include <Arduino.h>

// Small UDP services the rabbit needs on a network without internet:
//   DNS  - every name resolves to the bridge (the bootcode resolves
//          NTP/Violet hostnames; phones get a captive-portal popup)
//   NTP  - answers with the bridge's clock (set from a browser client)
namespace net_services {

void begin();
void loop();
void setTime(uint64_t epochMs);  // from a client, so the rabbit gets real time
bool timeKnown();

}  // namespace net_services
