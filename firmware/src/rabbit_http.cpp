#include "rabbit_http.h"
#include <LittleFS.h>
#include "config.h"
#include "eventlog.h"
#include "rabbit.h"
#include "store.h"

namespace rabbit_http {

static const char *BOOTCODE = "/vl/bootcode.bin";
static const int MAX_RECORDINGS = 4;
static int recIndex = 0;
static File recFile;
static String recPath;
static size_t recBytes = 0;

bool isRabbitPath(const String &url) { return url.startsWith("/vl/") || url.endsWith(".jsp"); }

static String query(AsyncWebServerRequest *r) {
    String q;
    for (size_t i = 0; i < r->params(); i++) {
        const AsyncWebParameter *p = r->getParam(i);
        if (p->isPost() || p->isFile()) continue;
        q += (q.length() ? "&" : "") + p->name() + "=" + p->value();
    }
    return q;
}

static void logReq(AsyncWebServerRequest *r) {
    LOGI("rabbit", "%s %s?%s from %s", r->methodToString(), r->url().c_str(), query(r).c_str(),
         r->client()->remoteIP().toString().c_str());
    rabbit::touch(r->client()->remoteIP().toString());
}

// Resources the bootcode downloads before running a program (MU/CH). Its
// HTTP client only treats a download as complete when the connection
// closes, so log timing to diagnose stalls.
static void resourceTiming(AsyncWebServerRequest *r) {
    uint32_t t0 = millis();
    String url = r->url();
    LOGI("http", "rabbit fetch %s (HTTP/1.%d, Connection: %s)", url.c_str(), r->version(),
         r->hasHeader("Connection") ? r->header("Connection").c_str() : "-");
    r->onDisconnect([t0, url]() { LOGI("http", "%s connection closed after %lums", url.c_str(), (unsigned long)(millis() - t0)); });
}

static void bootcode(AsyncWebServerRequest *r) {
    logReq(r);
    auto &s = rabbit::state();
    s.bootcodeServedAt = millis();
    if (r->hasParam("m")) {
        String mac = r->getParam("m")->value();
        mac.replace(":", "");
        if (mac.length() == 12) s.sn = mac;
    }
    rabbit::setPhase("bootcode");
    rabbit::emit("boot", [&](JsonObject o) {
        o["loader"] = r->hasParam("v") ? r->getParam("v")->value() : "";
        o["mac"] = s.sn;
    });
    if (!LittleFS.exists(BOOTCODE)) {
        LOGI("rabbit", "bootcode missing from filesystem!");
        r->send(404);
        return;
    }
    r->send(LittleFS, BOOTCODE, "application/octet-stream");
}

static void locate(AsyncWebServerRequest *r) {
    logReq(r);
    auto &s = rabbit::state();
    if (r->hasParam("sn")) s.sn = r->getParam("sn")->value();
    if (r->hasParam("v")) s.bootcodeRev = r->getParam("v")->value();
    rabbit::setPhase("locate");
    r->send(200, "text/plain",
            "ping " AP_IP_STR "\nbroad " AP_IP_STR "\nxmpp_domain " XMPP_DOMAIN ":" + String(XMPP_PORT) + "\n");
}

static void rfid(AsyncWebServerRequest *r) {
    logReq(r);
    String tag = r->hasParam("t") ? r->getParam("t")->value() : "";
    if (tag.length()) rabbit::onRfid(tag);
    r->send(200, "application/octet-stream", "");
}

// The rabbit POSTs its microphone recording as the raw request body.
static void recordBody(AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        LittleFS.mkdir("/rec");
        recPath = "/rec/rec" + String(recIndex) + ".wav";
        recIndex = (recIndex + 1) % MAX_RECORDINGS;
        recFile = LittleFS.open(recPath, "w");
        recBytes = 0;
        LOGI("rabbit", "recording upload started (%u bytes expected)", (unsigned)total);
    }
    if (recFile) recFile.write(data, len);
    recBytes += len;
    if (index + len >= total && recFile) recFile.close();
}

static void recordDone(AsyncWebServerRequest *r) {
    logReq(r);
    if (recFile) recFile.close();
    if (recBytes) rabbit::onRecording(recPath, recBytes);
    recBytes = 0;
    r->send(200, "application/octet-stream", "");
}

static void chorFile(AsyncWebServerRequest *r) {
    logReq(r);
    resourceTiming(r);
    String name = r->url().substring(strlen("/vl/chor/"));
    uint32_t id = name.toInt();
    packets::Bytes b;
    if (!rabbit::chorFile(id, b)) {
        r->send(404);
        return;
    }
    AsyncWebServerResponse *resp = r->beginResponse(200, "application/octet-stream", b.data(), b.size());
    r->send(resp);
}

static void sendMail(AsyncWebServerRequest *r) {
    logReq(r);
    // The bootcode reports XMPP registration/login failures here. Like
    // OpenJabNab, forget the stored password so the rabbit can re-register.
    store::remove(K_XMPP_PW);
    r->send(200, "text/plain", "");
}

static void otherRabbit(AsyncWebServerRequest *r) {
    logReq(r);
    r->send(200, "application/octet-stream", "");
}

static void audioFile(AsyncWebServerRequest *r) {
    String path = r->url();
    if (path.indexOf("..") >= 0 || !LittleFS.exists(path)) {
        r->send(404);
        return;
    }
    resourceTiming(r);
    String type = path.endsWith(".mp3") ? "audio/mpeg" : path.endsWith(".wav") ? "audio/wav" : "application/octet-stream";
    r->send(LittleFS, path, type);
}

void begin(AsyncWebServer &server) {
    server.on("/audio/*", HTTP_GET, audioFile);
    server.on("/vl/bc.jsp", HTTP_GET, bootcode);
    server.on("/bc.jsp", HTTP_GET, bootcode);
    server.on("/vl/locate.jsp", HTTP_GET, locate);
    server.on("/locate.jsp", HTTP_GET, locate);
    server.on("/vl/rfid.jsp", HTTP_ANY, rfid);
    server.on("/vl/record.jsp", HTTP_POST, recordDone, nullptr, recordBody);
    server.on("/vl/sendMailXMPP.jsp", HTTP_ANY, sendMail);
    server.on("/vl/p4.jsp", HTTP_ANY, otherRabbit);
    server.on("/vl/itmode.jsp", HTTP_ANY, otherRabbit);
    server.on("/vl/chor/*", HTTP_GET, chorFile);
    // recordings, for clients to play back
    server.serveStatic("/rec/", LittleFS, "/rec/").setCacheControl("no-store");
}

}  // namespace rabbit_http
