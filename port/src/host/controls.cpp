#include "host/controls.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include "host/host.h"
#include "psx/log.h"

namespace controls {
namespace {

struct Button {
    const char *name;
    uint16_t bit;
    const char *keys;     // default keyboard binding (SDL key names)
    const char *gamepad;  // default gamepad binding (SDL gamepad button / axis names)
};

// Gamepad defaults use PlayStation names: a DualShock 4 / DualSense maps button for button
// (Options = Start; Share / Create or a touchpad click = Select). Other pads map by position.
const Button kButtons[] = {
    {"up", host::PAD_UP, "Up", "dpup, lefty-"},
    {"down", host::PAD_DOWN, "Down", "dpdown, lefty+"},
    {"left", host::PAD_LEFT, "Left", "dpleft, leftx-"},
    {"right", host::PAD_RIGHT, "Right", "dpright, leftx+"},
    {"triangle", host::PAD_TRIANGLE, "W", "triangle"},
    {"circle", host::PAD_CIRCLE, "D", "circle"},
    {"cross", host::PAD_CROSS, "S", "cross"},
    {"square", host::PAD_SQUARE, "A", "square"},
    {"l1", host::PAD_L1, "Q", "l1"},
    {"r1", host::PAD_R1, "E", "r1"},
    {"l2", host::PAD_L2, "1", "l2"},
    {"r2", host::PAD_R2, "3", "r2"},
    {"start", host::PAD_START, "Return", "options"},
    {"select", host::PAD_SELECT, "Right Shift, Backspace", "share, touchpad"},
};

// PlayStation names for gamepad inputs -> SDL names. SDL reports face buttons by position, and on
// PlayStation pads south is cross, east circle, west square and north triangle.
const struct {
    const char *name, *sdl;
} kPadAliases[] = {
    {"cross", "a"}, {"circle", "b"}, {"square", "x"}, {"triangle", "y"},
    {"l1", "leftshoulder"}, {"r1", "rightshoulder"}, {"l2", "lefttrigger"}, {"r2", "righttrigger"},
    {"l3", "leftstick"}, {"r3", "rightstick"}, {"options", "start"}, {"share", "back"},
    {"create", "back"}, {"select", "back"}, {"ps", "guide"}, {"mute", "misc1"},
};

// First line of the controls.ini this version writes. A file starting with an older port's header
// holds that port's defaults, so it is replaced (and kept as controls.old.ini).
const char kHeader[] = "# PaRappa the Rapper port: controller bindings (v2).";
const char kOldHeader[] = "# PaRappa the Rapper port: controller bindings.";

struct KeyBinding {
    SDL_Scancode key;
    uint16_t bit;
};
struct PadBinding {
    SDL_GamepadButton button = SDL_GAMEPAD_BUTTON_INVALID;
    SDL_GamepadAxis axis = SDL_GAMEPAD_AXIS_INVALID;  // triggers / sticks (pressed past half travel)
    int direction = 1;                                 // axis: +1 or -1 ("leftx-" binds the left half)
    uint16_t bit;
};

std::vector<KeyBinding> g_keys;
std::vector<PadBinding> g_pad;

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void write_defaults(const std::string &path) {
    std::ofstream f(path);
    if (!f) {
        LOGW("Could not create %s", path.c_str());
        return;
    }
    f << kHeader << "\n"
         "# Each PS1 button takes a comma-separated list of names.\n"
         "#  [keyboard]  SDL key names, e.g. A, Space, Left Shift, Keypad 8\n"
         "#              (https://wiki.libsdl.org/SDL3/SDL_Scancode)\n"
         "#  [gamepad]   cross circle square triangle l1 r1 l2 r2 l3 r3 options share touchpad ps mute\n"
         "#              dpup dpdown dpleft dpright, a stick direction: leftx- leftx+ lefty- lefty+\n"
         "#              rightx- rightx+ righty- righty+, or SDL names (a b x y back start ...).\n"
         "#              DualShock 4 / DualSense buttons match by name (Create on a DualSense is\n"
         "#              share); other pads by position (cross = bottom face button).\n"
         "# Delete this file to restore the defaults.\n\n[keyboard]\n";
    for (const Button &b : kButtons) f << b.name << " = " << b.keys << "\n";
    f << "\n[gamepad]\n";
    for (const Button &b : kButtons) f << b.name << " = " << b.gamepad << "\n";
}

void bind_key(const std::string &name, uint16_t bit) {
    SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    if (sc == SDL_SCANCODE_UNKNOWN)
        LOGW("controls: unknown key '%s'", name.c_str());
    else
        g_keys.push_back(KeyBinding{sc, bit});
}

void bind_pad(std::string name, uint16_t bit) {
    PadBinding b;
    b.bit = bit;
    int dir = 1;
    if (!name.empty() && (name.back() == '+' || name.back() == '-')) {
        dir = name.back() == '-' ? -1 : 1;
        name.pop_back();
    }
    for (const auto &a : kPadAliases)
        if (name == a.name) name = a.sdl;
    b.button = SDL_GetGamepadButtonFromString(name.c_str());
    if (b.button == SDL_GAMEPAD_BUTTON_INVALID) {
        b.axis = SDL_GetGamepadAxisFromString(name.c_str());
        b.direction = dir;
        if (b.axis == SDL_GAMEPAD_AXIS_INVALID) {
            LOGW("controls: unknown gamepad input '%s'", name.c_str());
            return;
        }
    }
    g_pad.push_back(b);
}

}  // namespace

std::string default_path() {
    return host::user_dir() + "controls.ini";
}

void load(const std::string &path) {
    std::ifstream probe(path);
    std::string first;
    bool exists = bool(probe) && (std::getline(probe, first), true);
    probe.close();
    if (exists && trim(first) == kOldHeader) {
        std::string old = path.substr(0, path.rfind('.')) + ".old.ini";
        std::remove(old.c_str());
        if (std::rename(path.c_str(), old.c_str()) == 0) {
            LOGI("Controls file from an older version kept as %s", old.c_str());
            exists = false;
        }
    }
    if (!exists) {
        write_defaults(path);
        LOGI("Wrote default controls to %s", path.c_str());
    }
    std::ifstream f(path);
    g_keys.clear();
    g_pad.clear();
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        if (line.front() == '[') {
            section = lower(trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string button = lower(trim(line.substr(0, eq)));
        const Button *target = nullptr;
        for (const Button &b : kButtons)
            if (button == b.name) target = &b;
        if (!target) {
            LOGW("controls: unknown button '%s'", button.c_str());
            continue;
        }
        std::stringstream list(line.substr(eq + 1));
        std::string item;
        while (std::getline(list, item, ',')) {
            item = trim(item);
            if (item.empty()) continue;
            if (section == "keyboard") bind_key(item, target->bit);
            else if (section == "gamepad") bind_pad(lower(item), target->bit);
        }
    }
    LOGI("Controls from %s: %zu key and %zu gamepad bindings", path.c_str(), g_keys.size(), g_pad.size());
}

uint16_t keyboard_buttons() {
    const bool *keys = SDL_GetKeyboardState(nullptr);
    uint16_t bits = 0;
    for (const KeyBinding &k : g_keys)
        if (keys[k.key]) bits |= k.bit;
    return bits;
}

uint16_t gamepad_buttons(void *sdl_gamepad) {
    SDL_Gamepad *pad = static_cast<SDL_Gamepad *>(sdl_gamepad);
    uint16_t bits = 0;
    for (const PadBinding &b : g_pad) {
        if (b.button != SDL_GAMEPAD_BUTTON_INVALID) {
            if (SDL_GetGamepadButton(pad, b.button)) bits |= b.bit;
        } else if (SDL_GetGamepadAxis(pad, b.axis) * b.direction > 16000) {
            bits |= b.bit;
        }
    }
    return bits;
}

}  // namespace controls
