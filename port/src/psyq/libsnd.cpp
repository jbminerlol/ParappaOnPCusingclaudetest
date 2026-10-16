// libsnd subset: VAB sound banks and the SsUt* direct voice interface, on the native SPU.
//
// VAB header (kept in game RAM, as libsnd requires): 32-byte VabHdr, 128 x 16-byte ProgAtr,
// ps x 16 x 32-byte VagAtr (tones), then 256 x u16 VAG sizes (in units of 8 bytes).
#include <algorithm>
#include <cmath>
#include <cstring>

#include "audio/spu.h"
#include "psx/log.h"
#include "psx/memory.h"

namespace {

constexpr int MAX_VABS = 16;
constexpr uint32_t SPU_DATA_START = 0x1010;  // after the SPU's reserved area

struct Vab {
    const uint8_t *header = nullptr;
    int programs = 0;
    uint32_t vag_addr[256] = {};  // SPU RAM address of each VAG (index 1..)
    bool loaded = false;
    uint32_t body_base = 0, body_size = 0, body_done = 0;  // SPU RAM placement / partial transfer
    bool transferring = false;
};

Vab g_vabs[MAX_VABS];
uint32_t g_spu_alloc = SPU_DATA_START;
int g_master_l = 127, g_master_r = 127;

struct VoiceOwner {
    int vab = -1, prog = -1, tone = -1, note = -1;
};
VoiceOwner g_owner[spu::VOICES];

const uint8_t *prog_attr(const Vab &v, int prog) {
    return v.header + 32 + prog * 16;
}

const uint8_t *tone_attr(const Vab &v, int prog, int tone) {
    // Tone tables exist only for programs with tones, in program order.
    int slot = 0;
    for (int p = 0; p < prog; p++)
        if (prog_attr(v, p)[0]) slot++;
    return v.header + 32 + 128 * 16 + (slot * 16 + tone) * 32;
}

const uint16_t *vag_sizes(const Vab &v) {
    return reinterpret_cast<const uint16_t *>(v.header + 32 + 128 * 16 + v.programs * 16 * 32);
}

uint16_t note_pitch(int note, int fine, int center, int shift) {
    double semis = (note - center) + (fine - shift) / 128.0;
    double p = 4096.0 * std::pow(2.0, semis / 12.0);
    return uint16_t(p > 0x3FFF ? 0x3FFF : p);
}

void apply_master() {
    spu::set_main_volume(int16_t(g_master_l * 0x3FFF / 127), int16_t(g_master_r * 0x3FFF / 127));
}

}  // namespace

