// SPDX-License-Identifier: GPL-2.0-or-later
#include "net_services.h"
#include <AsyncUDP.h>
#include <DNSServer.h>
#include <sys/time.h>
#include "config.h"
#include "eventlog.h"

namespace net_services {

static DNSServer dns;
static AsyncUDP ntp;
static bool known = false;

void setTime(uint64_t epochMs) {
    struct timeval tv;
    tv.tv_sec = epochMs / 1000;
    tv.tv_usec = (epochMs % 1000) * 1000;
    settimeofday(&tv, nullptr);
    if (!known) LOGI("net", "clock set from client: %lu", (unsigned long)tv.tv_sec);
    known = true;
}

bool timeKnown() { return known; }

static void putU32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

void begin() {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", IPAddress(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D));

    if (ntp.listen(123)) {
        ntp.onPacket([](AsyncUDPPacket p) {
            if (p.length() < 48) return;
            uint8_t r[48] = {0};
            struct timeval tv;
            gettimeofday(&tv, nullptr);
            uint32_t secs = tv.tv_sec + 2208988800UL;  // NTP epoch is 1900
            uint32_t frac = (uint32_t)((double)tv.tv_usec * 4294.967296);
            r[0] = 0x24;  // LI 0, version 4, mode 4 (server)
            r[1] = known ? 2 : 16;  // stratum (16 = unsynchronised)
            r[2] = 6;
            r[3] = 0xEC;
            memcpy(r + 24, p.data() + 40, 8);  // originate = client's transmit
            putU32(r + 16, secs);
            putU32(r + 32, secs);
            putU32(r + 36, frac);
            putU32(r + 40, secs);
            putU32(r + 44, frac);
            p.write(r, sizeof(r));
        });
    }
    LOGI("net", "DNS catch-all and NTP responder running");
}

void loop() { dns.processNextRequest(); }

}  // namespace net_services
