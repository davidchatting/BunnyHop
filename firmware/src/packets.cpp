#include "packets.h"

namespace packets {

Bytes trame(const std::vector<Block> &blocks) {
    Bytes out;
    out.push_back(0x7F);
    for (const auto &b : blocks) {
        out.push_back(b.type);
        size_t n = b.data.size();
        out.push_back((n >> 16) & 0xFF);
        out.push_back((n >> 8) & 0xFF);
        out.push_back(n & 0xFF);
        out.insert(out.end(), b.data.begin(), b.data.end());
    }
    out.push_back(0xFF);
    return out;
}

Block ambient(const std::vector<std::pair<uint8_t, uint8_t>> &services) {
    // 7FFFFFFE header selects the bootcode's "info_update_v2" format
    Block b{0x04, {0x7F, 0xFF, 0xFF, 0xFE}};
    for (auto &s : services) {
        b.data.push_back(s.first);
        b.data.push_back(s.second);
    }
    return b;
}

// Message obfuscation by "Sache", from OpenJabNab messagepacket.cpp
static const uint8_t INVERSION[128] = {
    1, 171, 205, 183, 57, 163, 197, 239, 241, 27, 61, 167, 41, 19, 53, 223, 225, 139, 173, 151, 25, 131,
    165, 207, 209, 251, 29, 135, 9, 243, 21, 191, 193, 107, 141, 119, 249, 99, 133, 175, 177, 219, 253, 103,
    233, 211, 245, 159, 161, 75, 109, 87, 217, 67, 101, 143, 145, 187, 221, 71, 201, 179, 213, 127, 129, 43,
    77, 55, 185, 35, 69, 111, 113, 155, 189, 39, 169, 147, 181, 95, 97, 11, 45, 23, 153, 3, 37, 79,
    81, 123, 157, 7, 137, 115, 149, 63, 65, 235, 13, 247, 121, 227, 5, 47, 49, 91, 125, 231, 105, 83,
    117, 31, 33, 203, 237, 215, 89, 195, 229, 15, 17, 59, 93, 199, 73, 51, 85, 255};

Block message(const String &program) {
    Block b{0x0A, {0x00}};
    uint8_t prev = 35;
    for (size_t i = 0; i < program.length(); i++) {
        uint8_t c = (uint8_t)program[i];
        b.data.push_back((uint8_t)(INVERSION[prev % 128] * c + 47));
        prev = c;
    }
    return b;
}

Block sleep(bool s) { return Block{0x0B, {(uint8_t)(s ? 1 : 0)}}; }

Chor::Chor(uint8_t tempo10ms) {
    body = {0x00, 0x01, tempo10ms ? tempo10ms : (uint8_t)1};
}

Chor &Chor::led(uint8_t delay, uint8_t led, uint8_t r, uint8_t g, uint8_t b) {
    body.insert(body.end(), {delay, 0x07, led, r, g, b, 0x00, 0x00});
    return *this;
}

Chor &Chor::ledOff(uint8_t delay, uint8_t led) {
    body.insert(body.end(), {delay, 0x07, led, 0, 0, 0, 0x00, 0x00});
    return *this;
}

Chor &Chor::motor(uint8_t delay, uint8_t ear, uint8_t pos, uint8_t dir) {
    body.insert(body.end(), {delay, 0x08, ear, pos, dir});
    return *this;
}

Bytes Chor::build() const {
    Bytes out;
    size_t n = body.size();
    out.push_back((n >> 24) & 0xFF);
    out.push_back((n >> 16) & 0xFF);
    out.push_back((n >> 8) & 0xFF);
    out.push_back(n & 0xFF);
    out.insert(out.end(), body.begin(), body.end());
    out.insert(out.end(), {0, 0, 0, 0});
    return out;
}

String base64(const Bytes &b) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String s;
    s.reserve((b.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        uint32_t v = (b[i] << 16) | (b[i + 1] << 8) | b[i + 2];
        s += T[(v >> 18) & 63];
        s += T[(v >> 12) & 63];
        s += T[(v >> 6) & 63];
        s += T[v & 63];
    }
    if (i < b.size()) {
        uint32_t v = b[i] << 16;
        if (i + 1 < b.size()) v |= b[i + 1] << 8;
        s += T[(v >> 18) & 63];
        s += T[(v >> 12) & 63];
        s += (i + 1 < b.size()) ? T[(v >> 6) & 63] : '=';
        s += '=';
    }
    return s;
}

String hex(const Bytes &b) {
    String s;
    char tmp[3];
    for (uint8_t c : b) {
        snprintf(tmp, sizeof(tmp), "%02x", c);
        s += tmp;
    }
    return s;
}

bool fromHex(const String &in, Bytes &out) {
    String s;
    for (char c : in)
        if (isxdigit((unsigned char)c)) s += c;
    if (s.length() % 2) return false;
    out.clear();
    for (size_t i = 0; i < s.length(); i += 2) out.push_back((uint8_t)strtol(s.substring(i, i + 2).c_str(), nullptr, 16));
    return true;
}

}  // namespace packets
