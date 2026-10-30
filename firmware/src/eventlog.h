#pragma once
#include <Arduino.h>
#include <functional>

// Diagnostic log: every line goes to Serial, a ring buffer (for clients that
// connect later) and an optional sink (the WebSocket broadcaster).
namespace eventlog {

using Sink = std::function<void(const String &cat, const String &msg)>;

void begin();
void setSink(Sink s);
void log(const char *cat, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// Calls fn(cat, msg, millis) for each buffered line, oldest first.
void forEach(std::function<void(const String &, const String &, uint32_t)> fn);

}  // namespace eventlog

#define LOGI(cat, ...) eventlog::log(cat, __VA_ARGS__)
