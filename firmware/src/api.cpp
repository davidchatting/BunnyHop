// SPDX-License-Identifier: GPL-2.0-or-later
#include "api.h"
#include <map>
#include <LittleFS.h>
#include "config.h"
#include "eventlog.h"
#include "net_services.h"
#include "chor_server.h"
#include "rabbit.h"
#include "wifi_manager.h"
#include "xmpp_server.h"

namespace api {

static AsyncWebSocket ws("/ws");
static uint32_t lastStatePush = 0;
static File upload;

static bool parseColor(JsonVariantConst v, rabbit::Rgb &c) {
    if (v.is<const char *>()) {
        String s = v.as<const char *>();
        if (s.startsWith("#")) s = s.substring(1);
        if (s.length() != 6) return false;
        uint32_t x = strtoul(s.c_str(), nullptr, 16);
        c = {(uint8_t)(x >> 16), (uint8_t)(x >> 8), (uint8_t)x};
        return true;
    }
    if (v.is<JsonArrayConst>() && v.size() == 3) {
        c = {v[0].as<uint8_t>(), v[1].as<uint8_t>(), v[2].as<uint8_t>()};
        return true;
    }
    return false;
}

// "/audio/x.mp3" -> "http://192.168.4.1:8080/audio/x.mp3" (the rabbit
// resource server, see chor_server.h); other paths go to the web server.
static String absUrl(const String &u) {
    if (u.startsWith("http://")) return u;
    String p = (u.startsWith("/") ? "" : "/") + u;
    if (p.startsWith("/audio/")) return String("http://" AP_IP_STR ":") + String(CHOR_PORT) + p;
    return String("http://" AP_IP_STR) + p;
}

static void listAudio(JsonArray arr) {
    File dir = LittleFS.open("/audio");
    if (!dir) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        JsonObject o = arr.add<JsonObject>();
        o["name"] = String(f.name());
        o["url"] = "/audio/" + String(f.name());
        o["bytes"] = f.size();
    }
}

