// PS1 address space for the native port. Main RAM (2 MB) and the scratchpad are mapped at their
// original KSEG0 / physical addresses, so PS1 pointers in game code and data work unchanged.
#pragma once
#include <cstddef>
#include <cstdint>

namespace psx {

constexpr uint32_t RAM_BASE = 0x80000000u;
constexpr uint32_t RAM_SIZE = 0x200000u;
constexpr uint32_t SCRATCH_BASE = 0x1F800000u;
constexpr uint32_t SCRATCH_SIZE = 0x400u;

// Layout of SCUS_941.83.
constexpr uint32_t EXE_TEXT = 0x80010000u;
constexpr uint32_t BSS_START = 0x8006ECB8u;  // cleared by _start
constexpr uint32_t BSS_END = 0x801C3870u;
constexpr uint32_t OVERLAY_BASE = 0x801C3870u;
constexpr uint32_t STACK_TOP = 0x801FFFF0u;

// Reserves and commits the PS1 regions at their fixed addresses (fatal on failure).
void memory_init();

// Loads a PS-X EXE image into RAM, clears BSS, and returns its entry point.
uint32_t load_exe(const void *image, size_t size);

inline bool in_ram(uint32_t addr, uint32_t len = 1) {
    uint32_t a = addr & 0x1FFFFFFFu;
    return a < RAM_SIZE && a + len <= RAM_SIZE;
}

// PS1 address (any of KUSEG/KSEG0/KSEG1 for RAM) -> host pointer. Addresses outside RAM and the
// scratchpad yield nullptr.
inline uint8_t *ptr(uint32_t addr) {
    uint32_t phys = addr & 0x1FFFFFFFu;
    if (phys < RAM_SIZE) return reinterpret_cast<uint8_t *>(uintptr_t(RAM_BASE + phys));
    if (addr - SCRATCH_BASE < SCRATCH_SIZE) return reinterpret_cast<uint8_t *>(uintptr_t(addr));
    return nullptr;
}

template <typename T>
inline T *ptr_as(uint32_t addr) {
    return reinterpret_cast<T *>(ptr(addr));
}

inline uint32_t addr_of(const void *p) {
    return uint32_t(uintptr_t(p));
}

// Pointers below 2 MB (KUSEG, including NULL) are mirrors of main RAM on the PS1 and the game does
// pass them to library functions (e.g. an uninitialized coordinate pointer). Native code must map
// them onto the RAM mapping, since the low pages cannot be mapped on Windows.
template <typename T>
inline T *mirror(T *p) {
    uint32_t a = addr_of(p);
    return a < RAM_SIZE ? reinterpret_cast<T *>(uintptr_t(RAM_BASE + a)) : p;
}

}  // namespace psx
