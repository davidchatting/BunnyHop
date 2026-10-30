// SPDX-License-Identifier: GPL-2.0-or-later
#include "rabbit.h"
#include <map>
#include "config.h"
#include "eventlog.h"
#include "xmpp_server.h"

namespace rabbit {

static State st;
static EventSink sink;
static uint32_t programId = 1;
static uint32_t chorId = 1;
static std::map<uint32_t, packets::Bytes> chors;  // recent generated chor files
static const size_t MAX_CHORS = 6;
static SemaphoreHandle_t mtx = xSemaphoreCreateMutex();

State &state() { return st; }

void touch(const String &ip) {
    st.lastSeen = millis();
    if (ip.length()) st.ip = ip;
}

void setPhase(const char *phase) {
    if (st.phase == phase) return;
    st.phase = phase;
    LOGI("rabbit", "phase -> %s", phase);
    emit("phase", [&](JsonObject o) { o["phase"] = phase; });
}

void setEventSink(EventSink s) { sink = s; }

void emit(const char *type, std::function<void(JsonObject)> fill) {
    if (!sink) return;
    JsonDocument doc;
    doc["type"] = type;
    doc["t"] = millis();
    if (fill) fill(doc.as<JsonObject>());
    sink(doc);
}

// ---------------- inputs ----------------

void onButton(int clicks, int event) {
    st.lastClick = clicks;
    st.lastClickAt = millis();
    LOGI("event", "button clicks=%d event=%d", clicks, event);
    emit("button", [&](JsonObject o) {
        o["clicks"] = clicks;
        if (event >= 0) o["event"] = event;
    });
}

void onEars(int left, int right) {
    st.earLeft = left;
    st.earRight = right;
    LOGI("event", "ears moved by hand left=%d right=%d", left, right);
    emit("ears", [&](JsonObject o) {
        o["left"] = left;
        o["right"] = right;
    });
}

void onRfid(const String &tag) {
    st.lastTag = tag;
    st.lastTagAt = millis();
    LOGI("event", "rfid %s", tag.c_str());
    emit("rfid", [&](JsonObject o) { o["tag"] = tag; });
}

void onRecording(const String &path, size_t bytes) {
    st.lastRecording = path;
    LOGI("event", "recording %s (%u bytes)", path.c_str(), (unsigned)bytes);
    emit("recording", [&](JsonObject o) {
        o["url"] = path;
        o["bytes"] = bytes;
    });
}

void onXmppConnected(bool up) {
    st.xmpp = up;
    if (!up) setPhase("idle");
    emit("connection", [&](JsonObject o) { o["connected"] = up; });
}

// ---------------- outputs ----------------

static bool send(const std::vector<packets::Block> &blocks, String *err, const char *what) {
    if (!xmpp::connected()) {
        if (err) *err = "rabbit not connected";
        LOGI("cmd", "%s dropped: rabbit not connected", what);
        return false;
    }
    packets::Bytes t = packets::trame(blocks);
    bool ok = xmpp::sendPacket(t);
    LOGI("cmd", "%s -> %s (%s)", what, ok ? "sent" : "FAILED", packets::hex(t).substring(0, 96).c_str());
    if (!ok && err) *err = "send failed";
    return ok;
}

bool setEars(int left, int right, String *err) {
    std::vector<std::pair<uint8_t, uint8_t>> s;
    if (left >= 0) {
        left = constrain(left, 0, EAR_MAX);
        s.push_back({packets::SVC_RABBIT_LEFT_EAR, (uint8_t)left});
        st.cmdEarLeft = left;
    }
    if (right >= 0) {
        right = constrain(right, 0, EAR_MAX);
        s.push_back({packets::SVC_RABBIT_RIGHT_EAR, (uint8_t)right});
        st.cmdEarRight = right;
    }
    if (s.empty()) {
        if (err) *err = "no ear given";
        return false;
    }
    char what[48];
    snprintf(what, sizeof(what), "ears L=%d R=%d", left, right);
    return send({packets::ambient(s)}, err, what);
}

static String programHeader() {
    return "ID " + String(programId++) + "\n";
}

static uint32_t storeChor(const packets::Bytes &file) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    uint32_t id = chorId++;
    chors[id] = file;
    while (chors.size() > MAX_CHORS) chors.erase(chors.begin());
    xSemaphoreGive(mtx);
    return id;
}

bool chorFile(uint32_t id, packets::Bytes &out) {
    xSemaphoreTake(mtx, portMAX_DELAY);
    auto it = chors.find(id);
    bool ok = it != chors.end();
    if (ok) out = it->second;
    xSemaphoreGive(mtx);
    return ok;
}

// The rabbit needs ~0.9 s to download and start a choreography, and a new
// one arriving mid-download cancels the old without starting itself. So
// while one is in flight, only the latest request is kept and sent when the
// rabbit has fetched the current one (chorDelivered) - continuous senders
// like p5 sketches then get the fastest rate the rabbit can manage.
static bool chorInFlight = false;
static uint32_t chorSentAt = 0;
static bool chorPending = false;
static packets::Bytes pendingChor;
static volatile bool chorDone = false;
static uint32_t chorDeliveredAt = 0;

