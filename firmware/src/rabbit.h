// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include "packets.h"

// The rabbit as seen by the bridge: observed state, events it raises and
// commands we can send. Transport-independent - the XMPP server and HTTP
// handlers feed it, the WebSocket/REST API drives it.
namespace rabbit {

struct Rgb {
    uint8_t r, g, b;
};

struct State {
    // connection
    String phase = "idle";  // idle | bootcode | locate | xmpp-auth | boot | ready
    bool xmpp = false;
    String resource;
    String sn;
    String ip;
    String bootcodeRev;
    uint32_t lastSeen = 0;
    uint32_t bootcodeServedAt = 0;
    // last values reported by the rabbit
    int earLeft = -1, earRight = -1;
    String lastTag;
    uint32_t lastTagAt = 0;
    int lastClick = 0;
    uint32_t lastClickAt = 0;
    String lastRecording;
    // last values commanded by us (the rabbit can't report these)
    int cmdEarLeft = -1, cmdEarRight = -1;
    Rgb leds[5];
    bool asleep = false;
};

State &state();
void touch(const String &ip = "");
void setPhase(const char *phase);

using EventSink = std::function<void(JsonDocument &)>;
void setEventSink(EventSink s);
// Builds {"type":type, "t":millis, ...fill...} and broadcasts it
void emit(const char *type, std::function<void(JsonObject)> fill = nullptr);

// --- inputs (called by transports) ---
void onButton(int clicks, int event);
void onEars(int left, int right);
void onRfid(const String &tag);
void onRecording(const String &path, size_t bytes);
void onXmppConnected(bool up);

// --- outputs ---
// All return false (with reason in err) if the rabbit isn't connected.
bool setEars(int left, int right, String *err = nullptr);
// ids 0 bottom, 1 left, 2 middle, 3 right, 4 nose; -1 = all
bool setLed(int id, Rgb c, String *err = nullptr);
bool setLeds(const Rgb colors[5], String *err = nullptr);
bool playUrl(const String &url, bool wait = true, String *err = nullptr);
bool stream(const String &url, String *err = nullptr);
bool chor(const packets::Bytes &chorFile, String *err = nullptr);
bool program(const String &lines, String *err = nullptr);  // raw "KEY value" program lines
bool sleep(bool asleep, String *err = nullptr);
bool ambient(const std::vector<std::pair<uint8_t, uint8_t>> &services, String *err = nullptr);
bool rawTrame(const packets::Bytes &trame, String *err = nullptr);
bool reboot(String *err = nullptr);

// Chor files we generated, served to the rabbit at /vl/chor/<id>.chor
bool chorFile(uint32_t id, packets::Bytes &out);

void stateJson(JsonObject o);
void loop();
void chorDelivered();  // chor_server: the rabbit finished fetching a chor

}  // namespace rabbit
