// SPDX-License-Identifier: GPL-2.0-or-later
#include "wifi_manager.h"
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>
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
static uint32_t lastKick = 0;

// ---- home network (STA) and rabbit-only lock ----
static String lanSsid, lanPass;
static volatile bool lanUp = false;
static bool lanAttempting = false;
static uint32_t lanAttemptAt = 0;
static bool lanRequested = false;  // (re)connect asked for from another task
static bool inSetup = false;       // STA is busy with the rabbit's setup network
static String lanIp;
static String lanState = "unset";  // unset | connecting | connected | unavailable
static volatile bool apLocked = false;
static bool isPaired = false;      // cached: store has K_AP_SSID
static uint32_t lockHoldUntil = 0; // keep the network private while BunnyHop is briefly off home Wi-Fi
static uint8_t rabbitMac[6];
static bool haveRabbitMac = false;
static bool scanRequested = false;
static bool mdnsStarted = false;
static uint32_t lanPausedUntil = 0;  // debug: simulate a home Wi-Fi outage

static bool parseMac(const String &s, uint8_t out[6]) {
    return sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

// The rabbit's MAC: its setup AP's BSSID is the same address (verified).
static bool isRabbitMac(const uint8_t *m) {
    if (haveRabbitMac) return memcmp(m, rabbitMac, 6) == 0;
    return m[0] == 0x00 && m[1] == 0x13 && m[2] == 0xd3;  // not paired yet: any Violet rabbit
}

static void deauthOthers() {
    wifi_sta_list_t list;
    if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return;
    for (int i = 0; i < list.num; i++) {
        const uint8_t *m = list.sta[i].mac;
        if (isRabbitMac(m)) continue;
        uint16_t aid;
        if (esp_wifi_ap_get_sta_aid(m, &aid) == ESP_OK) {
            LOGI("wifi", "disconnecting %02x:%02x:%02x:%02x:%02x:%02x (network is now rabbit-only)", m[0], m[1], m[2], m[3], m[4], m[5]);
            esp_wifi_deauth_sta(aid);
        }
    }
}

static bool apHidden = false;

// hidden: don't broadcast the SSID (private mode, while home Wi-Fi is up)
static void startAp(const String &ssid, bool hidden = apHidden) {
    ap = ssid;
    apHidden = hidden;
    IPAddress ip(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
    // Always open (no password) - the rabbit needs that. Private = hidden +
    // locked to the rabbit's MAC; see lanLoop().
    WiFi.softAP(ap.c_str(), nullptr, AP_CHANNEL, hidden ? 1 : 0, AP_MAX_CLIENTS);
    // The rabbit's radio only decodes 802.11b (DSSS/CCK). With g/n enabled
    // the ESP32 sends it OFDM frames it can't receive, so the AP is b-only.
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B);
    esp_wifi_set_event_mask(WIFI_EVENT_MASK_NONE);  // enables AP probe-request events
    LOGI("wifi", "AP '%s' up at %s%s", ap.c_str(), WiFi.softAPIP().toString().c_str(), hidden ? " (hidden)" : "");
}

// Our AP SSID: the prefix + the last 3 bytes of the ESP32's own AP MAC, so it
// is fixed from the first boot and doesn't change when a rabbit pairs.
static String ownSsid() {
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_SOFTAP);
    char s[7];
    snprintf(s, sizeof s, "%02X%02X%02X", m[3], m[4], m[5]);
    return String(AP_SSID_PREFIX) + s;
}