static bool sendChorNow(const packets::Bytes &file, String *err) {
    uint32_t id = storeChor(file);
    String prog = programHeader() + "CH http://" AP_IP_STR ":" + String(CHOR_PORT) + "/" + String(id) + ".chor\n";
    bool ok = send({packets::message(prog)}, err, "chor");
    chorInFlight = ok;
    chorSentAt = millis();
    return ok;
}

bool chor(const packets::Bytes &file, String *err) {
    if (chorInFlight && millis() - chorSentAt < CHOR_FLIGHT_TIMEOUT_MS) {
        pendingChor = file;
        chorPending = true;
        return true;  // queued; superseded by any newer request
    }
    return sendChorNow(file, err);
}

void chorDelivered() { chorDone = true; }

void loop() {
    if (chorDone) {
        chorDone = false;
        chorInFlight = false;
        chorDeliveredAt = millis();
    }
    if (chorInFlight && millis() - chorSentAt >= CHOR_FLIGHT_TIMEOUT_MS) chorInFlight = false;
    // give the rabbit a moment to start the delivered chor before replacing it
    if (chorPending && !chorInFlight && millis() - chorDeliveredAt >= CHOR_SETTLE_MS) {
        chorPending = false;
        sendChorNow(pendingChor, nullptr);
    }
}

bool setLeds(const Rgb colors[5], String *err) {
    // LEDs can only be set from a choreography, and the bootcode's idle
    // display turns them off as soon as the choreography ends. So this sends
    // one that sets all five LEDs (a chor start blanks them all) and then
    // holds for LED_HOLD_S. Later commands interrupt it (sent as "urgent").
    packets::Chor c(255);  // 2.55 s frames
    for (int i = 0; i < 5; i++) {
        st.leds[i] = colors[i];
        c.led(0, i, colors[i].r, colors[i].g, colors[i].b);
    }
    for (uint32_t held = 0; held < LED_HOLD_S; held += 650)  // 255 frames x 2.55 s
        c.led(255, 4, colors[4].r, colors[4].g, colors[4].b);
    return chor(c.build(), err);
}

bool setLed(int id, Rgb c, String *err) {
    Rgb all[5];
    for (int i = 0; i < 5; i++) all[i] = (id < 0 || id == i) ? c : st.leds[i];
    return setLeds(all, err);
}

bool playUrl(const String &url, bool wait, String *err) {
    String prog = programHeader() + "MU " + url + "\n" + (wait ? "MW\n" : "");
    return send({packets::message(prog)}, err, "play");
}

bool stream(const String &url, String *err) {
    String prog = programHeader() + "ST " + url + "\nMW\n";
    return send({packets::message(prog)}, err, "stream");
}

bool program(const String &lines, String *err) {
    String p = lines;
    p.replace("\r", "");
    if (!p.endsWith("\n")) p += "\n";  // sent exactly as given (no ID line added)
    return send({packets::message(p)}, err, "program");
}

bool sleep(bool asleep, String *err) {
    bool ok = send({packets::sleep(asleep)}, err, asleep ? "sleep" : "wake");
    if (ok) st.asleep = asleep;
    return ok;
}

bool ambient(const std::vector<std::pair<uint8_t, uint8_t>> &services, String *err) {
    return send({packets::ambient(services)}, err, "ambient");
}

bool rawTrame(const packets::Bytes &t, String *err) {
    if (!xmpp::connected()) {
        if (err) *err = "rabbit not connected";
        return false;
    }
    bool ok = xmpp::sendPacket(t);
    LOGI("cmd", "raw trame %s -> %s", packets::hex(t).substring(0, 96).c_str(), ok ? "sent" : "FAILED");
    return ok;
}

bool reboot(String *err) { return send({packets::Block{0x09, {}}}, err, "reboot"); }

void stateJson(JsonObject o) {
    o["phase"] = st.phase;
    o["connected"] = st.xmpp;
    o["resource"] = st.resource;
    o["sn"] = st.sn;
    o["ip"] = st.ip;
    o["bootcodeRev"] = st.bootcodeRev;
    o["lastSeenMsAgo"] = st.lastSeen ? (int32_t)(millis() - st.lastSeen) : -1;
    o["asleep"] = st.asleep;
    JsonObject ears = o["ears"].to<JsonObject>();
    ears["left"] = st.earLeft;
    ears["right"] = st.earRight;
    ears["cmdLeft"] = st.cmdEarLeft;
    ears["cmdRight"] = st.cmdEarRight;
    JsonArray leds = o["leds"].to<JsonArray>();
    for (auto &l : st.leds) {
        char h[8];
        snprintf(h, sizeof(h), "#%02x%02x%02x", l.r, l.g, l.b);
        leds.add(h);
    }
    o["lastTag"] = st.lastTag;
    o["lastClick"] = st.lastClick;
    o["lastRecording"] = st.lastRecording;
}

}  // namespace rabbit
