// SPDX-License-Identifier: GPL-2.0-or-later
#include "wifi_manager.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <map>
#include "config.h"
#include "eventlog.h"
#include "rabbit.h"
#include "store.h"

#ifndef AUTO_PAIR
#define AUTO_PAIR 1
#endif

namespace wifimgr {

static String ap;
static bool pairRequested = false;
static uint32_t lastRabbitContact = 0;
static uint32_t lastScan = 0;
static String lastPairResult = "never";
static String setupFields;
// stale-session watchdog (see kickRabbit)
static volatile bool rabbitAssociated = false;
static volatile uint32_t rabbitAssociatedAt = 0;
static uint32_t lastKick = 0;  // "name=value&..." as scraped from the setup form

static void startAp(const String &ssid) {
    ap = ssid;
    IPAddress ip(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    // Must be an open network for the rabbit. See README "Security".
    WiFi.softAP(ap.c_str(), nullptr, AP_CHANNEL, 0, AP_MAX_CLIENTS);
    // The rabbit's radio only decodes 802.11b (DSSS/CCK). With g/n enabled
    // the ESP32 sends it OFDM frames it can't receive, so the AP is b-only.
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B);
    esp_wifi_set_event_mask(WIFI_EVENT_MASK_NONE);  // enables AP probe-request events
    LOGI("wifi", "AP '%s' up at %s (channel %d)", ap.c_str(), WiFi.softAPIP().toString().c_str(), AP_CHANNEL);
}

static String ssidFromBssid(const String &bssid) {
    String s = bssid;
    s.replace(":", "");
    s.toUpperCase();
    return String(AP_SSID_PREFIX) + s.substring(6);
}

// AP-side diagnostics: who associates, who gets an address, and whether
// the rabbit (Violet OUI 00:13:d3) is probing for our SSID at all.
static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
    char mac[18];
    switch (ev) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
            const uint8_t *m = info.wifi_ap_staconnected.mac;
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
            bool isRabbit = m[0] == 0x00 && m[1] == 0x13 && m[2] == 0xd3;
            if (isRabbit) {
                rabbitAssociated = true;
                rabbitAssociatedAt = millis();
            }
            LOGI("wifi", "station %s associated%s", mac, isRabbit ? " (rabbit)" : "");
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
            const uint8_t *m = info.wifi_ap_stadisconnected.mac;
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
            if (m[0] == 0x00 && m[1] == 0x13 && m[2] == 0xd3) rabbitAssociated = false;
            LOGI("wifi", "station %s left", mac);
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
            LOGI("wifi", "DHCP lease %s", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: {
            const uint8_t *m = info.wifi_ap_probereqrecved.mac;
            static uint32_t last = 0;
            if (m[0] == 0x00 && m[1] == 0x13 && m[2] == 0xd3 && millis() - last > 2000) {
                last = millis();
                LOGI("wifi", "probe request from rabbit (rssi %d)", info.wifi_ap_probereqrecved.rssi);
            }
            break;
        }
        default:
            break;
    }
}

void begin() {
    WiFi.onEvent(onWifiEvent);
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    String saved = store::get(K_AP_SSID);
    startAp(saved.length() ? saved : String(AP_FALLBACK_SSID));
    if (!saved.length()) {
        LOGI("wifi", "not provisioned yet - will scan for a rabbit in setup mode%s", AUTO_PAIR ? "" : " (auto-pair disabled)");
        pairRequested = AUTO_PAIR;
    } else {
        LOGI("wifi", "provisioned for rabbit %s; waiting for it to join", store::get(K_RABBIT_SN, "?").c_str());
    }
    lastRabbitContact = millis();
}

String apSsid() { return ap; }
bool paired() { return store::get(K_AP_SSID).length() > 0; }

String scanForRabbit(String *bssidOut, int *channelOut) {
    LOGI("wifi", "scanning for rabbits in setup mode...");
    int n = WiFi.scanNetworks(false, false, false, 300);
    String found;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        // setup-mode SSID is exactly "Nabaztag" + 2 hex digits (ours is longer)
        bool isSetup = ssid.startsWith(RABBIT_SETUP_SSID_PREFIX) && ssid.length() == strlen(RABBIT_SETUP_SSID_PREFIX) + 2;
        LOGI("wifi", "  %-24s ch%-2d %4ddBm %s%s", ssid.c_str(), WiFi.channel(i), WiFi.RSSI(i), WiFi.BSSIDstr(i).c_str(),
             isSetup ? "  <- rabbit setup AP" : "");
        if (isSetup && found.isEmpty()) {
            found = ssid;
            if (bssidOut) *bssidOut = WiFi.BSSIDstr(i);
            if (channelOut) *channelOut = WiFi.channel(i);
        }
    }
    WiFi.scanDelete();
    rabbit::emit("scan", [&](JsonObject o) { o["found"] = found; });
    return found;
}

