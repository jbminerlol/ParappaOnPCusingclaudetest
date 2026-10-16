// libcd: CD-ROM control on top of the disc image.
//
// Data reads (CdRead) take emulated time at the drive speed, like the hardware: the game polls
// CdReadSync while its loading screen animates from the VSync callback. Continuous reading (CdlReadN / CdlReadS) runs in
// emulated time at 75 or 150 sectors per second: XA audio sectors that pass the channel filter go
// to the SPU's CD input, data sectors go to the streaming ring (libcd St*, used for movies).
#include <cstring>

#include "audio/spu.h"
#include "audio/xa.h"
#include "host/host.h"
#include "psx/disc.h"
#include "psx/log.h"
#include "psx/memory.h"
#include "psx/system.h"
#include "psyq/libcd.h"

namespace {

enum Command : uint8_t {
    CdlNop = 0x01, CdlSetloc = 0x02, CdlPlay = 0x03, CdlForward = 0x04, CdlBackward = 0x05, CdlReadN = 0x06,
    CdlStandby = 0x07, CdlStop = 0x08, CdlPause = 0x09, CdlReset = 0x0A, CdlMute = 0x0B, CdlDemute = 0x0C,
    CdlSetfilter = 0x0D, CdlSetmode = 0x0E, CdlGetparam = 0x0F, CdlGetlocL = 0x10, CdlGetlocP = 0x11,
    CdlGetTN = 0x13, CdlGetTD = 0x14, CdlSeekL = 0x15, CdlSeekP = 0x16, CdlReadS = 0x1B,
};
enum Status : int { CdlNoIntr = 0, CdlDataReady = 1, CdlComplete = 2, CdlAcknowledge = 3, CdlDataEnd = 4,
                    CdlDiskError = 5 };
constexpr uint8_t ModeSpeed = 0x80, ModeRT = 0x40, ModeSize1 = 0x20, ModeSF = 0x08;
constexpr uint8_t StatStandby = 0x02, StatRead = 0x20;

struct Cd {
    uint32_t setloc = 0;     // target of the next read/seek
    uint32_t pos = 0;        // current head position
    uint8_t mode = 0;
    uint8_t filter_file = 0, filter_chan = 0;
    uint8_t last_com = 0;
    bool reading = false;    // continuous ReadN/ReadS
    bool muted = false;
    double sector_acc = 0;
    uint32_t sync_callback = 0;
    uint32_t ready_callback = 0;
    uint8_t last_header[8];  // header + subheader of the last sector read (GetlocL)
    uint8_t last_result[8];  // reply of the last command (CdSync)
    // CdRead in progress
    uint8_t *read_dst = nullptr;
    uint32_t read_lba = 0, read_left = 0, read_size = 2048;
    uint8_t read_mode = 0;
    double read_acc = 0;
} g;

// XA sectors decoded since the current continuous read started, with the span of the CD audio
// stream (spu::cd_pushed() frames) their audio occupies.
struct XaMark {
    uint32_t lba;
    uint64_t start, end;
};
constexpr int kXaMarks = 64;
XaMark g_xa[kXaMarks];
int g_xa_count;          // marks recorded (the newest kXaMarks are kept)
uint32_t g_stream_start; // first sector of the current continuous read

void start_reading(uint32_t lba) {
    g.pos = g_stream_start = lba;
    g.reading = true;
    g.sector_acc = 0;
    g_xa_count = 0;
    xa::reset();
}

// The rhythm clock is the drive position (CdlGetlocL) at which the music was read. On the
// console the audio of an XA sector plays while the drive reads the sectors interleaved after
// it; here it also waits in the port's buffers and the PC's audio output. So report the position
// a PS1 drive would be at for the music now coming out of the speakers, or the game judges every
// press late by the output latency.
uint32_t audible_lba() {
    if (!g.reading) return g.pos ? g.pos - 1 : 0;
    // Nothing audible yet right after the read started: hold the clock at the start (it would
    // otherwise run ahead and jump back when the first XA sector of the channel arrives).
    if (g_xa_count == 0) return (g.mode & ModeRT) && g.pos < g_stream_start + 32 ? g_stream_start : g.pos - 1;
    int64_t audible = int64_t(spu::cd_consumed()) - host::audio_latency_frames();
    int n = g_xa_count < kXaMarks ? g_xa_count : kXaMarks;
    const XaMark &newest = g_xa[(g_xa_count - 1) % kXaMarks];
    if (audible >= int64_t(newest.end)) return g.pos - 1;  // nothing decoded is still waiting
    for (int i = 0; i < n; i++) {
        int k = g_xa_count - 1 - i;
        const XaMark &m = g_xa[k % kXaMarks];
        if (audible < int64_t(m.start) && i + 1 < n) continue;
        // Sectors until the next XA sector of this channel: the interleave.
        uint32_t step = 8;
        if (i > 0) step = g_xa[(k + 1) % kXaMarks].lba - m.lba;
        else if (n > 1) step = m.lba - g_xa[(k - 1) % kXaMarks].lba;
        double f = m.end > m.start ? double(audible - int64_t(m.start)) / double(m.end - m.start) : 0.0;
        int64_t lba = int64_t(m.lba) + int64_t(f * step);
        if (lba < int64_t(g_stream_start)) lba = g_stream_start;
        if (lba > int64_t(g.pos) - 1) lba = int64_t(g.pos) - 1;
        return uint32_t(lba);
    }
    return g.pos - 1;
}

uint8_t status_byte() {
    uint8_t s = StatStandby;
    if (g.reading) s |= StatRead;
    return s;
}

void fill_result(uint8_t *result) {
    if (result) result[0] = status_byte();
}

void getloc_p(uint8_t *result) {
    uint8_t m, s, f, rm, rs, rf;
    psx::lba_to_msf(g.pos, &m, &s, &f);
    psx::lba_to_msf(g.pos > 150 ? g.pos - 150 : 0, &rm, &rs, &rf);  // track-relative (track 1 starts at 00:02:00)
    uint8_t loc[8] = {1, 1, rm, rs, rf, m, s, f};
    std::memcpy(result, loc, 8);
}

void read_one_sector() {
    uint8_t raw[psx::RAW_SECTOR];
    if (!psx::g_disc.read_raw(g.pos, raw)) {
        g.reading = false;
        return;
    }
    std::memcpy(g.last_header, raw + 12, 8);
    g.pos++;
    uint8_t file = raw[16], chan = raw[17], submode = raw[18];
    bool audio = submode & 0x04;
    if (audio) {
        if (!(g.mode & ModeRT)) return;
        if ((g.mode & ModeSF) && (file != g.filter_file || chan != g.filter_chan)) return;
        if (!g.muted) {
            XaMark &m = g_xa[g_xa_count++ % kXaMarks];
            m.lba = g.pos - 1;
            m.start = spu::cd_pushed();
            xa::decode_sector(raw);
            m.end = spu::cd_pushed();
        }
        return;
    }
    if (submode & 0x80) return;  // end-of-file marker only
    st::deliver_sector(raw);
}

// Transfers up to 'n' sectors of the CdRead in progress.
void read_data_sectors(uint32_t n) {
    uint8_t raw[psx::RAW_SECTOR];
    for (; n && g.read_left; n--) {
        if (!psx::g_disc.read_raw(g.read_lba, raw)) {
            LOGE("CdRead: sector %u out of range", g.read_lba);
            g.read_left = 0;
            break;
        }
        std::memcpy(g.read_dst, raw + (g.read_size == 2048 ? 24 : 12), g.read_size);
        std::memcpy(g.last_header, raw + 12, 8);
        g.read_dst += g.read_size;
        g.read_lba++;
        g.read_left--;
    }
    g.pos = g.read_lba;
    g.setloc = g.pos;
}

void vblank_task() {
    if (g.read_left) {
        g.read_acc += ((g.read_mode & ModeSpeed) ? 150.0 : 75.0) / psx::VBLANK_HZ;
        uint32_t n = uint32_t(g.read_acc);
        g.read_acc -= n;
        read_data_sectors(n);
    }
    static uint32_t ticks;
    if (++ticks % 120 == 0)
        LOGD("CD: reading=%d pos=%u mode=%02X xa_buffered=%d clock_lag=%d sectors (output %d frames)", g.reading,
             g.pos, g.mode, spu::cd_buffered(), g.reading ? int(g.pos - 1 - audible_lba()) : 0,
             host::audio_latency_frames());
    if (!g.reading) return;
    g.sector_acc += ((g.mode & ModeSpeed) ? 150.0 : 75.0) / psx::VBLANK_HZ;
    while (g.sector_acc >= 1.0 && g.reading) {
        g.sector_acc -= 1.0;
        read_one_sector();
    }
}

int control_impl(uint8_t com, const uint8_t *param, uint8_t *result) {
    g.last_com = com;
    if (com != CdlNop && com != CdlGetlocP && com != CdlGetlocL)
        LOGD("CdControl %02X %02X %02X %02X (pos %u)", com, param ? param[0] : 0, param ? param[1] : 0,
             param ? param[2] : 0, g.pos);
    switch (com) {
    case CdlNop:
        break;
    case CdlSetloc:
        if (param) g.setloc = psx::msf_to_lba(param[0], param[1], param[2]);
        break;
    case CdlReadN:
    case CdlReadS:
        if (param) g.setloc = psx::msf_to_lba(param[0], param[1], param[2]);
        start_reading(g.setloc);
        break;
    case CdlSeekL:
    case CdlSeekP:
        g.pos = g.setloc;
        break;
    case CdlPause:
    case CdlStop:
    case CdlStandby:
        g.reading = false;
        break;
    case CdlReset:
        g.reading = false;
        g.mode = 0;
        break;
    case CdlMute:
        g.muted = true;
        spu::cd_clear();
        break;
    case CdlDemute:
        g.muted = false;
        break;
    case CdlSetfilter:
        if (param) {
            g.filter_file = param[0];
            g.filter_chan = param[1];
        }
        break;
    case CdlSetmode:
        if (param) g.mode = param[0];
        break;
    case CdlGetparam:
        if (result) {
            result[0] = status_byte();
            result[1] = g.mode;
            result[2] = 0;
            result[3] = g.filter_file;
            result[4] = g.filter_chan;
        }
        return 1;
    case CdlGetlocL:
        if (result) {
            std::memcpy(result, g.last_header, 8);
            if (g.reading && g.pos > g_stream_start) psx::lba_to_msf(audible_lba(), &result[0], &result[1], &result[2]);
        }
        return 1;
    case CdlGetlocP:
        if (result) getloc_p(result);
        return 1;
    case CdlGetTN:
        if (result) {
            result[0] = status_byte();
            result[1] = 1;
            result[2] = 1;
        }
        return 1;
    case CdlGetTD:
        if (result) {
            uint8_t m, s, f;
            psx::lba_to_msf(psx::g_disc.sector_count(), &m, &s, &f);
            result[0] = status_byte();
            result[1] = m;
            result[2] = s;
        }
        return 1;
    default:
        LOGW("CdControl: unhandled command %02X", com);
        break;
    }
    fill_result(result);
    return 1;
}

// Every command's reply (up to 8 bytes) is kept: libcd returns it from CdSync(), which is how the
// game reads CdlGetlocL (the position of the XA music, which drives the rhythm timing).
int control(uint8_t com, const uint8_t *param, uint8_t *result) {
    uint8_t res[8] = {status_byte(), 0, 0, 0, 0, 0, 0, 0};
    int r = control_impl(com, param, res);
    std::memcpy(g.last_result, res, sizeof(res));
    if (result) std::memcpy(result, res, sizeof(res));
    return r;
}

}  // namespace

