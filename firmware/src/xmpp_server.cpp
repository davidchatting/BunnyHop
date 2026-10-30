#include "xmpp_server.h"
#include <AsyncTCP.h>
#include <mbedtls/base64.h>
#include <mbedtls/md5.h>
#include <string>
#include "config.h"
#include "eventlog.h"
#include "rabbit.h"
#include "store.h"

namespace xmpp {

static AsyncServer *server;
static AsyncClient *cli = nullptr;
static SemaphoreHandle_t mtx;
static std::string inbox;    // raw bytes received (filled by the TCP task)
static std::string pending;  // unparsed stanza text (loop task only)
static bool closedFlag = false;
static bool newConnFlag = false;  // a new connection replaced the session

// session state (loop task only)
static int authStep = 0;
static bool authed = false;
static String user, res;
static uint32_t msgNb = 0;
static uint32_t lastRx = 0;

static const char *STREAM_HDR =
    "<?xml version='1.0'?><stream:stream xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams' "
    "id='%u' from='" XMPP_DOMAIN "' version='1.0' xml:lang='en'>";

// ---------------- helpers ----------------

static void write(const String &s) {
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    if (cli && cli->connected()) {
        cli->add(s.c_str(), s.length(), ASYNC_WRITE_FLAG_COPY);
        cli->send();
    }
    xSemaphoreGiveRecursive(mtx);
    LOGI("xmpp>", "%s", s.substring(0, 300).c_str());
}

static std::string md5raw(const std::string &in) {
    uint8_t out[16];
    mbedtls_md5((const unsigned char *)in.data(), in.size(), out);
    return std::string((char *)out, 16);
}

static std::string toHex(const std::string &in) {
    static const char *H = "0123456789abcdef";
    std::string s;
    for (unsigned char c : in) {
        s += H[c >> 4];
        s += H[c & 15];
    }
    return s;
}

static String b64enc(const std::string &in) {
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, (const unsigned char *)in.data(), in.size());
    std::string out(olen, '\0');
    mbedtls_base64_encode((unsigned char *)&out[0], olen, &olen, (const unsigned char *)in.data(), in.size());
    out.resize(olen);
    return String(out.c_str());
}

static std::string b64dec(const String &in) {
    size_t olen = 0;
    mbedtls_base64_decode(nullptr, 0, &olen, (const unsigned char *)in.c_str(), in.length());
    std::string out(olen, '\0');
    if (mbedtls_base64_decode((unsigned char *)&out[0], olen, &olen, (const unsigned char *)in.c_str(), in.length()) != 0)
        return "";
    out.resize(olen);
    return out;
}

// From OpenJabNab plugin_auth.cpp. cnonce carries the rabbit's trailing \0.
static std::string digestResponse(const std::string &u, const std::string &pw, const std::string &nonce,
                                  const std::string &cnonce, const std::string &nc, const std::string &uri,
                                  const std::string &mode) {
    std::string ha1 = toHex(md5raw(md5raw(u + "::" + pw) + ":" + nonce + ":" + cnonce));
    std::string ha2 = toHex(md5raw(mode + ":" + uri));
    return toHex(md5raw(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":auth:" + ha2));
}

static String hexXor(const String &a, const String &b) {
    String out;
    char tmp[3];
    for (size_t i = 0; i + 1 < a.length() && i + 1 < b.length(); i += 2) {
        uint8_t x = strtol(a.substring(i, i + 2).c_str(), nullptr, 16) ^ strtol(b.substring(i, i + 2).c_str(), nullptr, 16);
        snprintf(tmp, sizeof(tmp), "%02x", x);
        out += tmp;
    }
    return out;
}

static String between(const String &s, const String &a, const String &b, int from = 0) {
    int i = s.indexOf(a, from);
    if (i < 0) return "";
    i += a.length();
    int j = s.indexOf(b, i);
    if (j < 0) return "";
    return s.substring(i, j);
}