void execute(JsonObjectConst cmd, JsonObject reply) {
    String c = cmd["cmd"] | "";
    String err;
    bool ok = false;
    if (cmd["id"].is<JsonVariantConst>()) reply["id"] = cmd["id"];
    reply["cmd"] = c;

    if (c == "state") {
        rabbit::stateJson(reply["data"].to<JsonObject>());
        ok = true;
    } else if (c == "wifi") {
        wifimgr::statusJson(reply["data"].to<JsonObject>());
        ok = true;
    } else if (c == "ears") {
        ok = rabbit::setEars(cmd["left"] | -1, cmd["right"] | -1, &err);
    } else if (c == "led") {
        rabbit::Rgb col{0, 0, 0};
        if (!parseColor(cmd["color"], col)) err = "bad color";
        else ok = rabbit::setLed(cmd["led"] | -1, col, &err);
    } else if (c == "leds") {
        rabbit::Rgb cols[5] = {};
        JsonArrayConst a = cmd["colors"];
        if (a.size() != 5) err = "colors must have 5 entries (bottom,left,middle,right,nose)";
        else {
            bool good = true;
            for (int i = 0; i < 5; i++) good &= parseColor(a[i], cols[i]);
            if (!good) err = "bad color";
            else ok = rabbit::setLeds(cols, &err);
        }
    } else if (c == "play") {
        String url = cmd["url"] | "";
        if (url.isEmpty()) err = "url required";
        else ok = rabbit::playUrl(absUrl(url), cmd["wait"] | true, &err);
    } else if (c == "stream") {
        String url = cmd["url"] | "";
        if (url.isEmpty()) err = "url required";
        else ok = rabbit::stream(absUrl(url), &err);
    } else if (c == "chor") {
        // {"tempo":10, "steps":[{"delay":0,"type":"led","led":1,"color":"#f00"},
        //                        {"delay":5,"type":"motor","ear":0,"pos":8,"dir":0}]}
        // ear 0 = rabbit's left, 1 = rabbit's right
        packets::Chor ch(cmd["tempo"] | 10);
        for (JsonObjectConst s : cmd["steps"].as<JsonArrayConst>()) {
            String t = s["type"] | "";
            uint8_t d = s["delay"] | 0;
            if (t == "led") {
                rabbit::Rgb col{0, 0, 0};
                parseColor(s["color"], col);
                ch.led(d, s["led"] | 0, col.r, col.g, col.b);
            } else if (t == "all") {
                rabbit::Rgb col{0, 0, 0};
                parseColor(s["color"], col);
                ch.allLeds(d, col.r, col.g, col.b);
            } else if (t == "motor") {
                // API ear: 0 = rabbit's left, 1 = rabbit's right; Violet motors are the other way round
                ch.motor(d, (s["ear"] | 0) ? 0 : 1, s["pos"] | 0, s["dir"] | 0);
            }
        }
        ok = rabbit::chor(ch.build(), &err);
    } else if (c == "program") {
        String text = cmd["text"] | "";
        if (text.isEmpty()) err = "text required";
        else ok = rabbit::program(text, &err);
    } else if (c == "ambient") {
        std::vector<std::pair<uint8_t, uint8_t>> s;
        for (JsonArrayConst p : cmd["services"].as<JsonArrayConst>()) s.push_back({p[0].as<uint8_t>(), p[1].as<uint8_t>()});
        if (s.empty()) err = "services required, e.g. [[1,3]]";
        else ok = rabbit::ambient(s, &err);
    } else if (c == "sleep") {
        ok = rabbit::sleep(true, &err);
    } else if (c == "wake") {
        ok = rabbit::sleep(false, &err);
    } else if (c == "reboot") {
        ok = rabbit::reboot(&err);
    } else if (c == "raw") {
        packets::Bytes b;
        if (!packets::fromHex(cmd["hex"] | "", b) || b.size() < 2) err = "hex trame required (7f ... ff)";
        else ok = rabbit::rawTrame(b, &err);
    } else if (c == "xmpp") {
        String xml = cmd["xml"] | "";
        if (xml.isEmpty()) err = "xml required";
        else if (!(ok = xmpp::sendRaw(xml))) err = "rabbit not connected";
    } else if (c == "diag") {
        // asks the bootcode for its internal state; reply arrives as a "diagnostics" event
        String node = cmd["node"] | "getrunningstate";
        if (!(ok = xmpp::queryRabbit(node.c_str()))) err = "rabbit not connected";
    } else if (c == "tune") {
        // experimental transport knobs (see docs/PROTOCOL.md)
        if (cmd["linger"].is<int>()) chor_server::lingerMs = cmd["linger"];
        reply["data"]["linger"] = chor_server::lingerMs;
        ok = true;
    } else if (c == "time") {
        net_services::setTime(cmd["epochMs"].as<uint64_t>());
        ok = true;
    } else if (c == "audio") {
        listAudio(reply["data"].to<JsonArray>());
        ok = true;
    } else if (c == "pair") {
        wifimgr::requestPair();
        ok = true;
    } else if (c == "lan") {
        // {"cmd":"lan","ssid":"MyWiFi","password":"..."}; empty ssid forgets
        wifimgr::setLan(cmd["ssid"] | "", cmd["password"] | "");
        ok = true;
    } else if (c == "lanScan") {
        wifimgr::requestLanScan();  // networks arrive as a "lanScan" event
        ok = true;
    } else if (c == "forget") {
        wifimgr::forget();
        ok = true;
    } else if (c == "log") {
        JsonArray a = reply["data"].to<JsonArray>();
        eventlog::forEach([&](const String &cat, const String &msg, uint32_t t) {
            JsonObject o = a.add<JsonObject>();
            o["t"] = t;
            o["cat"] = cat;
            o["msg"] = msg;
        });
        ok = true;
    } else {
        err = "unknown cmd '" + c + "'";
    }
    reply["ok"] = ok;
    if (!ok) reply["error"] = err;
}

static void sendDoc(AsyncWebSocketClient *client, JsonDocument &doc) {
    String s;
    serializeJson(doc, s);
    if (client) client->text(s);
    else ws.textAll(s);
}

