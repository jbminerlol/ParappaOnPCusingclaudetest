// libpress: STR bitstream decoding (DecDCTvlc) and the MDEC macroblock decoder (DecDCTin/out).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "psx/log.h"
#include "psx/memory.h"
#include "psx/system.h"

namespace {

// ---------------------------------------------------------------- VLC (bitstream -> run/level codes)

struct Vlc {
    uint32_t bits;  // code without the sign bit
    int len;        // code length without the sign bit
    int run, level;
};

// MPEG-1 dct_coeff_next (excluding EOB "10" and escape "000001"); every code is followed by a sign bit.
const Vlc kAc[] = {
    {0b11, 2, 0, 1}, {0b011, 3, 1, 1}, {0b0100, 4, 0, 2}, {0b0101, 4, 2, 1}, {0b00101, 5, 0, 3},
    {0b00111, 5, 3, 1}, {0b00110, 5, 4, 1}, {0b000110, 6, 1, 2}, {0b000111, 6, 5, 1}, {0b000101, 6, 6, 1},
    {0b000100, 6, 7, 1}, {0b0000110, 7, 0, 4}, {0b0000100, 7, 2, 2}, {0b0000111, 7, 8, 1}, {0b0000101, 7, 9, 1},
    {0b00100110, 8, 0, 5}, {0b00100001, 8, 0, 6}, {0b00100101, 8, 1, 3}, {0b00100100, 8, 3, 2},
    {0b00100111, 8, 10, 1}, {0b00100011, 8, 11, 1}, {0b00100010, 8, 12, 1}, {0b00100000, 8, 13, 1},
    {0b0000001010, 10, 0, 7}, {0b0000001100, 10, 1, 4}, {0b0000001011, 10, 2, 3}, {0b0000001111, 10, 4, 2},
    {0b0000001001, 10, 5, 2}, {0b0000001110, 10, 14, 1}, {0b0000001101, 10, 15, 1}, {0b0000001000, 10, 16, 1},
    {0b000000011101, 12, 0, 8}, {0b000000011000, 12, 0, 9}, {0b000000010011, 12, 0, 10},
    {0b000000010000, 12, 0, 11}, {0b000000011011, 12, 1, 5}, {0b000000010100, 12, 2, 4},
    {0b000000011100, 12, 3, 3}, {0b000000010010, 12, 4, 3}, {0b000000011110, 12, 6, 2},
    {0b000000010101, 12, 7, 2}, {0b000000010001, 12, 8, 2}, {0b000000011111, 12, 17, 1},
    {0b000000011010, 12, 18, 1}, {0b000000011001, 12, 19, 1}, {0b000000010111, 12, 20, 1},
    {0b000000010110, 12, 21, 1}, {0b0000000011010, 13, 0, 12}, {0b0000000011001, 13, 0, 13},
    {0b0000000011000, 13, 0, 14}, {0b0000000010111, 13, 0, 15}, {0b0000000010110, 13, 1, 6},
    {0b0000000010101, 13, 1, 7}, {0b0000000010100, 13, 2, 5}, {0b0000000010011, 13, 3, 4},
    {0b0000000010010, 13, 5, 3}, {0b0000000010001, 13, 9, 2}, {0b0000000010000, 13, 10, 2},
    {0b0000000011111, 13, 22, 1}, {0b0000000011110, 13, 23, 1}, {0b0000000011101, 13, 24, 1},
    {0b0000000011100, 13, 25, 1}, {0b0000000011011, 13, 26, 1}, {0b00000000011111, 14, 0, 16},
    {0b00000000011110, 14, 0, 17}, {0b00000000011101, 14, 0, 18}, {0b00000000011100, 14, 0, 19},
    {0b00000000011011, 14, 0, 20}, {0b00000000011010, 14, 0, 21}, {0b00000000011001, 14, 0, 22},
    {0b00000000011000, 14, 0, 23}, {0b00000000010111, 14, 0, 24}, {0b00000000010110, 14, 0, 25},
    {0b00000000010101, 14, 0, 26}, {0b00000000010100, 14, 0, 27}, {0b00000000010011, 14, 0, 28},
    {0b00000000010010, 14, 0, 29}, {0b00000000010001, 14, 0, 30}, {0b00000000010000, 14, 0, 31},
    {0b000000000011000, 15, 0, 32}, {0b000000000010111, 15, 0, 33}, {0b000000000010110, 15, 0, 34},
    {0b000000000010101, 15, 0, 35}, {0b000000000010100, 15, 0, 36}, {0b000000000010011, 15, 0, 37},
    {0b000000000010010, 15, 0, 38}, {0b000000000010001, 15, 0, 39}, {0b000000000010000, 15, 0, 40},
    {0b000000000011111, 15, 1, 8}, {0b000000000011110, 15, 1, 9}, {0b000000000011101, 15, 1, 10},
    {0b000000000011100, 15, 1, 11}, {0b000000000011011, 15, 1, 12}, {0b000000000011010, 15, 1, 13},
    {0b000000000011001, 15, 1, 14}, {0b0000000000010011, 16, 1, 15}, {0b0000000000010010, 16, 1, 16},
    {0b0000000000010001, 16, 1, 17}, {0b0000000000010000, 16, 1, 18}, {0b0000000000010100, 16, 6, 3},
    {0b0000000000011010, 16, 11, 2}, {0b0000000000011001, 16, 12, 2}, {0b0000000000011000, 16, 13, 2},
    {0b0000000000010111, 16, 14, 2}, {0b0000000000010110, 16, 15, 2}, {0b0000000000010101, 16, 16, 2},
    {0b0000000000011111, 16, 27, 1}, {0b0000000000011110, 16, 28, 1}, {0b0000000000011101, 16, 29, 1},
    {0b0000000000011100, 16, 30, 1}, {0b0000000000011011, 16, 31, 1},
};

// Lookup by the next 17 bits: (length incl. sign, run, level) or 0.
struct AcEntry {
    uint8_t len, run;
    int16_t level;
};
std::vector<AcEntry> g_ac_table;

void build_ac_table() {
    g_ac_table.assign(1 << 17, AcEntry{0, 0, 0});
    for (const Vlc &v : kAc) {
        for (int sign = 0; sign < 2; sign++) {
            uint32_t code = (v.bits << 1) | uint32_t(sign);
            int len = v.len + 1;
            uint32_t first = code << (17 - len), count = 1u << (17 - len);
            for (uint32_t i = 0; i < count; i++)
                g_ac_table[first + i] = AcEntry{uint8_t(len), uint8_t(v.run), int16_t(sign ? -v.level : v.level)};
        }
    }
}

class BitReader {
public:
    BitReader(const uint16_t *words) : w_(words) {}
    uint32_t peek(int n) {
        fill();
        return uint32_t(buf_ >> (64 - n));
    }
    void skip(int n) {
        fill();
        buf_ <<= n;
        avail_ -= n;
    }
    uint32_t get(int n) {
        uint32_t v = peek(n);
        skip(n);
        return v;
    }

private:
    void fill() {
        while (avail_ <= 48) {
            buf_ |= uint64_t(*w_++) << (48 - avail_);
            avail_ += 16;
        }
    }
    const uint16_t *w_;
    uint64_t buf_ = 0;
    int avail_ = 0;
};

// MPEG-1 DC size VLCs (STR v3): luma and chroma.
int dc_size(BitReader &br, bool luma) {
    if (luma) {
        static const struct { uint32_t code; int len, size; } t[] = {
            {0b100, 3, 0}, {0b00, 2, 1}, {0b01, 2, 2}, {0b101, 3, 3}, {0b110, 3, 4}, {0b1110, 4, 5},
            {0b11110, 5, 6}, {0b111110, 6, 7}, {0b1111110, 7, 8}};
        for (auto &e : t)
            if (br.peek(e.len) == e.code) { br.skip(e.len); return e.size; }
    } else {
        static const struct { uint32_t code; int len, size; } t[] = {
            {0b00, 2, 0}, {0b01, 2, 1}, {0b10, 2, 2}, {0b110, 3, 3}, {0b1110, 4, 4}, {0b11110, 5, 5},
            {0b111110, 6, 6}, {0b1111110, 7, 7}, {0b11111110, 8, 8}};
        for (auto &e : t)
            if (br.peek(e.len) == e.code) { br.skip(e.len); return e.size; }
    }
    return -1;
}

// Decodes a whole STR frame into MDEC run-level codes. out[0] = MDEC command (size in words).
int vlc_decode(const uint16_t *frame, uint32_t *out) {
    if (g_ac_table.empty()) build_ac_table();
    uint16_t rlc_halves = frame[0];
    uint16_t qscale = frame[2], version = frame[3];
    if (version < 1 || version > 3) {
        LOGW("DecDCTvlc: unsupported STR version %d", version);
        return -1;
    }
    BitReader br(frame + 4);
    uint16_t *rl = reinterpret_cast<uint16_t *>(out + 1);
    int n = 0;
    int blocks = 0;
    int dc_prev[3] = {0, 0, 0};          // v3: Cr, Cb, Y predictors
    const int max_halves = rlc_halves * 2;  // header: run-level output size in words
    while (n < max_halves) {
        int comp = blocks % 6;  // Cr, Cb, Y, Y, Y, Y
        int dc;
        if (version == 3) {
            int which = comp < 2 ? comp : 2;
            int size = dc_size(br, which == 2);
            if (size < 0) break;
            int diff = 0;
            if (size) {
                diff = int(br.get(size));
                if (!(diff >> (size - 1))) diff -= (1 << size) - 1;
            }
            dc_prev[which] += diff * 4;
            dc = dc_prev[which];
        } else {
            dc = int(br.get(10));
        }
        int block_start = n;
        rl[n++] = uint16_t((qscale << 10) | (dc & 0x3FF));
        // AC coefficients until end of block.
        while (true) {
            if (br.peek(2) == 0b10) {
                br.skip(2);
                rl[n++] = 0xFE00;
                break;
            }
            if (br.peek(6) == 0b000001) {
                br.skip(6);
                uint32_t v = br.get(16);  // 6-bit run, 10-bit level
                rl[n++] = uint16_t(v);
                continue;
            }
            AcEntry e = g_ac_table[br.peek(17)];
            if (!e.len) {
                // The declared size is rounded up and the stream ends in zero padding: the "DC" just
                // read belonged to the padding. Anything else is a corrupt stream.
                if (br.peek(17) != 0) LOGW("DecDCTvlc: bad VLC code in block %d", blocks);
                n = block_start;
                goto done;
            }
            br.skip(e.len);
            rl[n++] = uint16_t((e.run << 10) | (e.level & 0x3FF));
        }
        blocks++;
    }
done:
    while (n < max_halves) rl[n++] = 0xFE00;  // MDEC padding
    if (n & 1) rl[n++] = 0xFE00;
    out[0] = 0x38000000u | uint32_t(n / 2);
    return 0;
}

// ---------------------------------------------------------------- MDEC

const uint8_t kDefaultIq[64] = {
    2, 16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34, 34, 38,
    22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
    26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};
const uint8_t kZigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

uint8_t g_iq_y[64], g_iq_c[64];
float g_idct[8][8];
const uint32_t *g_in;  // run-level stream (after the command word)
uint32_t g_in_words;
bool g_out16 = true;
bool g_stp = false;
uint32_t g_out_callback;
bool g_out_pending;

void vblank_task() {
    if (!g_out_pending) return;
    g_out_pending = false;
    if (g_out_callback) psx::call_game(g_out_callback);
}

void init_idct() {
    for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
            g_idct[x][u] = float((u == 0 ? std::sqrt(0.125) : 0.5) * std::cos((2 * x + 1) * u * 3.14159265358979 / 16));
}

// Decodes one 8x8 block; returns false at the end of the stream.
bool decode_block(const uint16_t *&rl, const uint16_t *end, const uint8_t *iq, float out[64]) {
    while (rl < end && *rl == 0xFE00) rl++;  // padding
    if (rl >= end) return false;
    float coef[64] = {};
    uint16_t first = *rl++;
    int q = first >> 10;
    int dc = int16_t(first << 6) >> 6;
    coef[0] = float(dc * iq[0]);
    int k = 0;
    while (rl < end) {
        uint16_t c = *rl++;
        if (c == 0xFE00) break;
        k += (c >> 10) + 1;
        if (k > 63) break;
        int level = int16_t(c << 6) >> 6;
        int val = q ? (level * iq[k] * q + 4) / 8 : level * 2;
        coef[kZigzag[k]] = float(val);
    }
    // 2D IDCT.
    float tmp[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            float s = 0;
            for (int u = 0; u < 8; u++) s += g_idct[x][u] * coef[y * 8 + u];
            tmp[y * 8 + x] = s;
        }
    for (int x = 0; x < 8; x++)
        for (int y = 0; y < 8; y++) {
            float s = 0;
            for (int v = 0; v < 8; v++) s += g_idct[y][v] * tmp[v * 8 + x];
            out[y * 8 + x] = s;
        }
    return true;
}

// Decodes macroblocks into 'out' (16x16 pixels each, row-major) until 'words' are written.
void mdec_decode(uint32_t *out, uint32_t words) {
    if (!g_in) return;
    const uint16_t *rl = reinterpret_cast<const uint16_t *>(g_in);
    const uint16_t *end = rl + g_in_words * 2;
    uint8_t *dst8 = reinterpret_cast<uint8_t *>(out);
    uint32_t bytes = words * 4;
    uint32_t mb_bytes = g_out16 ? 16 * 16 * 2 : 16 * 16 * 3;
    for (uint32_t pos = 0; pos + mb_bytes <= bytes; pos += mb_bytes) {
        float cr[64], cb[64], y[4][64];
        if (!decode_block(rl, end, g_iq_c, cr) || !decode_block(rl, end, g_iq_c, cb)) break;
        bool ok = true;
        for (int b = 0; b < 4 && ok; b++) ok = decode_block(rl, end, g_iq_y, y[b]);
        if (!ok) break;
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 16; px++) {
                float lum = y[(py / 8) * 2 + px / 8][(py % 8) * 8 + px % 8];
                int ci = (py / 2) * 8 + px / 2;
                float r = lum + 1.402f * cr[ci], g = lum - 0.3437f * cb[ci] - 0.7143f * cr[ci], b = lum + 1.772f * cb[ci];
                int R = std::clamp(int(std::lround(r)) + 128, 0, 255);
                int G = std::clamp(int(std::lround(g)) + 128, 0, 255);
                int B = std::clamp(int(std::lround(b)) + 128, 0, 255);
                if (g_out16) {
                    uint16_t v = uint16_t((R >> 3) | ((G >> 3) << 5) | ((B >> 3) << 10) | (g_stp ? 0x8000 : 0));
                    std::memcpy(dst8 + pos + (py * 16 + px) * 2, &v, 2);
                } else {
                    uint8_t *p = dst8 + pos + (py * 16 + px) * 3;
                    p[0] = uint8_t(R);
                    p[1] = uint8_t(G);
                    p[2] = uint8_t(B);
                }
            }
        }
    }
    g_in = reinterpret_cast<const uint32_t *>(rl);
    g_in_words = uint32_t(end - rl) / 2;
}

}  // namespace

