// libgs (extended graphics library): double buffering, ordering tables and the 2D sorters.
// Packets are built in the game's packet area (GsSetWorkBase) exactly as the original library
// does, then executed by the GPU when the ordering table is drawn.
#include <cmath>
#include <cstring>
#include <utility>

#include "gpu/gpu.h"
#include "psx/log.h"
#include "psx/memory.h"
#include "psyq/libgpu.h"
#include "psyq/libgs.h"
#include "psyq/libgte.h"

namespace {

constexpr uint32_t GS_IDMATRIX = 0x80091838u;    // GsIDMATRIX, read directly by game code
constexpr uint32_t CLEAR_PACKETS = 0x8008A730u;  // libgs' static GsSortClear packets (2 x 16 bytes)

struct Gs {
    int w = 320, h = 240;
    bool ofs_gpu = false;   // GsOFSGPU: drawing offset applied by the GPU
    bool dither = false;
    short buf_x[2] = {0, 0}, buf_y[2] = {0, 0};
    int active = 0;
    uint32_t swap_count = 0;
    short center_x = 0, center_y = 0;  // GsInit3D: screen centre
    short pos_x = 0, pos_y = 0;        // POSITION: offset added to sprite coordinates
    RECT clip{0, 0, 320, 240};         // GsDRAWENV.clip, relative to the buffer
    DRAWENV drawenv{};
    DISPENV dispenv{};
    uint32_t *work = nullptr;          // packet area (GsSetWorkBase)
    int projection = 1000;
} g;

uint32_t link_addr(const void *p) {
    return psx::addr_of(p) & 0x00FFFFFFu;
}

// Links an n-word packet (plus header) at 'pri' and returns the next free packet address.
uint32_t *make_packet(uint32_t *p, GsOT *ot, uint32_t pri, int words) {
    int idx = int(pri & 0xFFFF) - int(ot->offset);
    if (idx < 0) {
        PSX_TODO_ONCE("libgs: z resolution overflow");
        idx = 0;
    }
    uint32_t *entry = reinterpret_cast<uint32_t *>(ot->org) + idx;
    p[0] = (*entry & 0x00FFFFFFu) | (uint32_t(words) << 24);
    *entry = link_addr(p);
    return p + words + 1;
}

uint32_t texpage_word(uint32_t attr, uint32_t tpage) {
    return 0xE1000200u | (tpage & 0x1F) | ((attr >> 17) & 0x180) | ((attr >> 23) & 0x60);
}

uint32_t sprite_cmd(uint32_t attr, uint32_t code, uint8_t r, uint8_t gr, uint8_t b) {
    return ((attr >> 5) & 0x02000000u) | ((attr & 0x40) << 18) | (code << 24) | uint32_t(b) << 16 |
           uint32_t(gr) << 8 | r;
}

uint32_t clut_uv(const GsSPRITE *s) {
    return uint32_t(s->u) | uint32_t(s->v) << 8 | uint32_t(int32_t(s->cy) << 22) | (uint32_t(s->cx) & 0x3F0) << 12;
}

void set_draw_clip() {
    int b = g.active;
    g.drawenv.clip.x = short(g.clip.x + g.buf_x[b]);
    g.drawenv.clip.y = short(g.clip.y + g.buf_y[b]);
    g.drawenv.clip.w = g.clip.w;
    g.drawenv.clip.h = g.clip.h;
    psyq_PutDrawEnv(&g.drawenv);
}

void set_draw_offset() {
    int b = g.active;
    if (g.ofs_gpu) {
        g.pos_x = g.pos_y = 0;
        g.drawenv.ofs[0] = short(g.center_x + g.buf_x[b]);
        g.drawenv.ofs[1] = short(g.center_y + g.buf_y[b]);
        psyq_PutDrawEnv(&g.drawenv);
    } else {
        g.pos_x = short(g.center_x + g.buf_x[b]);
        g.pos_y = short(g.center_y + g.buf_y[b]);
        g.drawenv.ofs[0] = g.drawenv.ofs[1] = 0;
        psyq_PutDrawEnv(&g.drawenv);
    }
}

void init_idmatrix() {
    MATRIX *m = psx::ptr_as<MATRIX>(GS_IDMATRIX);
    std::memset(m, 0, sizeof(*m));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = 4096;
}

}  // namespace

