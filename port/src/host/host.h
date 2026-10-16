// Host platform layer: SDL3 window with an OpenGL context, input, and audio output.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace host {

// libetc button bits (PadRead() layout, active high here).
enum PadButton : uint16_t {
    PAD_L2 = 1 << 0, PAD_R2 = 1 << 1, PAD_L1 = 1 << 2, PAD_R1 = 1 << 3,
    PAD_TRIANGLE = 1 << 4, PAD_CIRCLE = 1 << 5, PAD_CROSS = 1 << 6, PAD_SQUARE = 1 << 7,
    PAD_SELECT = 1 << 8, PAD_START = 1 << 11,
    PAD_UP = 1 << 12, PAD_RIGHT = 1 << 13, PAD_DOWN = 1 << 14, PAD_LEFT = 1 << 15,
};

constexpr int AUDIO_RATE = 44100;

// Per-user folder for the log, settings, screenshots (%APPDATA%\PaRappaPort\), with trailing slash.
std::string user_dir();

// Memory card folder: memcard\bu00 next to the executable, or in user_dir() where the game folder
// cannot be written (checked once per folder by a child process, see saves.cpp).
std::filesystem::path choose_save_dir();
// parappa --write-test <dir>: the child process side of that check.
int write_test(const char *dir_utf8);

bool init(int window_scale);

// Native "open file" dialog for the disc image; returns "" if cancelled.
std::string pick_disc_image(const std::string &title);
void message_box(const std::string &title, const std::string &text);
void shutdown();

// Processes window/input events; exits the process when the window is closed.
void poll_events();
// Buttons held on controller port 1/2 (PadButton bits).
uint16_t pad_buttons(int port);

// Shows the GPU's display area and swaps buffers.
void present();

// Queues interleaved stereo 16-bit samples for playback.
void audio_push(const int16_t *samples, int frames);
// Sample frames queued but not yet played.
int audio_queued_frames();
// Frames (at AUDIO_RATE) from queueing a sample to hearing it: the queue, the device buffer and
// audio_offset_ms from settings.ini (extra delay of e.g. wireless headphones).
int audio_latency_frames();

}  // namespace host
