// libcd streaming (St*): assembles STR movie frames from data sectors read in streaming mode.
//
// Each STR sector's 2048 data bytes start with a 32-byte header (frame number, sector index and
// count, frame size, width, height) followed by 2016 bytes of frame data. Complete frames are
// assembled contiguously in two slots inside the game's ring buffer: [32-byte header][data].
#include <cstring>

#include "psx/log.h"
#include "psx/memory.h"
#include "psyq/libcd.h"

namespace {

constexpr int DATA_PER_SECTOR = 2016;
constexpr int SLOTS = 2;

enum class Slot { Free, Filling, Ready, InUse };

struct St {
    uint8_t *ring = nullptr;
    uint32_t ring_bytes = 0;
    uint32_t slot_bytes = 0;
    uint32_t start_frame = 0, end_frame = 0xFFFFFFFFu;
    Slot state[SLOTS] = {};
    uint32_t frame[SLOTS] = {};
    uint32_t received[SLOTS] = {};  // bitmask of sectors
    uint32_t ready_order[SLOTS] = {};
    uint32_t ready_seq = 0;
} g;

uint8_t *slot_base(int s) {
    return g.ring + size_t(s) * g.slot_bytes;
}

}  // namespace

namespace st {

void deliver_sector(const uint8_t *raw) {
    if (!g.ring) return;
    const uint8_t *data = raw + 24;
    uint16_t type, index, count;
    uint32_t frame_no;
    std::memcpy(&type, data + 2, 2);
    std::memcpy(&index, data + 4, 2);
    std::memcpy(&count, data + 6, 2);
    std::memcpy(&frame_no, data + 8, 4);
    if ((type & 0x7FFF) != 0x0001 || count == 0 || count > 32 || index >= count) return;  // not video
    if (frame_no < g.start_frame || (g.end_frame != 0xFFFFFFFFu && frame_no > g.end_frame)) return;
    if (32 + size_t(count) * DATA_PER_SECTOR > g.slot_bytes) {
        PSX_TODO_ONCE("St: frame of %u sectors does not fit the ring", count);
        return;
    }
    int s = -1;
    for (int i = 0; i < SLOTS; i++)
        if (g.state[i] == Slot::Filling && g.frame[i] == frame_no) s = i;
    if (s < 0) {
        for (int i = 0; i < SLOTS && s < 0; i++)
            if (g.state[i] == Slot::Free) s = i;
        // A slot still filling an older frame lost sectors (e.g. ones read before StSetStream):
        // reuse it, as the hardware ring overwrites stale data.
        for (int i = 0; i < SLOTS && s < 0; i++)
            if (g.state[i] == Slot::Filling && g.frame[i] < frame_no) s = i;
        if (s < 0) return;  // ring full of complete frames: drop this one, as on hardware
        g.state[s] = Slot::Filling;
        g.frame[s] = frame_no;
        g.received[s] = 0;
    }
    uint8_t *base = slot_base(s);
    if (index == 0) std::memcpy(base, data, 32);
    std::memcpy(base + 32 + size_t(index) * DATA_PER_SECTOR, data + 32, DATA_PER_SECTOR);
    g.received[s] |= 1u << index;
    if (g.received[s] == ((count == 32) ? 0xFFFFFFFFu : (1u << count) - 1)) {
        g.state[s] = Slot::Ready;
        g.ready_order[s] = g.ready_seq++;
        LOGD("St: frame %u ready (%u sectors)", frame_no, count);
    }
}

}  // namespace st

extern "C" {

int psyq_StSetRing(uint32_t *ring, int sectors) {
    g = St{};
    g.ring = reinterpret_cast<uint8_t *>(ring);
    g.ring_bytes = uint32_t(sectors) * 2048;
    g.slot_bytes = g.ring_bytes / SLOTS;
    return 0;
}

int psyq_StSetStream(int mode, uint32_t start_frame, uint32_t end_frame, uint32_t func1, uint32_t func2) {
    (void)mode;
    LOGD("StSetStream %u..%u", start_frame, end_frame);
    if (func1 || func2) PSX_TODO_ONCE("StSetStream callbacks are not supported");
    g.start_frame = start_frame;
    g.end_frame = end_frame;
    for (Slot &s : g.state) s = Slot::Free;
    return 0;
}

int psyq_StUnSetRing() {
    g = St{};
    return 0;
}

// Returns 0 and the oldest complete frame (data, header), or 1 if none is ready.
int psyq_StGetNext(uint32_t *addr, uint32_t *header) {
    int best = -1;
    for (int i = 0; i < SLOTS; i++)
        if (g.state[i] == Slot::Ready && (best < 0 || g.ready_order[i] < g.ready_order[best])) best = i;
    if (best < 0) return 1;
    g.state[best] = Slot::InUse;
    *header = psx::addr_of(slot_base(best));
    *addr = psx::addr_of(slot_base(best) + 32);
    return 0;
}

int psyq_StFreeRing(uint32_t addr) {
    LOGD("StFreeRing %08X", addr);
    for (int i = 0; i < SLOTS; i++)
        if (g.state[i] == Slot::InUse && addr - psx::addr_of(slot_base(i)) < g.slot_bytes) g.state[i] = Slot::Free;
    return 0;
}

}  // extern "C"
