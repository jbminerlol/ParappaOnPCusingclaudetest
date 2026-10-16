// Native libgpu entry points also used by the other native libraries.
#pragma once
#include <cstdint>

#include "psyq_types.h"

extern "C" {
int psyq_ResetGraph(int mode);
int psyq_SetDispMask(int mask);
int psyq_PutDispEnv(const DISPENV *env);
int psyq_PutDrawEnv(const DRAWENV *env);
int psyq_DrawOTag(const uint32_t *ot);
int psyq_ClearOTagR(uint32_t *ot, int n);
int psyq_LoadImage(const RECT *r, const uint32_t *data);
int psyq_ClearImage(const RECT *r, int red, int green, int blue);
}

namespace libgpu {
// Links a primitive into an ordering table entry (AddPrim).
void add_prim(uint32_t *ot_entry, void *prim);
}  // namespace libgpu
