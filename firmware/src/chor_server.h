#pragma once
#include <Arduino.h>

// Serves the files the rabbit downloads before running a program -
// choreographies (/N.chor) and audio (/audio/...) - on a dedicated port with
// explicit connection timing: send, wait until everything is acknowledged,
// linger, then close. Served through AsyncWebServer (which closes as soon as
// the data is acked) ~60% of choreographies were silently dropped by the
// rabbit; with a 300 ms linger 24/24 ran. See docs/PROTOCOL.md.
namespace chor_server {

void begin(uint16_t port);
void loop();
extern uint32_t lingerMs;

}  // namespace chor_server
