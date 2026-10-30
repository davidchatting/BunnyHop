// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ESPAsyncWebServer.h>

// HTTP endpoints the rabbit itself calls (Violet platform URLs):
//   /vl/bc.jsp      bootcode download (original Violet bootcode, unmodified)
//   /vl/locate.jsp  tells the bootcode where its ping/broadcast/XMPP servers are
//   /vl/rfid.jsp    RFID tag read (t=<tag id>)
//   /vl/record.jsp  audio recorded by the rabbit's microphone (POST body)
namespace rabbit_http {

void begin(AsyncWebServer &server);
// True if the request looks like it came from the rabbit (Violet paths)
bool isRabbitPath(const String &url);

}  // namespace rabbit_http
