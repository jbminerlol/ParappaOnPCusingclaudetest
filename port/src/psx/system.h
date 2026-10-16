// Emulated time. The game runs natively between VSync() calls, so time only advances at vertical
// blanks: each one ticks the devices (CD, sound), runs interrupt-time callbacks, shows the frame
// and paces against the audio clock (NTSC, 59.94 Hz).
#pragma once
#include <cstdint>

namespace psx {

constexpr double VBLANK_HZ = 59.94;

void system_init();

// Vertical blanks since start-up.
uint32_t vblank_count();

// Runs one vertical blank interval.
void vblank();

// True while a vblank is being processed (vblank() cannot advance time from inside one).
bool in_vblank();

// Device work done every vblank, in registration order (before game callbacks).
void add_vblank_task(void (*task)());

// Game VSyncCallback (MIPS address, 0 = none).
void set_vsync_callback(uint32_t func);

// Test hook (PARAPPA_AUTOPLAY): controller 1 buttons replaying a recorded performance.
uint16_t test_autoplay();

// Calls a game function by MIPS address (callbacks registered with library functions).
uint32_t call_game(uint32_t func, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);

}  // namespace psx
