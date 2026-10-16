#include "host/host.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "gpu/gpu.h"
#include "host/controls.h"
#include "host/settings.h"
#include "psx/log.h"
#include "psx/system.h"

namespace host {
namespace {

SDL_Window *g_window;
SDL_GLContext g_gl;
SDL_AudioStream *g_audio;
int g_device_frames;  // device buffer, in AUDIO_RATE frames
SDL_Gamepad *g_pads[2];
bool g_screenshot_key;

// Player number on the pad itself: DualSense player LEDs, DualShock 4 / DualSense light bar colour.
void show_player(int port) {
    if (!g_pads[port]) return;
    SDL_SetGamepadPlayerIndex(g_pads[port], port);
    const char *type = SDL_GetGamepadStringForType(SDL_GetGamepadType(g_pads[port]));
    LOGI("Gamepad on controller port %d: %s (%s)", port + 1, SDL_GetGamepadName(g_pads[port]),
         type ? type : "unknown");
}

void open_gamepad(SDL_JoystickID id) {
    for (SDL_Gamepad *slot : g_pads)
        if (slot && SDL_GetGamepadID(slot) == id) return;
    for (int port = 0; port < 2; port++) {
        if (g_pads[port]) continue;
        g_pads[port] = SDL_OpenGamepad(id);
        if (g_pads[port])
            show_player(port);
        else
            LOGW("Could not open gamepad %u: %s", unsigned(id), SDL_GetError());
        return;
    }
}

void close_gamepad(SDL_JoystickID id) {
    for (SDL_Gamepad *&slot : g_pads) {
        if (slot && SDL_GetGamepadID(slot) == id) {
            LOGI("Gamepad disconnected: %s", SDL_GetGamepadName(slot));
            SDL_CloseGamepad(slot);
            slot = nullptr;
        }
    }
    // The game is played on port 1: a pad left on port 2 moves up.
    if (!g_pads[0] && g_pads[1]) {
        std::swap(g_pads[0], g_pads[1]);
        show_player(0);
    }
}

bool test_mode() {
    static const bool on = SDL_getenv("PARAPPA_AUTOINPUT") != nullptr;
    return on;
}

}  // namespace

std::string user_dir() {
    char *pref = SDL_GetPrefPath("PaRappaPort", "");
    std::string dir = pref ? pref : "";
    SDL_free(pref);
    while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
    return dir.empty() ? std::string() : dir + "\\";
}

bool init(int window_scale) {
    // DualShock 4 and DualSense through SDL's own drivers (USB and Bluetooth): every button including
    // the touchpad click, and the light bar / player LEDs.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        LOGE("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Scripted test runs (PARAPPA_AUTOINPUT) ignore the real keyboard/gamepads and never take the
    // focus, so whoever is using the desktop meanwhile cannot affect them.
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (test_mode()) flags |= SDL_WINDOW_NOT_FOCUSABLE;
    g_window = SDL_CreateWindow("PaRappa the Rapper", 320 * window_scale, 240 * window_scale, flags);
    if (!g_window) {
        LOGE("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) {
        LOGE("OpenGL 3.3 context creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(0);  // frame pacing follows the audio clock

    SDL_AudioSpec spec{SDL_AUDIO_S16, 2, AUDIO_RATE};
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (g_audio) {
        SDL_ResumeAudioStreamDevice(g_audio);
        SDL_AudioSpec dev;
        int dev_frames = 0;
        if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &dev_frames) && dev.freq > 0) {
            g_device_frames = int(int64_t(dev_frames) * AUDIO_RATE / dev.freq);
            LOGI("Audio output: %d Hz, %d-frame device buffer", dev.freq, dev_frames);
        }
    } else {
        LOGW("No audio output: %s", SDL_GetError());
    }
    return true;
}

std::string pick_disc_image(const std::string &title) {
    static const SDL_DialogFileFilter filters[] = {
        {"PlayStation disc image (*.cue, *.bin)", "cue;bin"},
        {"All files", "*"},
    };
    struct Result {
        bool done = false;
        std::string path;
    } result;
    SDL_SetWindowTitle(g_window, title.c_str());
    SDL_ShowOpenFileDialog(
        [](void *user, const char *const *files, int) {
            auto *r = static_cast<Result *>(user);
            if (files && files[0]) r->path = files[0];
            r->done = true;
        },
        &result, g_window, filters, 2, nullptr, false);
    while (!result.done) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT) std::exit(0);
        SDL_Delay(10);
    }
    SDL_SetWindowTitle(g_window, "PaRappa the Rapper");
    return result.path;
}

