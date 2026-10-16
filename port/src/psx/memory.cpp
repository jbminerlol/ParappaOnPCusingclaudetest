#include "psx/memory.h"

#include <windows.h>

#include <cstring>

#include "psx/log.h"

namespace psx {

static void map_fixed(uint32_t addr, uint32_t size, const char *what) {
    void *p = VirtualAlloc(reinterpret_cast<void *>(uintptr_t(addr)), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p != reinterpret_cast<void *>(uintptr_t(addr)))
        fatal("Could not map PS1 %s at %08X (error %lu). The port must be a 32-bit large-address-aware build.",
              what, addr, GetLastError());
}

static void map_view(HANDLE section, uint32_t offset, uint32_t addr, uint32_t size, const char *what) {
    void *p = MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, offset, size, reinterpret_cast<void *>(uintptr_t(addr)));
    if (p != reinterpret_cast<void *>(uintptr_t(addr)))
        fatal("Could not map PS1 %s at %08X (error %lu). The port must be a 32-bit large-address-aware build.",
              what, addr, GetLastError());
}

void memory_init() {
    // Main RAM is one shared section viewed at the addresses the PS1 decodes to it: KSEG0 (where the
    // game's pointers live), KSEG1, and the KUSEG mirror. The first 64 KB of KUSEG (the NULL page
    // region) cannot be mapped on Windows; accesses there are redirected by the fault handler.
    HANDLE ram = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, RAM_SIZE, nullptr);
    if (!ram) fatal("Could not allocate PS1 RAM (error %lu)", GetLastError());
    map_view(ram, 0, RAM_BASE, RAM_SIZE, "RAM");
    map_view(ram, 0, 0xA0000000u, RAM_SIZE, "RAM (KSEG1)");
    // The KUSEG mirror is mapped in 64 KB pieces: Windows sometimes already has something in this
    // range (process parameters, environment), and a missing piece only loses the mirror there.
    int skipped = 0;
    for (uint32_t off = 0x10000; off < RAM_SIZE; off += 0x10000)
        if (MapViewOfFileEx(ram, FILE_MAP_ALL_ACCESS, 0, off, 0x10000, reinterpret_cast<void *>(uintptr_t(off))) !=
            reinterpret_cast<void *>(uintptr_t(off)))
            skipped++;
    if (skipped) LOGW("RAM mirror: %d of %u pieces in low memory were unavailable", skipped, RAM_SIZE / 0x10000 - 1);
    // Allocation granularity is 64 KB; only the first 1 KB is the real scratchpad.
    map_fixed(SCRATCH_BASE, 0x10000, "scratchpad");
}

uint32_t load_exe(const void *image, size_t size) {
    const uint8_t *d = static_cast<const uint8_t *>(image);
    if (size < 0x800 || std::memcmp(d, "PS-X EXE", 8) != 0) fatal("Not a PS-X EXE image");
    uint32_t pc, text, text_size;
    std::memcpy(&pc, d + 0x10, 4);
    std::memcpy(&text, d + 0x18, 4);
    std::memcpy(&text_size, d + 0x1C, 4);
    if (!in_ram(text, text_size) || size < 0x800 + text_size) fatal("Bad PS-X EXE layout");
    std::memcpy(ptr(text), d + 0x800, text_size);
    std::memset(ptr(BSS_START), 0, BSS_END - BSS_START);
    LOGI("Loaded EXE: text %08X+%X, entry %08X", text, text_size, pc);
    return pc;
}

}  // namespace psx