// AP-side diagnostics: who associates, who gets an address, and whether
// the rabbit (Violet OUI 00:13:d3) is probing for our SSID at all.
static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t info) {
    char mac[18];
    switch (ev) {
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
            const uint8_t *m = info.wifi_ap_staconnected.mac;
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
            bool isRabbit = isRabbitMac(m);
            if (isRabbit) {
                rabbitAssociated = true;
                rabbitAssociatedAt = millis();
            } else if (apLocked) {
                // home Wi-Fi is up: the rabbit's network is private to the rabbit
                LOGI("wifi", "station %s refused (network is locked to the rabbit)", mac);
                esp_wifi_deauth_sta(info.wifi_ap_staconnected.aid);
                break;
            }
            LOGI("wifi", "station %s associated%s", mac, isRabbit ? " (rabbit)" : "");
            break;
        }
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
            const uint8_t *m = info.wifi_ap_stadisconnected.mac;
            snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
            if (isRabbitMac(m)) rabbitAssociated = false;
            LOGI("wifi", "station %s left", mac);
            break;
        }
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            if (!inSetup) {
                lanUp = true;
                lanIp = IPAddress(info.got_ip.ip_info.ip.addr).toString();
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (!inSetup) lanUp = false;
            break;
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
    isPaired = saved.length() > 0;
    startAp(ownSsid());
    if (!saved.length()) {
        LOGI("wifi", "not provisioned yet - will scan for a rabbit in setup mode%s", AUTO_PAIR ? "" : " (auto-pair disabled)");
        pairRequested = AUTO_PAIR;
    } else {
        LOGI("wifi", "provisioned for rabbit %s; waiting for it to join", store::get(K_RABBIT_SN, "?").c_str());
    }
    lastRabbitContact = millis();

    haveRabbitMac = parseMac(store::get(K_SETUP_BSSID), rabbitMac);
    lanSsid = store::get(K_LAN_SSID);
    lanPass = store::get(K_LAN_PASS);
    WiFi.setAutoReconnect(false);  // reconnects are paced in loop() to spare the rabbit's AP
    if (lanSsid.length()) lanRequested = true;
    else LOGI("wifi", "no home Wi-Fi set - '%s' is open with a captive portal", ap.c_str());
}

String apSsid() { return ap; }
bool paired() { return isPaired; }

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
    inSetup = true;
    lanUp = false;
    WiFi.disconnect(false, false);
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
    inSetup = false;
    lanRequested = lanSsid.length() > 0;  // back to home Wi-Fi
    // Pairing only took BunnyHop off home Wi-Fi for a moment: stay private
    // while it rejoins rather than flipping the network to public and back.
    if (lanRequested && apLocked) lockHoldUntil = millis() + LAN_CONNECT_TIMEOUT_MS + 10000UL;
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
    String newAp = ownSsid();
    String text = textOnly(page);
    String serial = after(text, "Serial number:");
    String firmware = after(text, "Firmware:");
    String info = "serial " + serial + ", loader firmware " + firmware + ", setup SSID " + ssid;
    store::set(K_SETUP_INFO, info);
    store::set(K_SETUP_BSSID, bssid);
    store::set(K_SETUP_SSID, ssid);
    haveRabbitMac = parseMac(bssid, rabbitMac);
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
    isPaired = true;
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
    WiFi.mode(WIFI_STA);  // keeps the home Wi-Fi connection
    delay(offMs);
    WiFi.mode(WIFI_AP_STA);
    startAp(ap);
}

void forget() {
    store::remove(K_AP_SSID);
    isPaired = false;
    store::remove(K_SETUP_BSSID);
    store::remove(K_SETUP_SSID);
    store::remove(K_RABBIT_SN);
    store::remove(K_XMPP_PW);
    LOGI("wifi", "provisioning forgotten");
}

// ---------------- home Wi-Fi ----------------

void setLan(const String &ssid, const String &pass) {
    lanSsid = ssid;
    lanPass = pass;
    if (ssid.length()) {
        store::set(K_LAN_SSID, ssid);
        store::set(K_LAN_PASS, pass);
    } else {
        store::remove(K_LAN_SSID);
        store::remove(K_LAN_PASS);
    }
    lanRequested = true;
}

void requestLanScan() { scanRequested = true; }