void message_box(const std::string &title, const std::string &text) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), g_window);
}

void info_box(const std::string &title, const std::string &text) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(), g_window);
}

void shutdown() {
    if (g_audio) SDL_DestroyAudioStream(g_audio);
    if (g_gl) SDL_GL_DestroyContext(g_gl);
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
}

void poll_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            LOGI("Window closed");
            shutdown();
            std::exit(0);
        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_gamepad(e.gdevice.which);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.scancode == SDL_SCANCODE_F12) g_screenshot_key = true;
            if (e.key.scancode == SDL_SCANCODE_F11) {
                bool full = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN;
                SDL_SetWindowFullscreen(g_window, !full);
            }
            break;
        default:
            break;
        }
    }
}

// Test hook: PARAPPA_AUTOINPUT="600:start,900:cross" presses a button for a few vblanks at the given
// vblank numbers (port 1), so input-dependent paths can be reproduced without a person playing.
uint16_t auto_input() {
    static std::vector<std::pair<uint32_t, uint16_t>> script;
    static bool parsed;
    if (!parsed) {
        parsed = true;
        if (const char *env = SDL_getenv("PARAPPA_AUTOINPUT")) {
            static const std::pair<const char *, uint16_t> names[] = {
                {"up", PAD_UP}, {"down", PAD_DOWN}, {"left", PAD_LEFT}, {"right", PAD_RIGHT},
                {"triangle", PAD_TRIANGLE}, {"circle", PAD_CIRCLE}, {"cross", PAD_CROSS}, {"square", PAD_SQUARE},
                {"l1", PAD_L1}, {"r1", PAD_R1}, {"l2", PAD_L2}, {"r2", PAD_R2}, {"start", PAD_START},
                {"select", PAD_SELECT}};
            std::string spec = env;
            size_t pos = 0;
            while (pos < spec.size()) {
                size_t end = spec.find(',', pos);
                std::string item = spec.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
                size_t colon = item.find(':');
                if (colon != std::string::npos)
                    for (const auto &n : names)
                        if (item.substr(colon + 1) == n.first)
                            script.emplace_back(uint32_t(std::strtoul(item.c_str(), nullptr, 10)), n.second);
                if (end == std::string::npos) break;
                pos = end + 1;
            }
            LOGI("Auto input: %zu presses", script.size());
        }
    }
    uint16_t bits = 0;
    uint32_t now = psx::vblank_count();
    for (const auto &e : script)
        if (now >= e.first && now < e.first + 6) bits |= e.second;
    return bits;
}

uint16_t pad_buttons(int port) {
    if (test_mode()) return port == 0 ? uint16_t(auto_input() | psx::test_autoplay()) : 0;
    uint16_t bits = port == 0 ? uint16_t(controls::keyboard_buttons() | auto_input()) : 0;
    if (SDL_Gamepad *pad = g_pads[port & 1]) bits |= controls::gamepad_buttons(pad);
    return bits;
}

bool save_screenshot(const std::string &path);

// Test hook: PARAPPA_SCREENSHOTS="300,900" saves shot_<vblank>.png at those vblanks; F12 saves one.
void maybe_screenshot(bool requested) {
    static std::vector<uint32_t> at;
    static bool parsed;
    if (!parsed) {
        parsed = true;
        if (const char *env = SDL_getenv("PARAPPA_SCREENSHOTS"))
            for (const char *p = env; *p;) {
                char *end;
                at.push_back(uint32_t(std::strtoul(p, &end, 10)));
                p = *end ? end + 1 : end;
            }
    }
    uint32_t now = psx::vblank_count();
    for (uint32_t v : at) requested |= v == now;
    if (requested) save_screenshot(user_dir() + "shot_" + std::to_string(now) + ".png");
}

void present() {
    int w, h;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    gpu::present(w, h);
    maybe_screenshot(g_screenshot_key);
    g_screenshot_key = false;
    SDL_GL_SwapWindow(g_window);
}

void audio_push(const int16_t *samples, int frames) {
    if (g_audio) SDL_PutAudioStreamData(g_audio, samples, frames * 4);
}

int audio_queued_frames() {
    return g_audio ? SDL_GetAudioStreamQueued(g_audio) / 4 : 0;
}

int audio_latency_frames() {
    static const int offset = std::atoi(settings::get("audio_offset_ms", "0").c_str()) * AUDIO_RATE / 1000;
    if (!g_audio) return 0;
    return audio_queued_frames() + g_device_frames + offset;
}

}  // namespace host