static void onWs(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        LOGI("api", "ws client #%u connected from %s", client->id(), client->remoteIP().toString().c_str());
        JsonDocument hello;
        hello["type"] = "hello";
        hello["t"] = millis();
        rabbit::stateJson(hello["state"].to<JsonObject>());
        wifimgr::statusJson(hello["wifi"].to<JsonObject>());
        hello["clients"] = ws.count();
        sendDoc(client, hello);
    } else if (type == WS_EVT_DISCONNECT) {
        LOGI("api", "ws client #%u disconnected", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        // Reassemble messages that arrive in several pieces (large chors)
        static std::map<uint32_t, String> partial;
        String &buf = partial[client->id()];
        if (info->index == 0) buf = "";
        if (info->len > 32768) return;  // ignore absurdly large messages
        buf.concat((const char *)data, len);
        if (!info->final || info->index + len < info->len) return;
        String msg = buf;
        partial.erase(client->id());
        JsonDocument in;
        if (deserializeJson(in, msg)) {
            client->text("{\"type\":\"ack\",\"ok\":false,\"error\":\"bad json\"}");
            return;
        }
        JsonDocument out;
        out["type"] = "ack";
        execute(in.as<JsonObjectConst>(), out.as<JsonObject>());
        sendDoc(client, out);
    }
}

void begin(AsyncWebServer &server) {
    ws.onEvent(onWs);
    server.addHandler(&ws);

    rabbit::setEventSink([](JsonDocument &doc) {
        sendDoc(nullptr, doc);
        // also on serial, for tools/serial_bridge.py
        Serial.print("EVT ");
        serializeJson(doc, Serial);
        Serial.println();
    });
    eventlog::setSink([](const String &cat, const String &msg) {
        if (!ws.count()) return;
        JsonDocument d;
        d["type"] = "log";
        d["t"] = millis();
        d["cat"] = cat;
        d["msg"] = msg;
        String s;
        serializeJson(d, s);
        ws.textAll(s);
    });

    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *r) {
        JsonDocument d;
        rabbit::stateJson(d["rabbit"].to<JsonObject>());
        wifimgr::statusJson(d["wifi"].to<JsonObject>());
        d["clients"] = ws.count();
        d["uptimeMs"] = millis();
        d["heap"] = ESP.getFreeHeap();
        String s;
        serializeJson(d, s);
        r->send(200, "application/json", s);
    });

    // POST /api/cmd with a JSON command body
    server.on(
        "/api/cmd", HTTP_POST, [](AsyncWebServerRequest *r) {}, nullptr,
        [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
            if (index + len != total) return;  // small bodies only
            JsonDocument in, out;
            if (deserializeJson(in, data, len)) {
                r->send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
                return;
            }
            execute(in.as<JsonObjectConst>(), out.to<JsonObject>());
            String s;
            serializeJson(out, s);
            r->send(out["ok"] ? 200 : 409, "application/json", s);
        });

    // Audio library: GET list, POST multipart upload, DELETE ?name=
    server.on("/api/audio", HTTP_GET, [](AsyncWebServerRequest *r) {
        JsonDocument d;
        listAudio(d.to<JsonArray>());
        String s;
        serializeJson(d, s);
        r->send(200, "application/json", s);
    });
    server.on(
        "/api/audio", HTTP_POST,
        [](AsyncWebServerRequest *r) { r->send(200, "application/json", "{\"ok\":true}"); },
        [](AsyncWebServerRequest *r, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                String name = filename.substring(filename.lastIndexOf('/') + 1);
                name.replace(" ", "_");
                LittleFS.mkdir("/audio");
                upload = LittleFS.open("/audio/" + name, "w");
                LOGI("api", "audio upload %s", name.c_str());
            }
            if (upload) upload.write(data, len);
            if (final && upload) {
                LOGI("api", "audio upload done (%u bytes)", (unsigned)(index + len));
                upload.close();
            }
        });
    server.on("/api/audio", HTTP_DELETE, [](AsyncWebServerRequest *r) {
        String name = r->hasParam("name") ? r->getParam("name")->value() : "";
        bool ok = name.length() && name.indexOf('/') < 0 && LittleFS.remove("/audio/" + name);
        r->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });
    server.on("/api/fs", HTTP_GET, [](AsyncWebServerRequest *r) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"total\":%u,\"used\":%u}", (unsigned)LittleFS.totalBytes(), (unsigned)LittleFS.usedBytes());
        r->send(200, "application/json", buf);
    });
}

void loop() {
    ws.cleanupClients();
    if (millis() - lastStatePush > 2000 && ws.count()) {
        lastStatePush = millis();
        JsonDocument d;
        d["type"] = "state";
        d["t"] = millis();
        rabbit::stateJson(d["state"].to<JsonObject>());
        d["clients"] = ws.count();
        sendDoc(nullptr, d);
    }
}

}  // namespace api