static bool joinSetup(const String &ssid, const String &bssid, int channel) {
    uint8_t b[6];
    sscanf(bssid.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
    // The setup network doesn't run DHCP - use a static address.
    WiFi.config(IPAddress(192, 168, 0, 100), IPAddress(192, 168, 0, 1), IPAddress(255, 255, 255, 0));
    WiFi.begin(ssid.c_str(), nullptr, channel, b);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(100);
    bool ok = WiFi.status() == WL_CONNECTED;
    LOGI("wifi", "join %s: %s", ssid.c_str(), ok ? "connected" : "FAILED");
    return ok;
}

static void leaveSetup() {
    WiFi.disconnect(false, true);
    delay(200);
}

// Plain HTTP/1.0 GET against the rabbit's own tiny web server.
static bool rabbitGet(const String &path, String &body) {
    WiFiClient c;
    if (!c.connect(RABBIT_SETUP_IP, 80, 5000)) {
        LOGI("wifi", "GET %s: connect failed", path.c_str());
        return false;
    }
    c.printf("GET %s HTTP/1.0\r\nHost: " RABBIT_SETUP_IP "\r\n\r\n", path.c_str());
    String resp;
    uint32_t t0 = millis();
    while ((c.connected() || c.available()) && millis() - t0 < 8000) {
        while (c.available()) resp += (char)c.read();
        delay(5);
    }
    c.stop();
    int hdrEnd = resp.indexOf("\r\n\r\n");
    String status = resp.substring(0, resp.indexOf("\r\n"));
    body = hdrEnd >= 0 ? resp.substring(hdrEnd + 4) : resp;
    LOGI("wifi", "GET %s -> '%s', %u bytes", path.c_str(), status.c_str(), (unsigned)body.length());
    return status.indexOf("200") > 0;
}

static String attr(const String &tag, const char *name) {
    String key = String(name) + "=";
    int i = 0;
    String low = tag;
    low.toLowerCase();
    while ((i = low.indexOf(key, i)) >= 0) {
        if (i == 0 || isspace((unsigned char)low[i - 1])) break;
        i++;
    }
    if (i < 0) return "";
    i += key.length();
    char q = tag[i];
    if (q == '"' || q == '\'') {
        int e = tag.indexOf(q, i + 1);
        return tag.substring(i + 1, e);
    }
    int e = i;
    while (e < (int)tag.length() && !isspace((unsigned char)tag[e]) && tag[e] != '>') e++;
    return tag.substring(i, e);
}

// Extract the current value of every form control on the setup page, so
// provisioning can resubmit the form exactly as a browser would, changing
// only the fields we need.
static std::vector<std::pair<String, String>> scrapeForm(const String &html) {
    std::vector<std::pair<String, String>> fields;
    String low = html;
    low.toLowerCase();
    int i = 0;
    while (true) {
        int inp = low.indexOf("<input", i);
        int sel = low.indexOf("<select", i);
        if (inp < 0 && sel < 0) break;
        if (sel < 0 || (inp >= 0 && inp < sel)) {
            int e = html.indexOf('>', inp);
            if (e < 0) break;  // truncated page
            String tag = html.substring(inp, e + 1);
            String type = attr(tag, "type");
            type.toLowerCase();
            String name = attr(tag, "name");
            if (name.length() && type != "submit" && type != "button") {
                bool isCheck = type == "radio" || type == "checkbox";
                String ltag = tag;
                ltag.toLowerCase();
                if (!isCheck || ltag.indexOf("checked") >= 0) fields.push_back({name, attr(tag, "value")});
            }
            i = e + 1;
        } else {
            int e = low.indexOf("</select>", sel);
            if (e < 0) break;  // truncated page
            String block = html.substring(sel, e);
            String name = attr(block.substring(0, block.indexOf('>') + 1), "name");
            String val, first;
            int o = 0;
            String lblock = block;
            lblock.toLowerCase();
            while ((o = lblock.indexOf("<option", o)) >= 0) {
                int oe = block.indexOf('>', o);
                String otag = block.substring(o, oe + 1);
                String v = attr(otag, "value");
                if (first.isEmpty()) first = v;
                String lo = otag;
                lo.toLowerCase();
                if (lo.indexOf("selected") >= 0) val = v;
                o = oe + 1;
            }
            if (name.length()) fields.push_back({name, val.length() ? val : first});
            i = e < 0 ? sel + 7 : e;
        }
    }
    return fields;
}

static String urlEncode(const String &s) {
    String o;
    char tmp[4];
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
        else if (c == ' ') o += '+';
        else {
            snprintf(tmp, sizeof(tmp), "%%%02X", (uint8_t)c);
            o += tmp;
        }
    }
    return o;
}

