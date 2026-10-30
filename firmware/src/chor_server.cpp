// SPDX-License-Identifier: GPL-2.0-or-later
#include "chor_server.h"
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <vector>
#include "eventlog.h"
#include "rabbit.h"

namespace chor_server {

static AsyncServer *server;
uint32_t lingerMs = 300;  // keep the connection open this long after the last ACK

struct Conn {
    AsyncClient *c;
    String req;
    packets::Bytes body;
    File file;          // or stream this (audio)
    size_t fileSize = 0;
    size_t sent = 0;
    size_t total = 0;  // header + body bytes to be acknowledged
    size_t acked = 0;
    bool responded = false;
    uint32_t ackedAt = 0;
    uint32_t openedAt = 0;
};
static std::vector<Conn *> conns;
static SemaphoreHandle_t mtx;

static void respond(Conn *k) {
    // "GET /12.chor HTTP/1.0"
    int s1 = k->req.indexOf(' '), s2 = k->req.indexOf(' ', s1 + 1);
    String path = k->req.substring(s1 + 1, s2);
    String hdr;
    if (path.startsWith("/audio/") && path.indexOf("..") < 0 && LittleFS.exists(path)) {
        k->file = LittleFS.open(path, "r");
        k->fileSize = k->file.size();
        String type = path.endsWith(".mp3") ? "audio/mpeg" : path.endsWith(".wav") ? "audio/wav" : "application/octet-stream";
        hdr = "HTTP/1.0 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + String(k->fileSize) + "\r\n\r\n";
        LOGI("http", "resource server: %s (%u bytes)", path.c_str(), (unsigned)k->fileSize);
        k->total = hdr.length() + k->fileSize;
        k->c->add(hdr.c_str(), hdr.length(), ASYNC_WRITE_FLAG_COPY);
        k->c->send();
        k->responded = true;
        return;
    }
    uint32_t id = path.substring(path.lastIndexOf('/') + 1).toInt();
    if (!rabbit::chorFile(id, k->body)) {
        hdr = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        k->body.clear();
    } else {
        hdr = "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + String(k->body.size()) + "\r\n\r\n";
    }
    LOGI("http", "resource server: %s (%u bytes)", path.c_str(), (unsigned)k->body.size());
    k->total = hdr.length() + k->body.size();
    k->c->add(hdr.c_str(), hdr.length(), ASYNC_WRITE_FLAG_COPY);
    k->c->send();
    k->responded = true;
}

static void pump(Conn *k) {
    if (k->file) {
        uint8_t buf[1024];
        while (k->sent < k->fileSize && k->c->space() > 0) {
            size_t want = std::min({sizeof(buf), k->c->space(), k->fileSize - k->sent});
            k->file.seek(k->sent);
            size_t n = k->file.read(buf, want);
            if (!n) break;
            n = k->c->add((const char *)buf, n, ASYNC_WRITE_FLAG_COPY);
            if (!n) break;
            k->sent += n;
            k->c->send();
        }
        return;
    }
    while (k->sent < k->body.size() && k->c->space() > 0) {
        size_t n = std::min(k->c->space(), k->body.size() - k->sent);
        n = k->c->add((const char *)k->body.data() + k->sent, n, ASYNC_WRITE_FLAG_COPY);
        if (!n) break;
        k->sent += n;
        k->c->send();
    }
}

void begin(uint16_t port) {
    mtx = xSemaphoreCreateRecursiveMutex();
    server = new AsyncServer(port);
    server->onClient(
        [](void *, AsyncClient *c) {
            Conn *k = new Conn();
            k->c = c;
            k->openedAt = millis();
            xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
            conns.push_back(k);
            xSemaphoreGiveRecursive(mtx);
            c->onData(
                [](void *arg, AsyncClient *c, void *data, size_t len) {
                    Conn *k = (Conn *)arg;
                    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
                    k->req.concat((const char *)data, len);
                    if (!k->responded && k->req.indexOf("\r\n\r\n") >= 0) {
                        respond(k);
                        pump(k);
                    }
                    xSemaphoreGiveRecursive(mtx);
                },
                k);
            c->onAck(
                [](void *arg, AsyncClient *c, size_t len, uint32_t) {
                    Conn *k = (Conn *)arg;
                    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
                    k->acked += len;
                    pump(k);
                    if (k->responded && k->acked >= k->total && !k->ackedAt) k->ackedAt = millis();
                    xSemaphoreGiveRecursive(mtx);
                },
                k);
            c->onDisconnect(
                [](void *arg, AsyncClient *c) {
                    Conn *k = (Conn *)arg;
                    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
                    for (size_t i = 0; i < conns.size(); i++)
                        if (conns[i] == k) conns.erase(conns.begin() + i);
                    xSemaphoreGiveRecursive(mtx);
                    if (k->file) k->file.close();
                    else if (k->responded) rabbit::chorDelivered();
                    delete k;
                    delete c;
                },
                k);
        },
        nullptr);
    server->begin();
    LOGI("http", "rabbit resource server on :%u", port);
}

void loop() {
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    uint32_t now = millis();
    for (Conn *k : conns) {
        bool done = k->ackedAt && now - k->ackedAt > lingerMs;
        bool stale = now - k->openedAt > 15000;
        if (done || stale) {
            k->ackedAt = 0;
            k->openedAt = now;  // don't re-close while the FIN is in flight
            k->c->close();
        }
    }
    xSemaphoreGiveRecursive(mtx);
}

}  // namespace chor_server