extern "C" {

int psyq_GsInitGraph(int w, int h, int intmode, int dither, int vram) {
    (void)vram;
    psyq_ResetGraph(0);
    g = Gs{};
    g.w = w & 0xFFFF;
    g.h = h & 0xFFFF;
    g.ofs_gpu = (intmode & 4) != 0;
    g.dither = dither != 0;
    if (intmode & 1) PSX_TODO_ONCE("GsInitGraph: interlaced mode");
    g.clip = RECT{0, 0, short(g.w), short(g.h)};
    g.drawenv.dtd = g.dither;
    g.drawenv.dfe = 1;
    g.dispenv.disp = RECT{0, 0, short(g.w), short(g.h)};
    g.dispenv.screen = RECT{0, 0, 256, 240};
    init_idmatrix();
    set_draw_clip();
    set_draw_offset();
    LOGI("GsInitGraph %dx%d mode %d", g.w, g.h, intmode);
    return 0;
}

int psyq_GsDefDispBuff(int x0, int y0, int x1, int y1) {
    g.buf_x[0] = short(x0);
    g.buf_y[0] = short(y0);
    g.buf_x[1] = short(x1);
    g.buf_y[1] = short(y1);
    set_draw_clip();
    set_draw_offset();
    return 0;
}

int psyq_GsInit3D() {
    g.center_x = short(g.w / 2);
    g.center_y = short(g.h / 2);
    set_draw_offset();
    return 0;
}

int psyq_GsSwapDispBuff() {
    g.dispenv.disp.x = g.buf_x[g.active];
    g.dispenv.disp.y = g.buf_y[g.active];
    g.dispenv.disp.w = short(g.w);
    g.dispenv.disp.h = short(g.h);
    psyq_PutDispEnv(&g.dispenv);
    psyq_SetDispMask(1);
    g.swap_count++;
    g.active ^= 1;
    set_draw_clip();
    set_draw_offset();
    return 0;
}

int psyq_GsGetActiveBuff() {
    return g.active;
}

int psyq_GsSetWorkBase(uint32_t *base) {
    g.work = base;
    return 0;
}

int psyq_GsClearOt(uint32_t offset, uint32_t point, GsOT *ot) {
    ot->offset = offset & 0xFFFF;
    ot->point = point & 0xFFFF;
    int n = 1 << (ot->length & 31);
    ot->tag = ot->org + n - 1;
    psyq_ClearOTagR(reinterpret_cast<uint32_t *>(ot->org), n);
    return 0;
}

int psyq_GsDrawOt(GsOT *ot) {
    psyq_DrawOTag(reinterpret_cast<const uint32_t *>(ot->tag));
    return 0;
}

int psyq_GsSortClear(int r, int gr, int b, GsOT *ot) {
    // GP0(02h) fill of the whole draw buffer, drawn before everything else (end of the reverse OT).
    uint32_t *p = psx::ptr_as<uint32_t>(CLEAR_PACKETS + uint32_t(g.active) * 16);
    p[1] = 0x02000000u | uint32_t(b & 0xFF) << 16 | uint32_t(gr & 0xFF) << 8 | uint32_t(r & 0xFF);
    p[2] = uint32_t(uint16_t(g.buf_y[g.active])) << 16 | uint16_t(g.buf_x[g.active]);
    p[3] = uint32_t(uint16_t(g.h)) << 16 | uint16_t(g.w);
    p[0] = 3u << 24;
    libgpu::add_prim(reinterpret_cast<uint32_t *>(ot->tag), p);
    return 0;
}

int psyq_GsClearDispArea(int r, int gr, int b) {
    RECT rc{g.buf_x[g.active], g.buf_y[g.active], short(g.w), short(g.h)};
    psyq_ClearImage(&rc, r & 0xFF, gr & 0xFF, b & 0xFF);
    return 0;
}

int psyq_GsSortFastSprite(const GsSPRITE *s, GsOT *ot, uint32_t pri) {
    uint32_t attr = s->attribute;
    if (int32_t(attr) < 0 || s->w == 0 || s->h == 0) return 0;
    uint32_t *p = g.work;
    p[1] = texpage_word(attr, s->tpage);
    p[2] = sprite_cmd(attr, 0x64, s->r, s->g, s->b);
    p[3] = uint32_t(uint16_t(s->x + g.pos_x)) | uint32_t(uint16_t(s->y + g.pos_y)) << 16;
    p[4] = clut_uv(s);
    p[5] = uint32_t(s->w) | uint32_t(s->h) << 16;
    g.work = make_packet(p, ot, pri, 5);
    return 0;
}

int psyq_GsSortSprite(const GsSPRITE *s, GsOT *ot, uint32_t pri) {
    uint32_t attr = s->attribute;
    if (int32_t(attr) < 0 || s->w == 0 || s->h == 0) return 0;
    bool unit_scale = s->scalex == 4096 && s->scaley == 4096;
    bool simple = (attr & 0x08000000u) || (unit_scale && s->rotate == 0 && !(attr & 0x00C00000u));
    uint32_t *p = g.work;
    if (simple) {
        p[1] = texpage_word(attr, s->tpage);
        p[2] = sprite_cmd(attr, 0x64, s->r, s->g, s->b);
        p[3] = uint32_t(uint16_t(s->x + g.pos_x - s->mx)) | uint32_t(uint16_t(s->y + g.pos_y - s->my)) << 16;
        p[4] = clut_uv(s);
        p[5] = uint32_t(s->w) | uint32_t(s->h) << 16;
        g.work = make_packet(p, ot, pri, 5);
        return 0;
    }
    // Rotated / scaled / flipped: a textured quad around the (mx, my) centre.
    // rotate is in 1/4096 degree; scale is 4.12 fixed point.
    double angle = double(s->rotate) / 4096.0 * 3.14159265358979323846 / 180.0;
    double c = std::cos(angle), sn = std::sin(angle);
    double sx = s->scalex / 4096.0, sy = s->scaley / 4096.0;
    int xs[4], ys[4];
    for (int k = 0; k < 4; k++) {
        double cx = ((k & 1) ? s->w : 0) - s->mx, cy = ((k & 2) ? s->h : 0) - s->my;
        cx *= sx;
        cy *= sy;
        xs[k] = int(std::lround(cx * c - cy * sn)) + s->x + g.pos_x;
        ys[k] = int(std::lround(cx * sn + cy * c)) + s->y + g.pos_y;
    }
    int u0 = s->u, u1 = (s->u + s->w - 1) & 0xFF, v0 = s->v, v1 = (s->v + s->h - 1) & 0xFF;
    if (attr & 0x00400000u) std::swap(u0, u1);  // horizontal flip
    if (attr & 0x00800000u) std::swap(v0, v1);  // vertical flip
    uint32_t clut = uint32_t(int32_t(s->cy) << 22) | (uint32_t(s->cx) & 0x3F0) << 12;
    uint32_t tpage = (uint32_t(s->tpage & 0x1F) << 16) | ((attr >> 1) & 0x01800000u) | ((attr >> 7) & 0x00600000u);
    auto xy = [&](int k) { return uint32_t(uint16_t(xs[k])) | uint32_t(uint16_t(ys[k])) << 16; };
    p[1] = sprite_cmd(attr, 0x2C, s->r, s->g, s->b);
    p[2] = xy(0);
    p[3] = uint32_t(u0) | uint32_t(v0) << 8 | clut;
    p[4] = xy(1);
    p[5] = uint32_t(u1) | uint32_t(v0) << 8 | tpage;
    p[6] = xy(2);
    p[7] = uint32_t(u0) | uint32_t(v1) << 8;
    p[8] = xy(3);
    p[9] = uint32_t(u1) | uint32_t(v1) << 8;
    g.work = make_packet(p, ot, pri, 9);
    return 0;
}

int psyq_GsSortBoxFill(const GsBOXF *box, GsOT *ot, uint32_t pri) {
    uint32_t attr = box->attribute;
    if (int32_t(attr) < 0) return 0;
    uint32_t *p = g.work;
    p[1] = 0xE1000200u | ((attr >> 17) & 0x180) | ((attr >> 23) & 0x60);
    p[2] = uint32_t(0x60 | ((attr >> 29) & 2)) << 24 | uint32_t(box->b) << 16 | uint32_t(box->g) << 8 | box->r;
    p[3] = uint32_t(uint16_t(box->x + g.pos_x)) | uint32_t(uint16_t(box->y + g.pos_y)) << 16;
    p[4] = uint32_t(box->w) | uint32_t(box->h) << 16;
    g.work = make_packet(p, ot, pri, 4);
    return 0;
}

// Copies a GPU primitive (POLY_* / SPRT ...) into the packet area, applying POSITION.
int psyq_GsSortPoly(const uint32_t *prim, GsOT *ot, uint32_t pri) {
    uint8_t code = uint8_t(prim[1] >> 24);
    bool textured = code & 0x04, gouraud = code & 0x10, quad = code & 0x08;
    int n = quad ? 4 : 3;
    uint32_t *p = g.work;
    int src = 1, dst = 1;
    p[dst++] = prim[src++];  // colour + code
    for (int k = 0; k < n; k++) {
        if (k > 0 && gouraud) p[dst++] = prim[src++];
        uint32_t xy = prim[src++];
        p[dst++] = uint32_t(uint16_t(int16_t(xy) + g.pos_x)) | uint32_t(uint16_t(int16_t(xy >> 16) + g.pos_y)) << 16;
        if (textured) p[dst++] = prim[src++];
    }
    g.work = make_packet(p, ot, pri, dst - 1);
    return 0;
}

// GsInitFixBg32: pre-builds, for each buffer, an offset packet, a grid of 32x32 cell sprites
// covering the screen plus one cell, and a closing offset packet, in the work area.
int psyq_GsInitFixBg32(const GsBG *bg, uint32_t *work) {
    uint32_t attr = bg->attribute;
    int cols = (g.w + 31) / 32 + 1, rows = (g.h + 31) / 32 + 1;
    uint32_t *p = work + 2;
    for (int b = 0; b < 2; b++) {
        work[b] = psx::addr_of(p);
        p[0] = link_addr(p + 2) | (1u << 24);
        p[1] = 0xE5000000u;
        uint32_t *q = p + 2;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                q[0] = link_addr(q + 6) | (5u << 24);
                q[1] = 0xE1000000u;
                q[2] = ((attr >> 5) & 0x02000000u) + 0x65000000u;
                q[3] = uint32_t(uint16_t(c * 32)) | uint32_t(uint16_t(r * 32 + b * g.h)) << 16;
                q[5] = 0x00200020u;
                q += 6;
            }
        }
        q[0] = 1u << 24;  // closing offset packet; linked into the OT when sorted
        q[1] = 0xE5000000u;
        p = q + 2;
    }
    return 0;
}

