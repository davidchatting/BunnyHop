// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

// Client-facing API: WebSocket /ws (commands + live events) and REST /api/*.
// One JSON command format is shared by WebSocket, REST and the serial
// console - see docs/API.md.
namespace api {

void begin(AsyncWebServer &server);
void loop();
// Executes one command object; fills reply (ok/error/data).
void execute(JsonObjectConst cmd, JsonObject reply);

}  // namespace api
