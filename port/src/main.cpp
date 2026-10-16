// PaRappa the Rapper PC port: boots the decompiled game on the native PsyQ runtime.
//
//   parappa [--disc image.cue|.bin] [--pick-disc] [--scale N] [--window N] [--controls file] [--debug]
// Without --disc the image chosen last time is used (settings.ini); the first run asks for it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "audio/spu.h"
#include "gpu/gpu.h"
#include "host/controls.h"
#include "host/host.h"
#include "host/settings.h"
#include "psx/disc.h"
#include "psx/log.h"
#include "psx/memory.h"
#include "psx/system.h"
#include "psyq/libcd.h"

extern "C" int game_main();

namespace psx {
void install_crash_handler();
void install_watchpoint();
}
namespace card {
void set_dir(const std::filesystem::path &dir);  // libapi.cpp
}

// A PaRappa disc image (.cue sheet, else .bin) in the executable's folder.
static std::string find_disc_next_to_exe() {
    namespace fs = std::filesystem;
    const char *base = SDL_GetBasePath();
    if (!base) return {};
    std::error_code ec;
    for (const char *ext : {".cue", ".bin"}) {
        for (const auto &e : fs::directory_iterator(fs::path(std::u8string(reinterpret_cast<const char8_t *>(base))), ec)) {
            std::string name = e.path().extension().string();
            for (char &c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (!e.is_regular_file() || name != ext) continue;
            std::u8string u8 = e.path().u8string();
            std::string path(u8.begin(), u8.end());
            bool ok = psx::g_disc.open(path) && psx::g_disc.is_parappa();
            psx::g_disc.close();
            if (ok) {
                LOGI("Found disc image next to the executable: %s", path.c_str());
                return path;
            }
        }
    }
    return {};
}

int main(int argc, char **argv) {
    // Child process of the save folder check (host/saves.cpp): no window, no log.
    if (argc == 3 && !std::strcmp(argv[1], "--write-test")) return host::write_test(argv[2]);

    std::string disc, controls_path;
    bool pick = false;
    int scale = 2, window = 3;
    psx::LogLevel level = psx::LogLevel::Info;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--disc") && i + 1 < argc) disc = argv[++i];
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--window") && i + 1 < argc) window = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--controls") && i + 1 < argc) controls_path = argv[++i];
        else if (!std::strcmp(argv[i], "--pick-disc")) pick = true;
        else if (!std::strcmp(argv[i], "--debug")) level = psx::LogLevel::Debug;
    }

    psx::log_init((host::user_dir() + "parappa.log").c_str());
    psx::log_set_level(level);
    psx::install_crash_handler();
    psx::memory_init();
    psx::install_watchpoint();

    if (!host::init(window)) psx::fatal("Could not create the window: %s", SDL_GetError());
    settings::load();
    card::set_dir(host::choose_save_dir());
    controls::load(controls_path.empty() ? controls::default_path() : controls_path);

    // The disc image: --disc, else a PaRappa image next to the executable, else the one picked last
    // time, else ask (as emulators do).
    if (disc.empty() && !pick) disc = find_disc_next_to_exe();
    if (disc.empty()) disc = settings::get("disc");
    if (disc.empty()) pick = true;
    while (true) {
        if (pick) {
            disc = host::pick_disc_image("Select your PaRappa the Rapper disc image (.cue / .bin)");
            if (disc.empty()) return 0;
        }
        if (!psx::g_disc.open(disc)) {
            host::message_box("PaRappa the Rapper", "Could not open the disc image:\n" + disc);
        } else if (!psx::g_disc.is_parappa()) {
            host::message_box("PaRappa the Rapper", "This is not PaRappa the Rapper (US, SCUS-94183):\n" + disc);
        } else {
            break;
        }
        pick = true;
    }
    settings::set("disc", disc);
    if (!gpu::init(scale)) psx::fatal("OpenGL 3.3 is required");

    psx::system_init();
    cd::init();   // CD before sound: XA decoded in a vblank is mixed in the same vblank
    spu::init();

    // Boot the executable named in SYSTEM.CNF, read from the disc like the BIOS would.
    psx::DiscFile exe;
    if (!psx::g_disc.find("\\SCUS_941.83;1", &exe)) psx::fatal("SCUS_941.83 not found on the disc");
    std::vector<uint8_t> image((exe.size + psx::DATA_SECTOR - 1) / psx::DATA_SECTOR * psx::DATA_SECTOR);
    for (uint32_t i = 0; i < image.size() / psx::DATA_SECTOR; i++)
        psx::g_disc.read_data(exe.lba + i, &image[i * psx::DATA_SECTOR]);
    psx::load_exe(image.data(), exe.size);

    LOGI("Starting game");
    game_main();
    LOGI("game main() returned");
    host::shutdown();
    return 0;
}