int psyq_GsSortFixBg32(const GsBG *bg, uint32_t *work, GsOT *ot, uint32_t pri) {
    uint32_t attr = bg->attribute;
    if (int32_t(attr) < 0) return 0;
    int cols = (g.w + 31) / 32 + 1, rows = (g.h + 31) / 32 + 1;
    int b = (g.active + 1) & 1;  // the buffer this ordering table will be drawn into
    uint32_t *head = psx::ptr_as<uint32_t>(work[b]);
    int sx = bg->scrollx, sy = bg->scrolly;
    int ax = sx & ~31, ay = sy & ~31;
    head[1] = 0xE5000000u | uint32_t((ax - sx) & 0x7FF) | uint32_t((ay - sy) & 0x7FF) << 11;
    const GsMAP *map = bg->map;
    int ncw = map->ncellw, nch = map->ncellh;
    int col0 = ((ax >> 5) % ncw + ncw) % ncw, row0 = ((ay >> 5) % nch + nch) % nch;
    uint32_t hi = attr >> 16;
    uint32_t *q = head + 2;
    for (int r = 0; r < rows; r++) {
        const uint16_t *index = map->index + ((row0 + r) % nch) * ncw;
        for (int c = 0; c < cols; c++) {
            uint16_t cell = index[(col0 + c) % ncw];
            if (cell == 0xFFFF) {
                q[0] &= 0x00FFFFFFu;  // empty cell: zero-length packet
            } else {
                const GsCELL *ce = map->base + cell;
                q[0] = (q[0] & 0x00FFFFFFu) | (5u << 24);
                q[1] = 0xE1000000u | ((hi >> 1) & 0x180) | ((hi >> 7) & 0x60) | (ce->tpage & 0x1F);
                q[4] = uint32_t(ce->u) | uint32_t(ce->v) << 8 | uint32_t(ce->cba) << 16;
            }
            q += 6;
        }
    }
    // Closing packet restores the normal drawing offset, then continues with the OT entry.
    if (g.ofs_gpu)
        q[1] = 0xE5000000u | uint32_t((g.center_x + g.buf_x[b]) & 0x7FF) |
               uint32_t((g.center_y + g.buf_y[b]) & 0x7FF) << 11;
    else
        q[1] = 0xE5000000u;
    uint32_t *entry = reinterpret_cast<uint32_t *>(ot->org) + (int(pri & 0xFFFF) - int(ot->offset));
    q[0] = (*entry & 0x00FFFFFFu) | (1u << 24);
    *entry = link_addr(head);
    return 0;
}