static String textOnly(const String &html) {
    String out;
    bool inTag = false;
    for (char c : html) {
        if (c == '<') inTag = true;
        else if (c == '>') {
            inTag = false;
            out += ' ';
        } else if (!inTag && c != '\r' && c != '\n' && c != '\t') out += c;
    }
    while (out.indexOf("  ") >= 0) out.replace("  ", " ");
    out.trim();
    return out;
}

// First whitespace-delimited word after a label in the page text
static String after(const String &text, const char *label) {
    int i = text.indexOf(label);
    if (i < 0) return "";
    String rest = text.substring(i + strlen(label));
    rest.trim();
    int sp = rest.indexOf(' ');
    return sp < 0 ? rest : rest.substring(0, sp);
}

bool probe() {
    String ssid, bssid;
    int ch = 0;
    ssid = scanForRabbit(&bssid, &ch);
    if (ssid.isEmpty()) return false;
    if (!joinSetup(ssid, bssid, ch)) {
        leaveSetup();
        return false;
    }
    String body;
    rabbitGet("/", body);
    String text = textOnly(body);
    for (size_t i = 0; i < text.length(); i += 160) {
        LOGI("page", "%s", text.substring(i, i + 160).c_str());
        delay(20);  // don't overrun the serial buffer
    }
    for (auto &f : scrapeForm(body)) LOGI("form", "%s = '%s'", f.first.c_str(), f.second.c_str());
    LOGI("wifi", "serial %s, loader firmware %s", after(text, "Serial number:").c_str(), after(text, "Firmware:").c_str());
    leaveSetup();
    return true;
}

bool pair() {
    String ssid, bssid;
    int ch = 0;
    ssid = scanForRabbit(&bssid, &ch);
    if (ssid.isEmpty()) {
        lastPairResult = "no rabbit in setup mode found";
        return false;
    }
    rabbit::emit("pairing", [&](JsonObject o) {
        o["step"] = "joining";
        o["ssid"] = ssid;
    });
    if (!joinSetup(ssid, bssid, ch)) {
        leaveSetup();
        lastPairResult = "could not join " + ssid;
        return false;
    }
    // The rabbit's TCP stack sometimes drops the tail of the page; retry
    // until the whole form has arrived.
    String page;
    bool gotPage = false;
    for (int attempt = 0; attempt < 4 && !gotPage; attempt++) gotPage = rabbitGet("/", page) && page.indexOf("</form>") > 0;
    if (!gotPage) {
        leaveSetup();
        lastPairResult = "setup page not reachable";
        return false;
    }
    auto fields = scrapeForm(page);
    // Our AP SSID: keep the existing one when re-pairing the same rabbit
    String newAp = store::get(K_AP_SSID);
    String text = textOnly(page);
    String serial = after(text, "Serial number:");
    String firmware = after(text, "Firmware:");
    if (newAp.isEmpty() || store::get(K_SETUP_BSSID) != bssid) newAp = ssidFromBssid(serial.length() == 17 ? serial : bssid);
    String info = "serial " + serial + ", loader firmware " + firmware + ", setup SSID " + ssid;
    store::set(K_SETUP_INFO, info);
    store::set(K_SETUP_BSSID, bssid);
    store::set(K_SETUP_SSID, ssid);
    LOGI("wifi", "setup page: %s", info.c_str());


    // Field meanings, from the rabbit's own setup form (see docs/PROTOCOL.md)
    std::map<String, String> overrides = {
        {"w", "-"},             // SSID picked from the rabbit's scan list: none
        {"k", newAp},           // "OR type your network name"; our AP may not be in its list yet
        {"m", "0"},             // encryption: 0 none
        {"n", ""},              // key
        {"f", "1"},             // DHCP on
        {"a", RABBIT_PLATFORM}, // "Violet Platform"
        // A browser also sends the clicked submit button. Without
        // z="Update and Start" the rabbit saves the settings but stays in
        // setup mode instead of restarting.
        {"z", "Update and Start"},
    };
    String q;
    std::vector<String> seen;
    for (auto &f : fields) {
        auto it = overrides.find(f.first);
        String v = it != overrides.end() ? it->second : f.second;
        q += (q.length() ? "&" : "") + f.first + "=" + urlEncode(v);
        seen.push_back(f.first);
    }
    for (auto &o : overrides)
        if (std::find(seen.begin(), seen.end(), o.first) == seen.end()) q += "&" + o.first + "=" + urlEncode(o.second);
    setupFields = q;
    LOGI("wifi", "submitting /b.htm?%s", q.c_str());
    String resp;
    bool ok = rabbitGet("/b.htm?" + q, resp);
    LOGI("wifi", "b.htm response: %s", textOnly(resp).substring(0, 200).c_str());
    leaveSetup();
    if (!ok) {
        lastPairResult = "b.htm push failed";
        return false;
    }
    store::set(K_AP_SSID, newAp);
    if (newAp != ap) startAp(newAp);
    lastPairResult = "ok: rabbit told to join " + newAp;
    lastRabbitContact = millis();
    rabbit::emit("pairing", [&](JsonObject o) {
        o["step"] = "done";
        o["ap"] = newAp;
    });
    return true;
}

