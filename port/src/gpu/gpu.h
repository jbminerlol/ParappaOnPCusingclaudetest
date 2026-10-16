// PS1 GPU implemented with OpenGL. VRAM (1024x512, 16-bit) lives in a GPU framebuffer scaled by an
// integer factor; GP0 drawing commands are rasterized there and the display area is presented.
#pragma once
#include <cstdint>
#include <vector>

namespace gpu {

struct Rect {
    int x, y, w, h;
};

struct DisplayMode {
    int x = 0, y = 0;       // display start in VRAM
    int w = 320, h = 240;   // visible size in VRAM pixels
    bool rgb24 = false;
    bool enabled = false;
};

bool init(int scale);
void reset();

// GP0 command stream (as sent through DMA). Words are consumed in full commands.
void gp0(const uint32_t *words, int count);
// Walks a GPU ordering table / linked packet list starting at PS1 address 'addr'.
void draw_otag(uint32_t addr);

void load_image(const Rect &r, const uint16_t *pixels);
void store_image(const Rect &r, uint16_t *pixels);
void move_image(const Rect &r, int x, int y);
void clear_image(const Rect &r, uint8_t red, uint8_t green, uint8_t blue);

void set_display(const DisplayMode &mode);
const DisplayMode &display();

// Draws the current display area into the window's default framebuffer.
void present(int window_w, int window_h);
// The current picture at the internal resolution (4:3), rendered offscreen so it does not depend
// on the window being visible: RGB rows, bottom row first.
bool capture(std::vector<uint8_t> &rgb, int *w, int *h);

}  // namespace gpu