extern "C" {

int psyq_DecDCTReset(int) {
    static bool registered;
    if (!registered) {
        registered = true;
        psx::add_vblank_task(vblank_task);
    }
    g_out_pending = false;
    std::memcpy(g_iq_y, kDefaultIq, 64);
    std::memcpy(g_iq_c, kDefaultIq, 64);
    if (g_idct[0][0] == 0) init_idct();
    g_in = nullptr;
    return 0;
}

int psyq_DecDCTvlcSize(int) {
    return 0;  // we always decode whole frames
}

int psyq_DecDCTvlc(const uint16_t *bs, uint32_t *buf) {
    return vlc_decode(bs, buf) < 0 ? -1 : 0;
}

// mode bit 0 clear: 24-bit output; set (or 2): 16-bit. Bit 1 also requests the STP bit.
int psyq_DecDCTin(const uint32_t *buf, int mode) {
    g_in = buf + 1;
    g_in_words = buf[0] & 0xFFFF;
    g_out16 = mode != 0;
    g_stp = (mode & 2) != 0 && false;
    return 0;
}

int psyq_DecDCTout(uint32_t *buf, int words) {
    LOGD("DecDCTout: %d words to %08X", words, psx::addr_of(buf));
    mdec_decode(buf, uint32_t(words));
    // The MDEC finishes asynchronously on hardware: callers set state after DecDCTout returns that
    // the completion callback then clears, so the callback runs at the next vblank.
    g_out_pending = true;
    return 0;
}

uint32_t psyq_DecDCToutCallback(uint32_t func) {
    uint32_t old = g_out_callback;
    g_out_callback = func;
    return old;
}

}  // extern "C"
