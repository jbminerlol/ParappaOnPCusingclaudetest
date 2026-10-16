// libgpu: VRAM transfers, display/drawing environments, ordering tables and the debug font.
#include "psyq/libgpu.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gpu/gpu.h"
#include "psx/log.h"
#include "psx/memory.h"

namespace {

gpu::Rect to_rect(const RECT *r) {
    return gpu::Rect{r->x, r->y, r->w, r->h};
}

bool g_display_enabled = false;
DISPENV g_dispenv;

void update_display() {
    gpu::DisplayMode m;
    m.x = g_dispenv.disp.x;
    m.y = g_dispenv.disp.y;
    m.w = g_dispenv.disp.w ? g_dispenv.disp.w : 320;
    m.h = g_dispenv.disp.h ? g_dispenv.disp.h : 240;
    m.rgb24 = g_dispenv.isrgb24;
    m.enabled = g_display_enabled;
    gpu::set_display(m);
}

// ---- debug font (FntLoad / FntOpen / FntPrint / FntFlush) ----

constexpr uint32_t FONT_CLUT = 0x8005CCE4u;   // 16-colour CLUT in the EXE's libgpu data
constexpr uint32_t FONT_IMAGE = 0x8005CEE4u;  // 128x32, 4-bit

struct FontStream {
    int x, y, w, h;
    bool bg;
    int max_chars;
    std::string text;
};

std::vector<FontStream> g_fonts;
uint16_t g_font_tpage, g_font_clut;
int g_dump_font = 0;

uint16_t get_tpage(int tp, int abr, int x, int y) {
    return uint16_t((tp & 3) << 7 | (abr & 3) << 5 | (y & 0x100) >> 4 | (x & 0x3FF) >> 6 | (y & 0x200) << 2);
}

uint16_t get_clut(int x, int y) {
    return uint16_t((y << 6) | ((x >> 4) & 0x3F));
}

void draw_font(FontStream &fs) {
    std::vector<uint32_t> cmd;
    cmd.push_back(0xE1000000u | g_font_tpage | 0x200);  // draw mode: font page, dithering on
    if (fs.bg) {
        cmd.push_back(0x62000000u);  // semi-transparent black box
        cmd.push_back(uint32_t(fs.y) << 16 | uint16_t(fs.x));
        cmd.push_back(uint32_t(fs.h) << 16 | uint16_t(fs.w));
    }
    int cx = fs.x, cy = fs.y;
    uint32_t color = 0x808080;
    int count = 0;
    for (size_t i = 0; i < fs.text.size() && count < fs.max_chars; i++) {
        unsigned char c = fs.text[i];
        if (c == '~' && i + 4 < fs.text.size() + 1 && fs.text[i + 1] == 'c') {
            auto digit = [&](size_t k) { return uint32_t(fs.text[k] - '0') * 0x10; };
            if (i + 4 < fs.text.size() + 1) color = digit(i + 2) | digit(i + 3) << 8 | digit(i + 4) << 16;
            i += 4;
            continue;
        }
        if (c == '\n' || cx + 8 > fs.x + fs.w) {
            cx = fs.x;
            cy += 8;
            if (cy + 8 > fs.y + fs.h) break;
            if (c == '\n') continue;
        }
        if (c == '\t') {
            cx += 32;
            continue;
        }
        if (c >= 'a' && c <= 'z') c -= 0x20;
        if (c > ' ' && c < 0x80) {
            int idx = c - 0x20;
            cmd.push_back(0x74000000u | color);  // 8x8 textured sprite
            cmd.push_back(uint32_t(cy) << 16 | uint16_t(cx));
            cmd.push_back(uint32_t(g_font_clut) << 16 | uint32_t((idx / 16) * 8) << 8 | uint32_t((idx % 16) * 8));
            count++;
        }
        cx += 8;
    }
    gpu::gp0(cmd.data(), int(cmd.size()));
    fs.text.clear();
}

}  // namespace

namespace libgpu {

void add_prim(uint32_t *ot_entry, void *prim) {
    uint32_t *p = static_cast<uint32_t *>(prim);
    p[0] = (p[0] & 0xFF000000u) | (*ot_entry & 0x00FFFFFFu);
    *ot_entry = (*ot_entry & 0xFF000000u) | (psx::addr_of(p) & 0x00FFFFFFu);
}

}  // namespace libgpu