void pauseLan(uint32_t ms) {
    lanPausedUntil = millis() + ms;
    lanAttempting = false;
    WiFi.disconnect(false, false);
    lanUp = false;
    LOGI("wifi", "home Wi-Fi paused for %lus (simulated outage)", (unsigned long)(ms / 1000));
}

// ---- router check: home Wi-Fi only counts once its router answers ----
static volatile bool routerOk = false;
static volatile bool pingBusy = false;
static volatile uint8_t pingFails = 0;
static uint32_t lastPingAt = 0;
static esp_ping_handle_t pingSession = nullptr;

static void onPingEnd(esp_ping_handle_t h, void *) {
    uint32_t replies = 0;
    esp_ping_get_profile(h, ESP_PING_PROF_REPLY, &replies, sizeof(replies));
    if (replies) {
        pingFails = 0;
        routerOk = true;
    } else if (++pingFails >= ROUTER_PING_FAILS) {
        routerOk = false;
    }
    pingBusy = false;
}

static void pingRouter() {
    if (pingSession) {
        esp_ping_delete_session(pingSession);
        pingSession = nullptr;
    }
    IPAddress gw = WiFi.gatewayIP();
    if (gw == IPAddress((uint32_t)0)) return;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    IP_ADDR4(&cfg.target_addr, gw[0], gw[1], gw[2], gw[3]);
    cfg.count = 3;
    cfg.interval_ms = 500;
    cfg.timeout_ms = 1000;
    esp_ping_callbacks_t cb = {};
    cb.on_ping_end = onPingEnd;
    if (esp_ping_new_session(&cfg, &cb, &pingSession) == ESP_OK) {
        pingBusy = true;
        esp_ping_start(pingSession);
    }
}

static void lanStart() {
    WiFi.disconnect(false, false);
    lanUp = false;
    if (lanSsid.isEmpty()) {
        lanState = "unset";
        lanAttempting = false;
        return;
    }
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP (the setup join used a static address)
    WiFi.setHostname(MDNS_NAME);
    WiFi.begin(lanSsid.c_str(), lanPass.c_str());
    lanAttempting = true;
    lanAttemptAt = millis();
    lanState = "connecting";
    LOGI("wifi", "joining home Wi-Fi '%s'", lanSsid.c_str());
}