int psyq_GsGetTimInfo(const uint32_t *tim, GsIMAGE *im) {
    // 'tim' points just past the TIM ID word: flags, [CLUT block], image block.
    im->pmode = tim[0];
    const uint32_t *blk = tim + 1;
    if (tim[0] & 8) {
        uint32_t clut_len = blk[0];
        const uint16_t *h = reinterpret_cast<const uint16_t *>(blk + 1);
        im->cx = short(h[0]);
        im->cy = short(h[1]);
        im->cw = h[2];
        im->ch = h[3];
        im->clut = const_cast<uint32_t *>(blk + 3);
        blk = reinterpret_cast<const uint32_t *>(reinterpret_cast<const uint8_t *>(blk) + (clut_len & ~3u));
    }
    const uint16_t *h = reinterpret_cast<const uint16_t *>(blk + 1);
    im->px = short(h[0]);
    im->py = short(h[1]);
    im->pw = h[2];
    im->ph = h[3];
    im->pixel = const_cast<uint32_t *>(blk + 3);
    return 0;
}

int psyq_GsSetProjection(int h) {
    g.projection = h;
    gte::set_projection(h);
    return 0;
}

}  // extern "C"

namespace libgs {

int screen_w() { return g.w; }
int screen_h() { return g.h; }
uint32_t swap_count() { return g.swap_count; }
uint32_t *&work_base() { return g.work; }
short pos_x() { return g.pos_x; }
short pos_y() { return g.pos_y; }

}  // namespace libgs