extern "C" {

int psyq_ResetGraph(int mode) {
    if (mode == 0 || mode == 3) gpu::reset();
    return 0;
}

int psyq_SetDispMask(int mask) {
    g_display_enabled = mask != 0;
    update_display();
    return 0;
}

int psyq_DrawSync(int) {
    return 0;  // drawing is synchronous
}

int psyq_ClearImage(const RECT *r, int red, int green, int blue) {
    gpu::clear_image(to_rect(r), uint8_t(red), uint8_t(green), uint8_t(blue));
    return 0;
}

int psyq_LoadImage(const RECT *r, const uint32_t *data) {
    gpu::load_image(to_rect(r), reinterpret_cast<const uint16_t *>(data));
    return 0;
}

int psyq_StoreImage(const RECT *r, uint32_t *data) {
    gpu::store_image(to_rect(r), reinterpret_cast<uint16_t *>(data));
    return 0;
}

int psyq_MoveImage(const RECT *r, int x, int y) {
    gpu::move_image(to_rect(r), x, y);
    return 0;
}

int psyq_LoadClut(const uint32_t *clut, int x, int y) {
    RECT r{short(x), short(y), 256, 1};
    psyq_LoadImage(&r, clut);
    return get_clut(x, y);
}

int psyq_LoadClut2(const uint32_t *clut, int x, int y) {
    RECT r{short(x), short(y), 16, 1};
    psyq_LoadImage(&r, clut);
    return get_clut(x, y);
}

int psyq_GetTPage(int tp, int abr, int x, int y) {
    return get_tpage(tp, abr, x, y);
}

int psyq_SetPolyG4(POLY_G4 *p) {
    p->tag = (p->tag & 0x00FFFFFFu) | (8u << 24);
    p->code = 0x38;
    return 0;
}

int psyq_PutDispEnv(const DISPENV *env) {
    g_dispenv = *env;
    update_display();
    return 0;
}

int psyq_PutDrawEnv(const DRAWENV *env) {
    int x2 = env->clip.x + env->clip.w - 1, y2 = env->clip.y + env->clip.h - 1;
    uint32_t cmd[6] = {
        0xE1000000u | (env->tpage & 0x1FF) | (env->dtd ? 0x200 : 0) | (env->dfe ? 0x400 : 0),
        0xE2000000u | uint32_t((env->tw.w ? (~(env->tw.w - 1) & 0xFF) >> 3 : 0) |
                               (env->tw.h ? ((~(env->tw.h - 1) & 0xFF) >> 3) << 5 : 0) |
                               ((env->tw.x & 0xFF) >> 3) << 10 | ((env->tw.y & 0xFF) >> 3) << 15),
        0xE3000000u | uint32_t(env->clip.x & 0x3FF) | uint32_t(env->clip.y & 0x3FF) << 10,
        0xE4000000u | uint32_t(x2 & 0x3FF) | uint32_t(y2 & 0x3FF) << 10,
        0xE5000000u | uint32_t(env->ofs[0] & 0x7FF) | uint32_t(env->ofs[1] & 0x7FF) << 11,
        0xE6000000u,
    };
    gpu::gp0(cmd, 6);
    if (env->isbg)
        gpu::clear_image(gpu::Rect{env->clip.x, env->clip.y, env->clip.w, env->clip.h}, env->r0, env->g0, env->b0);
    return 0;
}

int psyq_DrawOTag(const uint32_t *ot) {
    gpu::draw_otag(psx::addr_of(ot));
    return 0;
}

int psyq_ClearOTagR(uint32_t *ot, int n) {
    for (int i = n - 1; i > 0; i--) ot[i] = psx::addr_of(&ot[i - 1]) & 0x00FFFFFFu;
    ot[0] = 0x00FFFFFFu;
    return 0;
}

int psyq_FntLoad(int tx, int ty) {
    g_font_clut = uint16_t(psyq_LoadClut2(psx::ptr_as<uint32_t>(FONT_CLUT), tx, ty + 0x80));
    RECT r{short(tx), short(ty), 0x80 / 4, 0x20};
    psyq_LoadImage(&r, psx::ptr_as<uint32_t>(FONT_IMAGE));
    g_font_tpage = get_tpage(0, 0, tx, ty);
    g_fonts.clear();
    return 0;
}

int psyq_FntOpen(int x, int y, int w, int h, int isbg, int n) {
    if (g_fonts.size() >= 8) return -1;
    g_fonts.push_back(FontStream{x, y, w, h, isbg != 0, n, {}});
    return int(g_fonts.size()) - 1;
}

int psyq_SetDumpFnt(int id) {
    if (id >= 0 && id < int(g_fonts.size())) g_dump_font = id;
    return 0;
}

int psyq_FntPrint(const char *fmt, ...) {
    if (g_fonts.empty()) return 0;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_fonts[g_dump_font].text += buf;
    return 0;
}

int psyq_FntFlush(int id) {
    if (id < 0 || id >= int(g_fonts.size())) id = g_dump_font;
    if (id < int(g_fonts.size())) draw_font(g_fonts[id]);
    return 0;
}

}  // extern "C"
