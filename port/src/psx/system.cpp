#include "psx/system.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "dispatch.h"
#include "ghidra.h"
#include "host/host.h"
#include "psx/log.h"
#include "psx/memory.h"

namespace psx {
namespace {

uint32_t g_vblanks;
uint32_t g_vsync_callback;
std::vector<void (*)()> g_tasks;
uint64_t g_last_frame_ns;
bool g_in_vblank;

void pace() {
    // Keep about two frames of audio queued: the sound device sets the frame rate, which keeps
    // XA music and rhythm timing locked together.
    const int target = int(host::AUDIO_RATE / VBLANK_HZ * 2.0);
    uint64_t start = SDL_GetTicksNS();
    while (host::audio_queued_frames() > target) {
        SDL_DelayNS(500000);
        if (SDL_GetTicksNS() - start > 100000000) break;  // audio stalled: fall back to timer pacing
    }
    // Without audio output (or if it stalled) pace on the clock instead.
    if (host::audio_queued_frames() <= 0) {
        const uint64_t frame_ns = uint64_t(1e9 / VBLANK_HZ);
        uint64_t now = SDL_GetTicksNS();
        if (g_last_frame_ns && now < g_last_frame_ns + frame_ns) SDL_DelayNS(g_last_frame_ns + frame_ns - now);
    }
    g_last_frame_ns = SDL_GetTicksNS();
}

}  // namespace

void log_main_thread();

// Logs when the game stops reaching vertical blanks (a busy-wait on something only an interrupt
// would have changed, which never happens in the native port).
int watchdog(void *) {
    uint32_t last = 0;
    int stalled = 0;
    while (true) {
        SDL_Delay(1000);
        uint32_t now = g_vblanks;
        stalled = (now == last) ? stalled + 1 : 0;
        if (stalled == 3) {
            LOGW("Game has not reached a vblank for 3 seconds (stuck in a wait loop?)");
            log_main_thread();
        }
        last = now;
    }
    return 0;
}

void system_init() {
    g_vblanks = 0;
    g_vsync_callback = 0;
    SDL_DetachThread(SDL_CreateThread(watchdog, "watchdog", nullptr));
}

uint32_t vblank_count() {
    return g_vblanks;
}

// Test hook: PARAPPA_TEST_RANK=0..3 holds the rap rank (COOL/GOOD/BAD/AWFUL) during stages, so
// scripted runs can clear stages (or fail them) without anyone playing.
const PsxOverlay *current_overlay();  // dispatch.cpp

static void test_rank_hook() {
    static const int rank = [] {
        const char *e = SDL_getenv("PARAPPA_TEST_RANK");
        return e ? SDL_atoi(e) : -1;
    }();
    if (rank < 0) return;
    const PsxOverlay *o = current_overlay();
    if (o && o->index != 0) *ptr_as<int16_t>(0x801C368Eu) = int16_t(rank);  // stage state + 0x4e
}

// Debug log of the rap rank (stage state + 0x4e: 0 COOL, 1 GOOD, 2 BAD, 3 AWFUL) as it changes.
static void log_rank() {
    static int last = -1;
    const PsxOverlay *o = current_overlay();
    if (!o || o->index == 0) {
        last = -1;
        return;
    }
    int rank = *ptr_as<int16_t>(0x801C368Eu);
    if (rank != last) LOGD("Rank %d at song tick %d", rank, *ptr_as<int32_t>(0x801C364Cu));
    last = rank;
}

// Test hook: PARAPPA_TEST_UNLOCK=1 marks stages 1-6 as cleared with COOL (progress status 3 at
// 0x80092F1D + stage), which unlocks the bonus stage (scene 8, COMOD9) in the stage select.
static void test_unlock_hook() {
    static const bool on = SDL_getenv("PARAPPA_TEST_UNLOCK") != nullptr;
    if (!on) return;
    for (uint32_t i = 0; i < 6; i++) {
        uint8_t *status = ptr_as<uint8_t>(0x80092F1Du + i);
        if (*status < 3) *status = 3;
    }
}

// Test hooks for checking rhythm judgement through the real controller path. Stages record the
// player's presses as {song tick, buttons} pairs at 0x8008EEF8 (count at 0x800901BC); the attract
// demo plays back a recording stored there.
// PARAPPA_DUMP_REPLAY=1 writes the demo's recording to replay_<overlay>.txt in the user folder.
static void dump_replay_hook() {
    static const bool on = SDL_getenv("PARAPPA_DUMP_REPLAY") != nullptr;
    if (!on) return;
    const PsxOverlay *o = current_overlay();
    if (!o || o->index == 0 || *ptr_as<int16_t>(0x800916D0u) != 1) return;  // demo mode only
    int count = *ptr_as<int32_t>(0x800901BCu);
    static int dumped_count = -1, dumped_index = -1;
    if (count <= 0 || count > 600 || (count == dumped_count && o->index == dumped_index)) return;
    dumped_count = count;
    dumped_index = o->index;
    std::string path = host::user_dir() + "replay_" + o->file + ".txt";
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f) return;
    for (int i = 0; i < count; i++)
        std::fprintf(f, "%d %X\n", *ptr_as<int32_t>(0x8008EEF8u + i * 8), *ptr_as<uint32_t>(0x8008EEFCu + i * 8));
    std::fclose(f);
    LOGI("Replay data (%d presses) written to %s", count, path.c_str());
}