namespace cd {

void init() {
    psx::add_vblank_task(vblank_task);
}

// Starts continuous streaming reads (CdRead2) at the current Setloc position.
void start_stream(uint8_t mode) {
    g.mode = mode;
    start_reading(g.setloc);
}

}  // namespace cd

extern "C" {

int psyq_CdInit() {
    g = Cd{};
    LOGI("CdInit");
    return 1;
}

int psyq_CdLastCom() {
    return g.last_com;
}

int psyq_CdSync(int, uint8_t *result) {
    if (result) std::memcpy(result, g.last_result, sizeof(g.last_result));
    return CdlComplete;
}

uint32_t psyq_CdSyncCallback(uint32_t func) {
    uint32_t old = g.sync_callback;
    g.sync_callback = func;
    return old;
}

int psyq_CdControlF(int com, const uint8_t *param) {
    return control(uint8_t(com), param, nullptr);
}

int psyq_CdControlB(int com, const uint8_t *param, uint8_t *result) {
    return control(uint8_t(com), param, result);
}

int psyq_CdMix(const uint8_t *atv) {
    // CdlATV: L->L, L->R, R->R, R->L (0x80 = 100%). The cross terms are ignored.
    spu::set_cd_volume(int16_t(atv[0] * 0x7FFF / 0x80), int16_t(atv[2] * 0x7FFF / 0x80));
    return 1;
}

CdlLOC *psyq_CdIntToPos(int lba, CdlLOC *p) {
    psx::lba_to_msf(uint32_t(lba), &p->minute, &p->second, &p->sector);
    p->track = 0;
    return p;
}

int psyq_CdPosToInt(const CdlLOC *p) {
    return int(psx::msf_to_lba(p->minute, p->second, p->sector));
}

CdlFILE *psyq_CdSearchFile(CdlFILE *fp, const char *name) {
    psx::DiscFile f;
    if (!psx::g_disc.find(name, &f)) {
        LOGW("CdSearchFile: %s not found", name);
        return nullptr;
    }
    psx::lba_to_msf(f.lba, &fp->pos.minute, &fp->pos.second, &fp->pos.sector);
    fp->pos.track = 0;
    fp->size = f.size;
    const char *base = std::strrchr(name, '\\');
    std::strncpy(fp->name, base ? base + 1 : name, sizeof(fp->name) - 1);
    fp->name[sizeof(fp->name) - 1] = 0;
    LOGD("CdSearchFile: %s at %u (%u bytes)", name, f.lba, f.size);
    return fp;
}

// Starts reading 'sectors' data sectors from the Setloc position; they arrive at drive speed.
int psyq_CdRead(int sectors, uint32_t *buf, int mode) {
    if (g.read_left) read_data_sectors(g.read_left);  // a read still running: finish it first
    g.read_dst = psx::mirror(reinterpret_cast<uint8_t *>(buf));
    g.read_size = (mode & ModeSize1) ? 2340 : 2048;
    g.read_mode = uint8_t(mode);
    g.read_lba = g.setloc;
    g.read_left = uint32_t(sectors > 0 ? sectors : 0);
    g.read_acc = 0;
    g.last_com = CdlReadN;
    if (psx::in_vblank()) read_data_sectors(g.read_left);  // no time can pass inside an interrupt
    return 1;
}

// mode 0 waits for the read; mode 1 returns the sectors remaining. The game polls mode 1 in a bare
// loop, so each poll lets a vblank pass (the loading screen runs from the VSync callback).
int psyq_CdReadSync(int mode, uint8_t *result) {
    while (g.read_left && !psx::in_vblank()) {
        psx::vblank();
        if (mode) break;
    }
    if (psx::in_vblank()) read_data_sectors(g.read_left);
    fill_result(result);
    return int(g.read_left);
}

int psyq_CdRead2(int mode) {
    LOGD("CdRead2 mode %02X at %u", mode, g.setloc);
    cd::start_stream(uint8_t(mode));
    return 1;
}

}  // extern "C"