void requestPair() { pairRequested = true; }

void kickRabbit(uint32_t offMs) {
    // A rabbit whose XMPP session died with a bridge restart never notices
    // (the bootcode sends no XMPP pings) and a deauth alone just makes it
    // re-associate. Taking the AP away long enough makes it reboot and
    // start over from bc.jsp.
    LOGI("wifi", "AP off for %lus to make the rabbit restart", (unsigned long)(offMs / 1000));
    WiFi.softAPdisconnect(false);
    WiFi.mode(WIFI_STA);
    delay(offMs);
    WiFi.mode(WIFI_AP_STA);
    startAp(ap);
}

void forget() {
    store::remove(K_AP_SSID);
    store::remove(K_SETUP_BSSID);
    store::remove(K_SETUP_SSID);
    store::remove(K_RABBIT_SN);
    store::remove(K_XMPP_PW);
    LOGI("wifi", "provisioning forgotten");
}

void loop() {
    if (rabbit::state().xmpp) lastRabbitContact = millis();
    if (pairRequested) {
        pairRequested = false;
        lastScan = millis();
        if (!pair()) LOGI("wifi", "pairing: %s", lastPairResult.c_str());
        return;
    }
    uint32_t now = millis();
    // Rabbit on the AP but silent: a session left over from before a bridge
    // restart. Make it reboot.
    auto &rs = rabbit::state();
    uint32_t lastTraffic = max(rs.lastSeen, (uint32_t)rabbitAssociatedAt);
    if (rabbitAssociated && !rs.xmpp && now - lastTraffic > STALE_RABBIT_MS && (!lastKick || now - lastKick > KICK_INTERVAL_MS)) {
        lastKick = now;
        LOGI("wifi", "rabbit associated but silent for %lus - stale session", (unsigned long)((now - lastTraffic) / 1000));
        kickRabbit(KICK_AP_OFF_MS);
        return;
    }
    // Rabbit missing for a while: look for one in setup mode periodically
    if (AUTO_PAIR && now - lastRabbitContact > RABBIT_RESCAN_AFTER_MS && now - lastScan > RABBIT_RESCAN_INTERVAL_MS) {
        lastScan = now;
        String bssid;
        int ch;
        if (scanForRabbit(&bssid, &ch).length()) pairRequested = true;
    }
}

void statusJson(JsonObject o) {
    o["ap"] = ap;
    o["paired"] = paired();
    o["apClients"] = WiFi.softAPgetStationNum();
    o["setupSsid"] = store::get(K_SETUP_SSID);
    o["setupBssid"] = store::get(K_SETUP_BSSID);
    o["setupInfo"] = store::get(K_SETUP_INFO);
    o["rabbitSn"] = store::get(K_RABBIT_SN);
    o["lastPair"] = lastPairResult;
}

}  // namespace wifimgr
