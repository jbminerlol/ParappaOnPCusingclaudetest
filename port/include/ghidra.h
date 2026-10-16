/* Types and helper macros used by Ghidra's decompiler output (PSX:LE:32 / MIPS o32 data model).
 * Included by the generated game code; usable from C and C++. */
#pragma once
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

typedef uint8_t undefined;
typedef uint8_t undefined1;
typedef uint16_t undefined2;
typedef uint32_t undefined3;
typedef uint32_t undefined4;
typedef uint64_t undefined8;
typedef uint8_t byte;
typedef int8_t sbyte;
typedef uint16_t word;
typedef uint32_t dword;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef uint32_t ulong; /* MIPS long is 32-bit (as is Win32 long) */
typedef int64_t longlong;
typedef uint64_t ulonglong;
typedef void *pointer;
#define PSYQ_BASIC_TYPES
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef uint32_t u_long;
typedef int code();
typedef int func();

#include "psyq_types.h"

/* Ghidra pseudo-ops for partial/combined values. */
#define CONCAT11(a, b) ((uint16_t)(((uint16_t)(uint8_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT12(a, b) ((uint32_t)(((uint32_t)(uint8_t)(a) << 16) | (uint16_t)(b)))
#define CONCAT13(a, b) ((uint32_t)(((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(b) & 0xFFFFFFu)))
#define CONCAT21(a, b) ((uint32_t)(((uint32_t)(uint16_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT22(a, b) ((uint32_t)(((uint32_t)(uint16_t)(a) << 16) | (uint16_t)(b)))
#define CONCAT31(a, b) ((uint32_t)(((uint32_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT44(a, b) ((uint64_t)(((uint64_t)(uint32_t)(a) << 32) | (uint32_t)(b)))
#define CONCAT26(a, b) ((uint64_t)(((uint64_t)(uint16_t)(a) << 48) | ((uint64_t)(b) & 0xFFFFFFFFFFFFull)))
#define CONCAT17(a, b) ((uint64_t)(((uint64_t)(uint8_t)(a) << 56) | ((uint64_t)(b) & 0xFFFFFFFFFFFFFFull)))
#define SUB41(v, off) ((uint8_t)((uint32_t)(v) >> ((off) * 8)))
#define SUB42(v, off) ((uint16_t)((uint32_t)(v) >> ((off) * 8)))
#define SUB21(v, off) ((uint8_t)((uint16_t)(v) >> ((off) * 8)))
#define SUB81(v, off) ((uint8_t)((uint64_t)(v) >> ((off) * 8)))
#define SUB82(v, off) ((uint16_t)((uint64_t)(v) >> ((off) * 8)))
#define SUB84(v, off) ((uint32_t)((uint64_t)(v) >> ((off) * 8)))
#define ZEXT14(v) ((uint32_t)(uint8_t)(v))
#define ZEXT24(v) ((uint32_t)(uint16_t)(v))
#define ZEXT48(v) ((uint64_t)(uint32_t)(v))
#define SEXT14(v) ((int32_t)(int8_t)(v))
#define SEXT24(v) ((int32_t)(int16_t)(v))
#define SEXT48(v) ((int64_t)(int32_t)(v))
#define CARRY4(a, b) ((uint32_t)(a) + (uint32_t)(b) < (uint32_t)(a))
#define SCARRY4(a, b) (((int32_t)(a) > 0 && (int32_t)(b) > INT32_MAX - (int32_t)(a)) || \
                       ((int32_t)(a) < 0 && (int32_t)(b) < INT32_MIN - (int32_t)(a)))
#define SBORROW4(a, b) (((int32_t)(b) < 0 && (int32_t)(a) > INT32_MAX + (int32_t)(b)) || \
                        ((int32_t)(b) > 0 && (int32_t)(a) < INT32_MIN + (int32_t)(b)))

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*psx_fn_t)();

/* Resolves a MIPS function address (main EXE, PsyQ entry point, or the loaded overlay). */
psx_fn_t psx_resolve(uint32_t addr);
#define PSX_CALL(addr) (psx_resolve((uint32_t)(addr)))

/* MIPS 'break' (compiler-inserted divide-by-zero / overflow checks). */
void psx_trap(int code);

/* Called by functions the decompiler could not produce (see src/game/fixed). */
void psx_unimplemented(const char *name);

/* Shifts with MIPS semantics for a count of 32 (an empty mask), used by unaligned loads/stores. */
static inline uint32_t psx_shl(uint32_t v, uint32_t n) { return n >= 32 ? 0u : v << n; }
static inline uint32_t psx_shr(uint32_t v, uint32_t n) { return n >= 32 ? 0u : v >> n; }
#define PSX_SHL(v, n) psx_shl((uint32_t)(v), (uint32_t)(n))
#define PSX_SHR(v, n) psx_shr((uint32_t)(v), (uint32_t)(n))

/* Debug log line from game code (src/game/fixed overrides). */
void psx_debug(const char *fmt, ...);

#ifdef __cplusplus
}
#endif
