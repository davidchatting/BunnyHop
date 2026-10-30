// SPDX-License-Identifier: GPL-2.0-or-later
#include "adpcm.h"
#include <vector>

static const int16_t STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t INDEX_ADJ[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static uint16_t u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static void le16(uint8_t *p, uint16_t x) { p[0] = x; p[1] = x >> 8; }
static void le32(uint8_t *p, uint32_t x) { le16(p, x); le16(p + 2, x >> 16); }

// Streams block by block (recordings can be long; the heap is small).
bool adpcmToPcmWav(fs::FS &fs, const char *src, const char *dst) {
    File in = fs.open(src, "r");
    if (!in) return false;
    uint8_t h[12];
    if (in.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;

    uint16_t format = 0, channels = 1, blockAlign = 256;
    uint32_t rate = 8000, dataLen = 0;
    bool haveData = false;
    uint8_t ch[8];
    while (in.read(ch, 8) == 8) {
        uint32_t len = u32(ch + 4);
        if (!memcmp(ch, "fmt ", 4) && len >= 16) {
            uint8_t f[16];
            in.read(f, 16);
            format = u16(f);
            channels = u16(f + 2);
            rate = u32(f + 4);
            blockAlign = u16(f + 12);
            in.seek(in.position() + len - 16 + (len & 1));
        } else if (!memcmp(ch, "data", 4)) {
            dataLen = std::min<uint32_t>(len, in.size() - in.position());
            haveData = true;
            break;
        } else {
            in.seek(in.position() + len + (len & 1));
        }
    }
    if (format != 0x11 || channels != 1 || !haveData || blockAlign < 5 || blockAlign > 2048) return false;

    File out = fs.open(dst, "w");
    if (!out) return false;
    uint8_t hdr[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0};
    le32(hdr + 24, rate);
    le32(hdr + 28, rate * 2);
    le16(hdr + 32, 2);
    le16(hdr + 34, 16);
    memcpy(hdr + 36, "data", 4);
    out.write(hdr, 44);

    std::vector<uint8_t> blk(blockAlign);
    std::vector<uint8_t> pcm((blockAlign - 4) * 4 + 2);
    uint32_t pcmBytes = 0;
    for (uint32_t b = 0; b + 4 <= dataLen; b += blockAlign) {
        size_t n = in.read(blk.data(), std::min<uint32_t>(blockAlign, dataLen - b));
        if (n < 4) break;
        int pred = (int16_t)u16(blk.data());
        int idx = std::min<int>(blk[2], 88);
        size_t o = 0;
        le16(&pcm[o], (uint16_t)pred);
        o += 2;
        for (size_t i = 4; i < n; i++) {
            for (int nib = 0; nib < 2; nib++) {
                int code = nib ? blk[i] >> 4 : blk[i] & 0x0F;  // low nibble first
                int step = STEPS[idx];
                int diff = step >> 3;
                if (code & 1) diff += step >> 2;
                if (code & 2) diff += step >> 1;
                if (code & 4) diff += step;
                pred += (code & 8) ? -diff : diff;
                pred = constrain(pred, -32768, 32767);
                idx = constrain(idx + INDEX_ADJ[code], 0, 88);
                le16(&pcm[o], (uint16_t)pred);
                o += 2;
            }
        }
        out.write(pcm.data(), o);
        pcmBytes += o;
    }
    in.close();
    le32(hdr + 4, 36 + pcmBytes);
    le32(hdr + 40, pcmBytes);
    out.seek(0);
    out.write(hdr, 44);
    out.close();
    return true;
}
