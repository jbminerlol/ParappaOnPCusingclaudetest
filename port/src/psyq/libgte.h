// Native libgte: fixed-point matrix/vector math with the PS1 GTE's conventions (4.12 fixed point,
// 4096 = 1.0, angles in 1/4096 turns) and the GTE state used by the 3D sorters.
#pragma once
#include <cstdint>

#include "psyq_types.h"

namespace gte {

struct State {
    MATRIX rot{};      // current rotation + translation (SetRotMatrix / SetTransMatrix)
    int32_t ofx = 0, ofy = 0;  // screen offset
    int32_t h = 1000;          // projection plane distance
};

State &state();
void set_projection(int h);

int32_t sin12(int32_t angle);
int32_t cos12(int32_t angle);

// out = m.rot * v >> 12, plus translation.
void rot_trans(const MATRIX &m, const SVECTOR &v, int32_t out[3]);
// Perspective projection of a view-space point; returns false when behind the camera.
bool project(const int32_t v[3], int32_t *sx, int32_t *sy);

void mul_matrix(const MATRIX &a, const MATRIX &b, MATRIX &out);  // out.m = a.m * b.m >> 12

}  // namespace gte
