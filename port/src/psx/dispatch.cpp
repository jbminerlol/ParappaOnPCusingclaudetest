#include <cstdarg>
#include <cstdio>
// MIPS address -> native function resolution for indirect calls in game code.
#include <algorithm>
#include <cstring>

#include "dispatch.h"
#include "psx/log.h"
#include "psx/memory.h"

namespace {

const PsxOverlay *g_overlay;  // overlay identified at the last overlay-region call

psx_fn_t find(const PsxFnEntry *fns, int n, uint32_t addr) {
    const PsxFnEntry *end = fns + n;
    const PsxFnEntry *e = std::lower_bound(fns, end, addr, [](const PsxFnEntry &a, uint32_t v) { return a.addr < v; });
    return (e != end && e->addr == addr) ? e->fn : nullptr;
}

bool overlay_loaded(const PsxOverlay &o) {
    return std::memcmp(psx::ptr(o.fingerprint_addr), o.fingerprint, o.fingerprint_len) == 0;
}

// Library entry points Ghidra did not split out as functions but the game calls by address.
extern "C" int psyq_bcmp(const void *a, const void *b, int n);
const PsxFnEntry g_extra_fns[] = {
    {0x800488E4u, reinterpret_cast<psx_fn_t>(psyq_bcmp)},  // bcmp (save data comparison)
};

}  // namespace

namespace psx {

// The stage overlay currently in RAM (identified by its code bytes), or nullptr.
const PsxOverlay *current_overlay() {
    if (g_overlay && overlay_loaded(*g_overlay)) return g_overlay;
    g_overlay = nullptr;
    for (int i = 0; i < psx_overlay_count; i++) {
        if (overlay_loaded(psx_overlays[i])) {
            g_overlay = &psx_overlays[i];
            LOGI("Overlay %s is loaded", g_overlay->file);
            break;
        }
    }
    return g_overlay;
}

}  // namespace psx

extern "C" psx_fn_t psx_resolve(uint32_t addr) {
    addr = (addr & 0x1FFFFFFFu) | 0x80000000u;
    if (addr >= psx::OVERLAY_BASE) {
        if (const PsxOverlay *o = psx::current_overlay())
            if (psx_fn_t fn = find(o->fns, o->fn_count, addr)) return fn;
    } else if (psx_fn_t fn = find(g_extra_fns, int(sizeof(g_extra_fns) / sizeof(g_extra_fns[0])), addr)) {
        return fn;
    } else if (psx_fn_t fn = find(psx_main_fns, psx_main_fn_count, addr)) {
        return fn;
    }
    psx::fatal("Indirect call to unknown function %08X", addr);
}

extern "C" void psx_trap(int code) {
    LOGW("MIPS break %d (division by zero?)", code);
}

extern "C" void psx_debug(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOGD("game: %s", buf);
}

extern "C" void psx_unimplemented(const char *name) {
    psx::fatal("Game function %s was not decompiled (add src/game/fixed/%s.c)", name, name);
}
