// libetc: vertical sync, interrupt callbacks and controllers.
#include <cstdint>

#include "host/host.h"
#include "psx/log.h"
#include "psx/system.h"

namespace {
uint32_t g_last_vsync;    // vblank count at the previous VSync(0/n)
bool g_pad_initialized;
}  // namespace

extern "C" {

// VSync(0): wait for the next vertical blank. VSync(n>1): wait until n blanks have passed since the
// previous VSync call. VSync(1): no wait. VSync(-1): vertical blank counter. Others return the
// horizontal blank count since the previous call (one frame of NTSC lines).
int psyq_VSync(int mode) {
    if (mode < 0) return int(psx::vblank_count());
    if (mode == 1) return 0;
    uint32_t target = g_last_vsync + (mode > 1 ? uint32_t(mode) - 1 : 0);
    while (int32_t(psx::vblank_count() - target) < 0) psx::vblank();
    psx::vblank();
    g_last_vsync = psx::vblank_count();
    return 263;
}

int psyq_VSyncCallback(uint32_t func) {
    psx::set_vsync_callback(func);
    return 0;
}

int psyq_ResetCallback() {
    psx::set_vsync_callback(0);
    return 0;
}

int psyq_StopCallback() {
    psx::set_vsync_callback(0);
    return 0;
}

int psyq_PadInit(int) {
    g_pad_initialized = true;
    return 0;
}

// Low 16 bits: port 1, high 16 bits: port 2 (libetc button layout, pressed = 1).
// The pad is sampled by the vblank interrupt, so the game waits for a button release with a bare
// "while (PadRead(1)) ;" loop. Natively nothing else would advance time inside such a loop, so
// repeated polls within one vblank let the next vblank happen.
uint32_t psyq_PadRead(int) {
    if (!g_pad_initialized) return 0;
    static uint32_t last_vblank, polls;
    uint32_t now = psx::vblank_count();
    if (now != last_vblank) {
        last_vblank = now;
        polls = 0;
    } else if (++polls >= 16) {
        psx::vblank();
    }
    return host::pad_buttons(0) | (uint32_t(host::pad_buttons(1)) << 16);
}

int psyq_ChangeClearPAD(int) {
    return 0;
}

// Interrupts are delivered at vblank boundaries only, so critical sections need no work.
int psyq_EnterCriticalSection() {
    return 1;
}

int psyq_ExitCriticalSection() {
    return 0;
}

int psyq_SetMem(int megabytes) {
    if (megabytes != 2) LOGW("SetMem(%d): the port always provides 2 MB", megabytes);
    return 0;
}

int psyq___main() {
    return 0;
}

}  // extern "C"
