// Controller bindings loaded from controls.ini (keyboard keys and gamepad buttons/axes per PS1
// button). The file is created with the default layout if it does not exist.
#pragma once
#include <cstdint>
#include <string>

namespace controls {

// Loads 'path' (writing the defaults there first if it is missing). Unknown names are logged.
void load(const std::string &path);

// Default location: <user app-data>/PaRappaPort/controls.ini
std::string default_path();

// Held buttons (host::PadButton bits) from the keyboard (port 1 only) and a gamepad.
uint16_t keyboard_buttons();
uint16_t gamepad_buttons(void *sdl_gamepad);

}  // namespace controls