// PARAPPA_AUTOPLAY=<replay_<overlay>.txt> presses the recorded buttons on controller 1 when the
// song clock (stage state + 0x0C) reaches each tick, like a player with perfect timing by ear.
uint16_t test_autoplay() {
    static std::vector<std::pair<int32_t, uint16_t>> presses;
    static std::string overlay;
    static bool loaded;
    if (!loaded) {
        loaded = true;
        if (const char *path = SDL_getenv("PARAPPA_AUTOPLAY")) {
            if (FILE *f = std::fopen(path, "r")) {
                int tick;
                unsigned buttons;
                while (std::fscanf(f, "%d %X", &tick, &buttons) == 2) presses.emplace_back(tick, uint16_t(buttons));
                std::fclose(f);
            }
            std::string name = path;
            size_t a = name.find("replay_"), b = name.rfind(".txt");
            if (a != std::string::npos && b != std::string::npos && b > a + 7) overlay = name.substr(a + 7, b - a - 7);
            LOGI("Autoplay: %zu presses for %s", presses.size(), overlay.c_str());
        }
    }
    if (presses.empty()) return 0;
    const PsxOverlay *o = current_overlay();
    if (!o || overlay != o->file) return 0;
    static size_t next;
    static int32_t last_clock;
    static uint16_t held;
    static uint32_t held_until;
    int32_t clock = *ptr_as<int32_t>(0x801C364Cu);
    if (clock < last_clock) next = 0;  // the song started again
    last_clock = clock;
    while (next < presses.size() && presses[next].first <= clock) {
        if (clock - presses[next].first < 24) {  // within a 16th note: press it now
            held = presses[next].second;
            held_until = g_vblanks + 3;
        }
        next++;
    }
    return g_vblanks < held_until ? held : 0;
}

void vblank() {
    if (g_in_vblank) return;  // a callback waiting on VSync must not recurse
    g_in_vblank = true;
    g_vblanks++;
    host::poll_events();
    test_rank_hook();
    log_rank();
    test_unlock_hook();
    dump_replay_hook();
    for (auto task : g_tasks) task();
    if (g_vsync_callback) call_game(g_vsync_callback);
    host::present();
    pace();
    g_in_vblank = false;
}

bool in_vblank() {
    return g_in_vblank;
}

void add_vblank_task(void (*task)()) {
    g_tasks.push_back(task);
}

void set_vsync_callback(uint32_t func) {
    g_vsync_callback = func;
}

uint32_t call_game(uint32_t func, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    // Game functions are cdecl; pass the four MIPS argument registers.
    auto fn = reinterpret_cast<int (*)(uint32_t, uint32_t, uint32_t, uint32_t)>(psx_resolve(func));
    return uint32_t(fn(a0, a1, a2, a3));
}

}  // namespace psx