// value of key="..." or key=... (comma-terminated) in a SASL digest string
static std::string saslField(const std::string &s, const std::string &key) {
    size_t p = 0;
    while ((p = s.find(key + "=", p)) != std::string::npos) {
        if (p == 0 || s[p - 1] == ',') break;
        p++;
    }
    if (p == std::string::npos) return "";
    p += key.size() + 1;
    if (p < s.size() && s[p] == '"') {
        size_t e = s.find('"', p + 1);
        return s.substr(p + 1, e == std::string::npos ? std::string::npos : e - p - 1);
    }
    size_t e = s.find(',', p);
    return s.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

struct Iq {
    bool valid = false;
    String type, id, from, to, content;
};

static Iq parseIq(const String &d) {
    Iq iq;
    if (!d.startsWith("<iq")) return iq;
    int gt = d.indexOf('>');
    if (gt < 0) return iq;
    bool selfClosing = d[gt - 1] == '/';
    String attrs = d.substring(3, selfClosing ? gt - 1 : gt);
    // key='value' or key="value"
    int i = 0;
    while (i < (int)attrs.length()) {
        int eq = attrs.indexOf('=', i);
        if (eq < 0 || eq + 1 >= (int)attrs.length()) break;
        String key = attrs.substring(i, eq);
        key.trim();
        char q = attrs[eq + 1];
        int end = attrs.indexOf(q, eq + 2);
        if (end < 0) break;
        String val = attrs.substring(eq + 2, end);
        if (key == "type") iq.type = val;
        else if (key == "id") iq.id = val;
        else if (key == "from") iq.from = val;
        else if (key == "to") iq.to = val;
        i = end + 1;
    }
    if (!selfClosing) {
        int close = d.lastIndexOf("</iq>");
        iq.content = d.substring(gt + 1, close < 0 ? d.length() : close);
    }
    iq.valid = true;
    return iq;
}

// OpenJabNab IQ::Reply - fmt uses %1 id, %2 from, %3 to, %4 type (from/to swapped)
static String iqReply(const Iq &iq, const char *type, String fmt, const String &content, bool hasContent = true) {
    fmt.replace("%1", "id='" + iq.id + "'");
    fmt.replace("%2", "from='" + iq.to + "'");
    fmt.replace("%3", "to='" + iq.from + "'");
    fmt.replace("%4", String("type='") + type + "'");
    String r = "<iq " + fmt;
    if (!hasContent) return r + "/>";
    return r + ">" + content + "</iq>";
}

static void resetSession() {
    authStep = 0;
    authed = false;
    res = "";
    pending.clear();
}

// ---------------- stanza handling ----------------

static void handleAuth(const String &d) {
    switch (authStep) {
        case 0:
            if (d.startsWith("<?xml")) {
                char hdr[256];
                snprintf(hdr, sizeof(hdr), STREAM_HDR, (unsigned)esp_random());
                write(String(hdr) +
                      "<stream:features><mechanisms xmlns='urn:ietf:params:xml:ns:xmpp-sasl'><mechanism>DIGEST-MD5</mechanism>"
                      "<mechanism>PLAIN</mechanism></mechanisms><register xmlns='http://violet.net/features/violet-register'/>"
                      "</stream:features>");
                authStep = 1;
                rabbit::setPhase("xmpp-auth");
            } else {
                LOGI("xmpp", "unexpected before stream open: %s", d.c_str());
            }
            return;
        case 1: {
            Iq iq = parseIq(d);
            if (iq.valid && iq.type == "get" && iq.content.indexOf("violet:iq:register") >= 0) {
                write(iqReply(iq, "result", "from='" XMPP_DOMAIN "' %1 %4",
                              "<query xmlns='violet:iq:register'><instructions>Choose a username and password to register "
                              "with this server</instructions><username/><password/></query>"));
                authStep = 100;
                return;
            }
            if (d.startsWith("<auth") && d.indexOf("DIGEST-MD5") >= 0) {
                String challenge = "nonce=\"" + String(esp_random()) + "\",qop=\"auth\",charset=utf-8,algorithm=md5-sess";
                write("<challenge xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>" + b64enc(challenge.c_str()) + "</challenge>");
                authStep = 2;
                return;
            }
            LOGI("xmpp", "unexpected in auth step 1: %s", d.c_str());
            return;
        }
        case 100: {
            Iq iq = parseIq(d);
            String u = between(iq.content, "<username>", "</username>");
            String p = between(iq.content, "<password>", "</password>");
            if (iq.valid && iq.type == "set" && u.length() && p.length()) {
                String pw = hexXor(u, p);
                store::set(K_XMPP_PW, u + ":" + pw);
                LOGI("xmpp", "rabbit %s registered (password stored)", u.c_str());
                write(iqReply(iq, "result", "%1 %2 %3 %4", iq.content));
                authStep = 1;
            } else {
                LOGI("xmpp", "bad register: %s", d.c_str());
            }
            return;
        }
        case 2: {
            String b64 = between(d, ">", "</response>");
            std::string a = b64dec(b64);
            std::string clean;
            for (char c : a)
                if (c) clean += c;
            std::string u = saslField(clean, "username");
            std::string nonce = saslField(clean, "nonce");
            std::string cnonce = saslField(clean, "cnonce") + std::string(1, '\0');
            std::string nc = saslField(clean, "nc");
            std::string uri = saslField(clean, "digest-uri");
            std::string resp = saslField(clean, "response");
            user = u.c_str();
            String saved = store::get(K_XMPP_PW);
            bool verified = false;
            if (saved.startsWith(user + ":")) {
                std::string pw = saved.substring(user.length() + 1).c_str();
                if (digestResponse(u, pw, nonce, cnonce, nc, uri, "AUTHENTICATE") == resp) {
                    std::string rsp = "rspauth=" + digestResponse(u, pw, nonce, cnonce, nc, uri, "");
                    write("<challenge xmlns='urn:ietf:params:xml:ns:xmpp-sasl'>" + b64enc(rsp) + "</challenge>");
                    authStep = 3;
                    verified = true;
                    LOGI("xmpp", "digest auth verified for %s", user.c_str());
                }
            }
            if (!verified) {
                // Same as OpenJabNab's StandAloneAuthBypass: we are the only
                // server this rabbit talks to, so accept it.
                LOGI("xmpp", "no matching password for %s - accepting (auth bypass)", user.c_str());
                write("<success xmlns='urn:ietf:params:xml:ns:xmpp-sasl'/>");
                authStep = 4;
            }
            return;
        }
        case 3:
            if (d.startsWith("<response")) {
                write("<success xmlns='urn:ietf:params:xml:ns:xmpp-sasl'/>");
                authStep = 4;
            }
            return;
        case 4:
            if (d.startsWith("<?xml")) {
                char hdr[256];
                snprintf(hdr, sizeof(hdr), STREAM_HDR, (unsigned)esp_random());
                write(String(hdr) +
                      "<stream:features><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'><required/></bind>"
                      "<unbind xmlns='urn:ietf:params:xml:ns:xmpp-bind'/><session xmlns='urn:ietf:params:xml:ns:xmpp-session'/>"
                      "</stream:features>");
                authed = true;
                rabbit::state().sn = user;
                if (store::get(K_RABBIT_SN) != user) store::set(K_RABBIT_SN, user);
                LOGI("xmpp", "rabbit %s authenticated", user.c_str());
            }
            return;
    }
}

static packets::Bytes initPacket() {
    // Like OpenJabNab Bunny::GetInitPacket: nose off, wake up. Ears are only
    // included if a client has set them, so a reboot doesn't flatten them.
    auto &s = rabbit::state();
    std::vector<std::pair<uint8_t, uint8_t>> svc = {{packets::SVC_NOSE, 0}};
    if (s.cmdEarLeft >= 0) svc.push_back({packets::SVC_RABBIT_LEFT_EAR, (uint8_t)s.cmdEarLeft});
    if (s.cmdEarRight >= 0) svc.push_back({packets::SVC_RABBIT_RIGHT_EAR, (uint8_t)s.cmdEarRight});
    return packets::trame({packets::ambient(svc), packets::sleep(false)});
}

static void handleStanza(const String &d) {
    if (!authed) {
        handleAuth(d);
        return;
    }
    if (d.startsWith("<iq")) {
        Iq iq = parseIq(d);
        if (!iq.valid) {
            LOGI("xmpp", "invalid iq: %s", d.c_str());
            return;
        }
        if (iq.content.indexOf("<bind") >= 0) {
            res = between(iq.content, "<resource>", "</resource>");
            rabbit::state().resource = res;
            String jid = user + "@" XMPP_DOMAIN "/" + res;
            write(iqReply(iq, "result", "%1 %4", "<bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'><jid>" + jid + "</jid></bind>"));
            LOGI("xmpp", "bound resource '%s'", res.c_str());
            if (res == "boot") rabbit::setPhase("boot");
            else {
                rabbit::setPhase("ready");
                rabbit::onXmppConnected(true);
            }
            rabbit::emit("resource", [&](JsonObject o) { o["resource"] = res; });
        } else if (iq.content.indexOf("xmpp-session") >= 0) {
            write(iqReply(iq, "result", "%4 %3 %2 %1", "<session xmlns='urn:ietf:params:xml:ns:xmpp-session'/>"));
        } else if (iq.content.indexOf("violet:iq:sources") >= 0) {
            write(iqReply(iq, "result", "%2 %3 %1 %4",
                          "<query xmlns='violet:iq:sources'><packet xmlns='violet:packet' format='1.0' ttl='604800'>" +
                              packets::base64(initPacket()) + "</packet></query>"));
        } else if (iq.content.indexOf("<unbind") >= 0) {
            String r = between(iq.content, "<resource>", "</resource>");
            write(iqReply(iq, "result", "%1 %4", "", false));
            LOGI("xmpp", "unbind '%s'%s", r.c_str(), r == "boot" ? " - boot finished" : "");
        } else if (iq.content.indexOf("jabber:iq:version") >= 0) {
            LOGI("xmpp", "version: %s", iq.content.c_str());
        } else if (iq.content.indexOf("http://jabber.org/protocol/commands") >= 0) {
            // reply to queryRabbit(): <field var='k'><value>v</value></field>...
            String node = between(iq.content, "node='", "'");
            rabbit::emit("diagnostics", [&](JsonObject o) {
                o["node"] = node;
                JsonObject f = o["fields"].to<JsonObject>();
                int i = 0;
                while ((i = iq.content.indexOf("<field var='", i)) >= 0) {
                    String k = between(iq.content, "<field var='", "'", i);
                    String v = between(iq.content, "<value>", "</value>", i);
                    f[k] = v;
                    LOGI("diag", "%s = %s", k.c_str(), v.c_str());
                    i += 12;
                }
            });
        } else if (iq.type == "result" || iq.type == "error") {
            // replies to our own requests - nothing to do
        } else {
            LOGI("xmpp", "unhandled iq: %s", d.c_str());
        }
        return;
    }
    if (d.startsWith("<presence")) {
        // OpenJabNab echoes <presence from='X' id='Y'> back as from=to=X
        int fi = d.indexOf("from=");
        int ii = d.indexOf("id=");
        if (fi >= 0 && ii >= 0) {
            char q = d[fi + 5];
            String from = d.substring(fi + 6, d.indexOf(q, fi + 6));
            char q2 = d[ii + 3];
            String id = d.substring(ii + 4, d.indexOf(q2, ii + 4));
            write("<presence from='" + from + "' to='" + from + "' id='" + id + "'/>");
        }
        return;
    }
    if (d.startsWith("<message")) {
        if (d.indexOf("<button") >= 0) {
            int clicks = between(d, "<clic>", "</clic>").toInt();
            String ev = between(d, "<event>", "</event>");
            rabbit::onButton(clicks, ev.length() ? ev.toInt() : -1);
        } else if (d.indexOf("<ears") >= 0) {
            // Violet's <left> is the rabbit's right ear (camera-verified)
            rabbit::onEars(between(d, "<right>", "</right>").toInt(), between(d, "<left>", "</left>").toInt());
        } else {
            LOGI("xmpp", "unhandled message: %s", d.c_str());
            rabbit::emit("xmpp-message", [&](JsonObject o) { o["xml"] = d; });
        }
        return;
    }
    if (d.startsWith("</stream:stream")) {
        LOGI("xmpp", "rabbit closed stream");
        disconnect();
        return;
    }
    LOGI("xmpp", "unhandled: %s", d.c_str());
}

// Split the receive buffer into complete top-level stanzas
// (same rules as OpenJabNab XmppHandler::XmlParse).
static bool nextStanza(String &out) {
    size_t s = pending.find_first_not_of(" \t\r\n");
    if (s == std::string::npos) {
        pending.clear();
        return false;
    }
    if (s) pending.erase(0, s);
    if (pending[0] != '<') {
        // garbage - drop up to next tag
        size_t n = pending.find('<');
        pending.erase(0, n);
        return false;
    }
    size_t end = std::string::npos;
    if (pending.compare(0, 5, "<?xml") == 0) {
        size_t st = pending.find("<stream:stream");
        if (st != std::string::npos) {
            size_t gt = pending.find('>', st);
            if (gt != std::string::npos) end = gt + 1;
        }
    } else if (pending.compare(0, 2, "</") == 0) {
        size_t gt = pending.find('>');
        if (gt != std::string::npos) end = gt + 1;
    } else {
        size_t gt = pending.find('>');
        if (gt == std::string::npos) return false;
        if (pending[gt - 1] == '/') end = gt + 1;
        else {
            size_t nameEnd = pending.find_first_of(" />", 1);
            std::string closing = "</" + pending.substr(1, nameEnd - 1) + ">";
            size_t c = pending.find(closing, gt);
            if (c != std::string::npos) end = c + closing.size();
        }
    }
    if (end == std::string::npos) return false;
    out = String(pending.substr(0, end).c_str());
    pending.erase(0, end);
    return true;
}

// ---------------- TCP plumbing ----------------

static void onClient(void *, AsyncClient *c) {
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    if (cli) {
        // only one rabbit: drop the old connection
        cli->close(true);
    }
    cli = c;
    inbox.clear();
    closedFlag = false;
    newConnFlag = true;
    xSemaphoreGiveRecursive(mtx);
    LOGI("xmpp", "connection from %s", c->remoteIP().toString().c_str());
    rabbit::touch(c->remoteIP().toString());

    c->onData(
        [](void *, AsyncClient *c, void *data, size_t len) {
            xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
            if (c == cli) inbox.append((const char *)data, len);
            xSemaphoreGiveRecursive(mtx);
        },
        nullptr);
    c->onDisconnect(
        [](void *, AsyncClient *c) {
            xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
            bool current = c == cli;
            if (current) {
                cli = nullptr;
                closedFlag = true;
            }
            xSemaphoreGiveRecursive(mtx);
            delete c;
        },
        nullptr);
    c->setRxTimeout(0);
    c->setNoDelay(true);
}

void begin() {
    mtx = xSemaphoreCreateRecursiveMutex();
    server = new AsyncServer(XMPP_PORT);
    server->onClient(onClient, nullptr);
    server->begin();
    LOGI("xmpp", "listening on :%d", XMPP_PORT);
}

void loop() {
    std::string got;
    bool closed;
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    got.swap(inbox);
    closed = closedFlag;
    closedFlag = false;
    bool fresh = newConnFlag;
    newConnFlag = false;
    xSemaphoreGiveRecursive(mtx);

    if (fresh) {
        bool was = authed;
        resetSession();
        if (was) rabbit::onXmppConnected(false);
    }

    if (!got.empty()) {
        lastRx = millis();
        rabbit::touch();
        if (got.find_first_not_of(" ") == std::string::npos) {
            // whitespace keepalive from the rabbit
        } else {
            LOGI("xmpp<", "%s", got.substr(0, 300).c_str());
            pending += got;
            String st;
            while (nextStanza(st)) handleStanza(st);
        }
    }
    if (closed) {
        bool was = authed;
        LOGI("xmpp", "rabbit disconnected");
        resetSession();
        rabbit::state().resource = "";
        if (was) rabbit::onXmppConnected(false);
    }
}

bool connected() {
    bool c;
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    c = cli != nullptr;
    xSemaphoreGiveRecursive(mtx);
    return c && authed && res.length();
}

String resource() { return res; }

bool sendPacket(const packets::Bytes &t) {
    if (!connected()) return false;
    // Addressed to the "urgent" resource: the bootcode evaluates those
    // immediately (_isResourceValid), whereas packets for "idle" are queued
    // while a program or choreography is running.
    String msg = "<message from='net.openjabnab.platform@" XMPP_DOMAIN "/services' to='" + user + "@" XMPP_DOMAIN "/urgent" +
                 "' id='OJaNa-" + String(msgNb++) + "'><packet xmlns='violet:packet' format='1.0' ttl='604800'>" +
                 packets::base64(t) + "</packet></message>";
    write(msg);
    return true;
}

bool sendRaw(const String &xml) {
    if (!connected()) return false;
    write(xml);
    return true;
}

bool queryRabbit(const char *node) {
    return sendRaw("<iq from='net.openjabnab.platform@" XMPP_DOMAIN "/services' to='" + user + "@" XMPP_DOMAIN "/" + res +
                   "' type='set' id='diag-" + String(msgNb++) + "'><command xmlns='http://jabber.org/protocol/commands' node='" +
                   node + "' action='execute'/></iq>");
}

void disconnect() {
    xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    if (cli) cli->close(true);
    xSemaphoreGiveRecursive(mtx);
}

}  // namespace xmpp
