// SPDX-License-Identifier: GPL-2.0-or-later
// Nabaztag:tag <-> ESP32 bridge
// See README.md for architecture; docs/PROTOCOL.md for the rabbit side.
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "adpcm.h"
#include "api.h"
#include "chor_server.h"
#include "config.h"
#include "eventlog.h"
#include "packets.h"
#include "net_services.h"
#include "rabbit.h"
#include "rabbit_http.h"
#include "store.h"
#include "wifi_manager.h"
#include "xmpp_server.h"

static AsyncWebServer server(80);
static String serialLine;

static void printHelp() {
    Serial.println(
        "commands:\n"
        "  status | scan | probe | pair | forget | restart | kick [s] | diag [node] | dump PATH\n"
        "  {json}            any API command, e.g. {\"cmd\":\"ears\",\"left\":4,\"right\":10}\n"
        "  ears L R | led N #rrggbb | play URL | prog TEXT(\\n for newline)\n"
        "  raw HEX | sleep | wake | reboot");
}

static void runJson(const String &json) {
    JsonDocument in, out;
    if (deserializeJson(in, json)) {
        Serial.println("bad json");
        return;
    }
    api::execute(in.as<JsonObjectConst>(), out.to<JsonObject>());
    serializeJson(out, Serial);
    Serial.println();
}

static void serialCommand(String line) {
    line.trim();
    if (line.isEmpty()) return;
    if (line.startsWith("{")) return runJson(line);
    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? "" : line.substring(sp + 1);
    if (cmd == "help") printHelp();
    else if (cmd == "status") {
        runJson("{\"cmd\":\"state\"}");
        runJson("{\"cmd\":\"wifi\"}");
        Serial.printf("heap %u, fs %u/%u\n", ESP.getFreeHeap(), (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    } else if (cmd == "scan") wifimgr::scanForRabbit();
    else if (cmd == "probe") wifimgr::probe();
    else if (cmd == "pair") wifimgr::requestPair();
    else if (cmd == "forget") wifimgr::forget();
    else if (cmd == "diag") runJson("{\"cmd\":\"diag\",\"node\":\"" + (rest.length() ? rest : String("getrunningstate")) + "\"}");
    else if (cmd == "dump") {
        // base64 of a file on the bridge's flash, for offline inspection
        File f = LittleFS.open(rest, "r");
        if (!f) Serial.println("no such file");
        else {
            packets::Bytes b(f.size());
            f.read(b.data(), b.size());
            f.close();
            Serial.printf("DUMP %s %u\n", rest.c_str(), (unsigned)b.size());
            String enc = packets::base64(b);
            for (size_t i = 0; i < enc.length(); i += 76) {
                Serial.println(enc.substring(i, i + 76));
                delay(2);
            }
            Serial.println("END DUMP");
        }
    } else if (cmd == "convert") {
        String dst = rest;
        dst.replace(".wav", "-pcm.wav");
        Serial.println(adpcmToPcmWav(LittleFS, rest.c_str(), dst.c_str()) ? "converted -> " + dst : String("conversion failed"));
    } else if (cmd == "kick") wifimgr::kickRabbit((rest.toInt() > 0 ? rest.toInt() : 30) * 1000UL);
    else if (cmd == "restart") ESP.restart();
    else if (cmd == "ears") {
        int l = -1, r = -1;
        sscanf(rest.c_str(), "%d %d", &l, &r);
        runJson("{\"cmd\":\"ears\",\"left\":" + String(l) + ",\"right\":" + String(r) + "}");
    } else if (cmd == "led") {
        int sp2 = rest.indexOf(' ');
        runJson("{\"cmd\":\"led\",\"led\":" + rest.substring(0, sp2) + ",\"color\":\"" + rest.substring(sp2 + 1) + "\"}");
    } else if (cmd == "play") runJson("{\"cmd\":\"play\",\"url\":\"" + rest + "\"}");
    else if (cmd == "prog") {
        JsonDocument d;
        rest.replace("\\n", "\n");
        d["cmd"] = "program";
        d["text"] = rest;
        String s;
        serializeJson(d, s);
        runJson(s);
    } else if (cmd == "raw") runJson("{\"cmd\":\"raw\",\"hex\":\"" + rest + "\"}");
    else if (cmd == "sleep" || cmd == "wake" || cmd == "reboot") runJson("{\"cmd\":\"" + cmd + "\"}");
    else Serial.println("unknown command - try help");
}

void setup() {
    Serial.begin(115200);
    delay(200);
    eventlog::begin();
    LOGI("boot", "BunnyHop starting");
    if (!LittleFS.begin(true)) LOGI("boot", "LittleFS mount failed");
    store::begin();

    wifimgr::begin();
    net_services::begin();
    xmpp::begin();
    chor_server::begin(CHOR_PORT);

    rabbit_http::begin(server);
    api::begin(server);
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html").setCacheControl("no-cache");  // always revalidate: devices pick up web app updates
    server.onNotFound([](AsyncWebServerRequest *r) {
        if (rabbit_http::isRabbitPath(r->url())) {
            LOGI("rabbit", "unhandled %s %s", r->methodToString(), r->url().c_str());
            r->send(200, "application/octet-stream", "");
            return;
        }
        // Captive-portal probes and stray hostnames -> the app, but only for
        // devices on the rabbit's network (home Wi-Fi users can't reach it)
        if (wifimgr::fromRabbitNetwork(r->client()->localIP())) r->redirect("http://" AP_IP_STR "/");
        else r->send(404, "text/plain", "not found");
    });
    server.begin();
    LOGI("boot", "web server on http://%s/", AP_IP_STR);
    printHelp();
}

void loop() {
    wifimgr::loop();
    xmpp::loop();
    chor_server::loop();
    rabbit::loop();
    net_services::loop();
    api::loop();
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            serialCommand(serialLine);
            serialLine = "";
        } else serialLine += c;
    }
    delay(2);
}
