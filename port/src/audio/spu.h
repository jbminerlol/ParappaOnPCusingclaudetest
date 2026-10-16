// PS1 SPU: 512 KB sound RAM, 24 ADPCM voices with ADSR envelopes, and the CD audio input.
// Mixed at 44.1 kHz and handed to the host once per vblank.
#pragma once
#include <cstdint>

namespace spu {

constexpr int VOICES = 24;
constexpr uint32_t RAM_SIZE = 512 * 1024;

void init();

// Sound RAM transfers (address in bytes).
void write_ram(uint32_t addr, const void *data, uint32_t size);
uint8_t *ram();

struct VoiceParams {
    uint32_t start;        // ADPCM start address (bytes, 8-aligned)
    uint16_t pitch;        // 0x1000 = 44.1 kHz
    int16_t vol_l, vol_r;  // 0..0x3FFF
    uint16_t adsr1, adsr2; // SPU ADSR registers
};

void key_on(int voice, const VoiceParams &p);
void key_off(int voice);
void set_volume(int voice, int16_t vol_l, int16_t vol_r);
void set_pitch(int voice, uint16_t pitch);
bool voice_active(int voice);
// Voice whose envelope is lowest (free voices first), for voice allocation.
int pick_voice(uint32_t mask = 0xFFFFFF);

void set_main_volume(int16_t left, int16_t right);  // 0..0x3FFF
void set_cd_volume(int16_t left, int16_t right);    // 0..0x7FFF
void set_cd_enabled(bool enabled);

// CD audio (decoded XA / CD-DA) at 44.1 kHz stereo, consumed by the mixer.
void cd_push(const int16_t *frames, int count);
int cd_buffered();
void cd_clear();
// Running totals of CD audio frames pushed / consumed by the mixer (dropped frames count as
// consumed), so a position in the CD audio stream can be matched to what is playing.
uint64_t cd_pushed();
uint64_t cd_consumed();

}  // namespace spu
