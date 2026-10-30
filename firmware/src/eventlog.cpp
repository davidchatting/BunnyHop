#include "eventlog.h"
#include <stdarg.h>

namespace eventlog {

static const int RING = 80;
struct Line {
    String cat, msg;
    uint32_t t;
};
static Line ring[RING];
static int head = 0, count = 0;
static Sink sink;
static SemaphoreHandle_t mtx;

void begin() { mtx = xSemaphoreCreateRecursiveMutex(); }

void setSink(Sink s) { sink = s; }

void log(const char *cat, const char *fmt, ...) {
    char buf[384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uint32_t t = millis();
    Serial.printf("[%7lu.%03lu] %-6s %s\n", (unsigned long)(t / 1000), (unsigned long)(t % 1000), cat, buf);

    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    ring[head] = {cat, buf, t};
    head = (head + 1) % RING;
    if (count < RING) count++;
    Sink s = sink;
    xSemaphoreGiveRecursive(mtx);
    if (s) s(cat, buf);
}

void forEach(std::function<void(const String &, const String &, uint32_t)> fn) {
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    int start = (head - count + RING) % RING;
    for (int i = 0; i < count; i++) {
        const Line &l = ring[(start + i) % RING];
        fn(l.cat, l.msg, l.t);
    }
    xSemaphoreGiveRecursive(mtx);
}

}  // namespace eventlog
