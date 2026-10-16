/* MIPS address -> native function tables (generated into gen/dispatch.c). */
#pragma once
#include "ghidra.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PsxFnEntry {
    uint32_t addr;
    psx_fn_t fn;
} PsxFnEntry;

typedef struct PsxOverlay {
    int index;                       /* N of COMODN.BIN */
    const char *file;
    uint32_t fingerprint_addr;       /* code bytes that identify this overlay when loaded */
    const unsigned char *fingerprint;
    int fingerprint_len;
    const PsxFnEntry *fns;           /* sorted by addr */
    int fn_count;
} PsxOverlay;

extern const PsxFnEntry psx_main_fns[]; /* sorted by addr */
extern const int psx_main_fn_count;
extern const PsxOverlay psx_overlays[];
extern const int psx_overlay_count;

#ifdef __cplusplus
}
#endif