extern "C" {

int psyq_SsInit() {
    for (Vab &v : g_vabs) v = Vab{};
    for (VoiceOwner &o : g_owner) o = VoiceOwner{};
    g_spu_alloc = SPU_DATA_START;
    return 0;
}

int psyq_SsStart() { return 0; }
int psyq_SsSetTickMode(int) { return 0; }
int psyq_SsUtFlush() { return 0; }

int psyq_SsSetMVol(int l, int r) {
    g_master_l = l & 0x7F;
    g_master_r = r & 0x7F;
    apply_master();
    return 0;
}

int psyq_SsGetMVol(uint16_t *vol) {
    vol[0] = uint16_t(g_master_l);
    vol[1] = uint16_t(g_master_r);
    return 0;
}

// (serial A = CD input) mix on/off.
int psyq_SsSetSerialAttr(int serial, int attr, int mode) {
    if (serial == 0 && attr == 0) spu::set_cd_enabled(mode != 0);
    return 0;
}

int psyq_SsSetSerialVol(int serial, int l, int r) {
    if (serial == 0) {
        l = l > 127 ? 127 : l < 0 ? 0 : l;
        r = r > 127 ? 127 : r < 0 ? 0 : r;
        spu::set_cd_volume(int16_t(l * 0x102), int16_t(r * 0x102));
    }
    return 0;
}

int psyq_SsVabOpenHead(const uint8_t *addr, int vabid) {
    if (std::memcmp(addr, "pBAV", 4) != 0) {
        LOGE("SsVabOpenHead: bad VAB header at %08X", psx::addr_of(addr));
        return -1;
    }
    int id = vabid;
    if (id < 0 || id >= MAX_VABS) {
        for (id = 0; id < MAX_VABS && g_vabs[id].header; id++) {
        }
        if (id == MAX_VABS) return -1;
    }
    Vab &v = g_vabs[id];
    v = Vab{};
    v.header = addr;
    uint16_t ps;
    std::memcpy(&ps, addr + 18, 2);
    v.programs = ps;
    LOGI("SsVabOpenHead: VAB %d, %d programs", id, ps);
    return id;
}

// Reserves SPU RAM for a VAB's body and computes its VAG addresses; returns the body size.
static uint32_t begin_body(Vab &v) {
    const uint16_t *sizes = vag_sizes(v);
    uint32_t off = 0, total = 0;
    for (int i = 0; i < 256; i++) total += uint32_t(sizes[i]) * 8;
    if (g_spu_alloc + total > spu::RAM_SIZE) {
        LOGW("SsVabTransBody: SPU RAM full, recycling");
        g_spu_alloc = SPU_DATA_START;
    }
    v.body_base = g_spu_alloc;
    v.body_size = total;
    v.body_done = 0;
    for (int i = 0; i < 256; i++) {
        v.vag_addr[i] = v.body_base + off;  // VAG i starts after the sizes of VAGs 0..i-1
        off += uint32_t(sizes[i]) * 8;
    }
    g_spu_alloc = (v.body_base + total + 15) & ~15u;
    return total;
}

int psyq_SsVabTransBody(const uint8_t *body, int vabid) {
    if (vabid < 0 || vabid >= MAX_VABS || !g_vabs[vabid].header) return -1;
    Vab &v = g_vabs[vabid];
    uint32_t total = begin_body(v);
    spu::write_ram(v.body_base, body, total);
    v.loaded = true;
    return vabid;
}

// Transfers the VAB body in pieces of 'bufsize' bytes: returns -2 while more is needed, then the id.
int psyq_SsVabTransBodyPartly(const uint8_t *buf, uint32_t bufsize, int vabid) {
    vabid = int16_t(vabid);
    if (vabid < 0 || vabid >= MAX_VABS || !g_vabs[vabid].header) return -1;
    Vab &v = g_vabs[vabid];
    if (!v.transferring) {
        begin_body(v);
        v.transferring = true;
    }
    uint32_t n = std::min(bufsize, v.body_size - v.body_done);
    spu::write_ram(v.body_base + v.body_done, buf, n);
    v.body_done += n;
    if (v.body_done < v.body_size) return -2;
    v.transferring = false;
    v.loaded = true;
    return vabid;
}

int psyq_SsVabTransCompleted(int) {
    return 1;
}

int psyq_SsVabClose(int vabid) {
    if (vabid >= 0 && vabid < MAX_VABS) g_vabs[vabid] = Vab{};
    bool any = false;
    for (const Vab &v : g_vabs) any |= v.header != nullptr;
    if (!any) g_spu_alloc = SPU_DATA_START;
    return 0;
}

int psyq_SsUtKeyOn(int vabid, int prog, int tone, int note, int fine, int voll, int volr) {
    vabid = int16_t(vabid);
    if (vabid < 0 || vabid >= MAX_VABS || !g_vabs[vabid].loaded) return -1;
    const Vab &v = g_vabs[vabid];
    const uint8_t *pa = prog_attr(v, prog & 0x7F);
    if ((tone & 0xFF) >= pa[0]) return -1;
    const uint8_t *ta = tone_attr(v, prog & 0x7F, tone & 0xFF);
    uint16_t vag, adsr1, adsr2;
    std::memcpy(&vag, ta + 22, 2);
    std::memcpy(&adsr1, ta + 16, 2);
    std::memcpy(&adsr2, ta + 18, 2);
    if (vag == 0 || vag > 255) return -1;
    int voice = spu::pick_voice();
    if (voice < 0) return -1;

    int vab_vol = v.header[24];  // VabHdr.mvol
    int vol = voll > volr ? voll : volr;
    double level = (vol / 127.0) * (ta[2] / 127.0) * (pa[1] / 127.0) * (vab_vol / 127.0);
    // Pan: key pan from the l/r ratio, tone pan and program pan (64 = centre).
    int key_pan = voll == volr ? 64 : (voll > volr ? volr * 64 / (voll ? voll : 1) : 127 - voll * 64 / (volr ? volr : 1));
    double pan = (key_pan - 64) + (ta[3] - 64) + (pa[4] - 64);
    pan = pan < -64 ? -64 : pan > 63 ? 63 : pan;
    double left = level * (pan <= 0 ? 1.0 : (63 - pan) / 63.0);
    double right = level * (pan >= 0 ? 1.0 : (64 + pan) / 64.0);

    spu::VoiceParams p;
    p.start = v.vag_addr[vag];
    p.pitch = note_pitch(note & 0x7F, fine & 0x7F, ta[4], ta[5]);
    p.vol_l = int16_t(left * 0x3FFF);
    p.vol_r = int16_t(right * 0x3FFF);
    p.adsr1 = adsr1;
    p.adsr2 = adsr2;
    spu::key_on(voice, p);
    g_owner[voice] = VoiceOwner{vabid, prog, tone, note};
    return voice;
}

int psyq_SsUtKeyOff(int voice, int vabid, int prog, int tone, int note) {
    voice = int16_t(voice);
    if (voice < 0 || voice >= spu::VOICES) return -1;
    const VoiceOwner &o = g_owner[voice];
    if (o.vab != int16_t(vabid) || o.prog != int16_t(prog) || o.tone != int16_t(tone) || o.note != int16_t(note))
        return -1;
    spu::key_off(voice);
    return 0;
}

int psyq_SsUtAllKeyOff(int) {
    for (int i = 0; i < spu::VOICES; i++) spu::key_off(i);
    return 0;
}

}  // extern "C"