static void lanLoop() {
    if (lanRequested && !inSetup) {
        lanRequested = false;
        lanStart();
    }
    // read the clock after lanStart(), or the timeout check below underflows
    uint32_t now = millis();
    if (lanAttempting && lanUp) {
        lanAttempting = false;
        lanState = "connected";
        LOGI("wifi", "home Wi-Fi up: %s - http://%s.local/ (channel %d)", lanIp.c_str(), MDNS_NAME, WiFi.channel());
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        rabbit::emit("lan", [](JsonObject o) { o["state"] = "connected"; o["ip"] = lanIp; });
    } else if (lanAttempting && now - lanAttemptAt > LAN_CONNECT_TIMEOUT_MS) {
        // give up for now - an unanswered STA keeps scanning all channels,
        // which disturbs the rabbit's AP
        lanAttempting = false;
        WiFi.disconnect(false, false);
        lanState = "unavailable";
        LOGI("wifi", "home Wi-Fi '%s' not reachable - retrying in %lus", lanSsid.c_str(), (unsigned long)(LAN_RETRY_MS / 1000));
        rabbit::emit("lan", [](JsonObject o) { o["state"] = "unavailable"; });
    } else if (!lanAttempting && !lanUp && lanState == "connected") {
        lanState = "unavailable";
        LOGI("wifi", "home Wi-Fi lost");
        rabbit::emit("lan", [](JsonObject o) { o["state"] = "unavailable"; });
        lanAttemptAt = now - LAN_RETRY_MS + 5000;  // retry soon
    } else if (!lanAttempting && !lanUp && lanSsid.length() && !inSetup && now - lanAttemptAt > LAN_RETRY_MS &&
               (int32_t)(now - lanPausedUntil) >= 0) {
        lanStart();
    }

    // Is home Wi-Fi really working? Only while its router answers pings.
    if (!lanUp) {
        routerOk = false;
        pingFails = 0;
        lastPingAt = 0;
    } else if (!pingBusy && (lastPingAt == 0 || now - lastPingAt > (routerOk && !pingFails ? ROUTER_PING_MS : ROUTER_PING_RETRY_MS))) {
        lastPingAt = now;
        pingRouter();
    }
    static bool routerWas = false;
    if (routerOk != routerWas) {
        routerWas = routerOk;
        LOGI("wifi", "home router %s", routerOk ? "answers" : "doesn't answer");
    }

    // The rabbit's network is private (hidden, rabbit only) once a rabbit is
    // paired and home Wi-Fi works, and open and visible otherwise (captive
    // portal to the web app), so there is always a way in to reconfigure.
    bool wantLock = isPaired && routerOk;
    if (!wantLock && apLocked && (inSetup || (int32_t)(lockHoldUntil - now) > 0)) wantLock = true;  // see leaveSetup()
    if (wantLock != apLocked) {
        apLocked = wantLock;
        LOGI("wifi", "'%s' is now %s", ap.c_str(), wantLock ? "private: hidden, rabbit only" : "public: visible, captive portal");
        // Tell the web pages first (restarting the AP drops everyone on it),
        // so a page on this network can say where to go next.
        rabbit::emit("network", [wantLock](JsonObject o) {
            o["ap"] = ap;
            o["private"] = wantLock;
            o["lan"] = lanSsid;
        });
        delay(300);
        if (wantLock) deauthOthers();
        // the rabbit re-associates by name; if its session goes stale the
        // watchdog restarts it
        startAp(ap, wantLock);
    }

    if (!mdnsStarted && lanUp) {
        mdnsStarted = MDNS.begin(MDNS_NAME);
        if (mdnsStarted) {
            MDNS.addService("http", "tcp", 80);
            LOGI("wifi", "mDNS: %s.local", MDNS_NAME);
        }
    }

    if (scanRequested && !inSetup && !lanAttempting) {
        scanRequested = false;
        int n = WiFi.scanNetworks(false, false, false, 200);
        rabbit::emit("lanScan", [n](JsonObject o) {
            JsonArray a = o["networks"].to<JsonArray>();
            for (int i = 0; i < n; i++) {
                String ssid = WiFi.SSID(i);
                bool dup = false;
                for (JsonVariant v : a) dup |= v["ssid"] == ssid;
                if (ssid.isEmpty() || dup || ssid == ap || ssid.startsWith(RABBIT_SETUP_SSID_PREFIX)) continue;
                JsonObject n = a.add<JsonObject>();
                n["ssid"] = ssid;
                n["rssi"] = WiFi.RSSI(i);
                n["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
            }
        });
        WiFi.scanDelete();
    }
}

bool lanConnected() { return lanUp; }

bool fromRabbitNetwork(IPAddress local) { return local == IPAddress(AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D); }

void loop() {
    lanLoop();
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
    // No rabbit paired yet: look for one in setup mode every 30 s from boot.
    // Paired rabbit missing for a while: look every minute (it may have been reset).
    bool lookNow = !isPaired ? now - lastScan > UNPAIRED_SCAN_INTERVAL_MS
                             : now - lastRabbitContact > RABBIT_RESCAN_AFTER_MS && now - lastScan > RABBIT_RESCAN_INTERVAL_MS;
    if (AUTO_PAIR && lookNow) {
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
    JsonObject lan = o["lan"].to<JsonObject>();
    lan["ssid"] = lanSsid;
    lan["state"] = lanState;
    lan["ip"] = lanUp ? lanIp : "";
    lan["router"] = (bool)routerOk;  // answers pings
    lan["host"] = String(MDNS_NAME) + ".local";
    o["private"] = (bool)apLocked;  // hidden + rabbit-only (paired, home Wi-Fi working)
}

}  // namespace wifimgr
