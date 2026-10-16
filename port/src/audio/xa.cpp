#include "audio/xa.h"

#include <algorithm>
#include <vector>

#include "audio/spu.h"

namespace xa {
namespace {

const int kPos[4] = {0, 60, 115, 98};
const int kNeg[4] = {0, 0, -52, -55};

int16_t g_hist[2][2];       // ADPCM history per channel
double g_phase;             // resampler position between input samples
int16_t g_last[2];          // previous input sample per channel (for interpolation)

void decode_unit(const uint8_t *group, int unit, int16_t *out, int16_t *hist, bool eight_bit) {
    uint8_t param = group[4 + unit];
    int shift = param & 15, filter = (param >> 4) & 3;
    if (shift > 12) shift = 9;
    for (int i = 0; i < 28; i++) {
        int s;
        if (eight_bit) {
            s = int16_t(group[16 + i * 4 + unit] << 8) >> shift;
        } else {
            int nibble = (group[16 + i * 4 + unit / 2] >> ((unit & 1) * 4)) & 15;
            s = int16_t(nibble << 12) >> shift;
        }
        s += (hist[0] * kPos[filter] + hist[1] * kNeg[filter] + 32) / 64;
        s = std::clamp(s, -32768, 32767);
        hist[1] = hist[0];
        hist[0] = int16_t(s);
        out[i] = int16_t(s);
    }
}

}  // namespace

void reset() {
    std::fill(&g_hist[0][0], &g_hist[0][0] + 4, int16_t(0));
    g_phase = 0;
    g_last[0] = g_last[1] = 0;
}

void decode_sector(const uint8_t *raw) {
    uint8_t coding = raw[16 + 3];
    bool stereo = (coding & 3) == 1;
    bool half_rate = (coding >> 2) & 1;  // 18.9 kHz instead of 37.8 kHz
    bool eight_bit = (coding >> 4) & 1;
    const uint8_t *data = raw + 24;
    int units = eight_bit ? 4 : 8;

    std::vector<int16_t> left, right;
    left.reserve(18 * 8 * 28);
    right.reserve(18 * 8 * 28);
    int16_t buf[28];
    for (int g = 0; g < 18; g++) {
        const uint8_t *group = data + g * 128;
        for (int u = 0; u < units; u++) {
            if (stereo) {
                decode_unit(group, u, buf, g_hist[u & 1], eight_bit);
                (u & 1 ? right : left).insert((u & 1 ? right : left).end(), buf, buf + 28);
            } else {
                decode_unit(group, u, buf, g_hist[0], eight_bit);
                left.insert(left.end(), buf, buf + 28);
            }
        }
    }
    if (!stereo) right = left;

    // Resample 37.8 / 18.9 kHz to 44.1 kHz with linear interpolation.
    const double step = (half_rate ? 18900.0 : 37800.0) / 44100.0;
    std::vector<int16_t> out;
    out.reserve(size_t(left.size() / step + 2) * 2);
    size_t n = std::min(left.size(), right.size());
    for (size_t i = 0; i < n;) {
        double t = g_phase;
        int16_t l0 = i == 0 ? g_last[0] : left[i - 1], r0 = i == 0 ? g_last[1] : right[i - 1];
        out.push_back(int16_t(l0 + (left[i] - l0) * t));
        out.push_back(int16_t(r0 + (right[i] - r0) * t));
        g_phase += step;
        while (g_phase >= 1.0 && i < n) {
            g_phase -= 1.0;
            i++;
        }
    }
    if (n) {
        g_last[0] = left[n - 1];
        g_last[1] = right[n - 1];
    }
    spu::cd_push(out.data(), int(out.size() / 2));
}

}  // namespace xa
