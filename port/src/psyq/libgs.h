// libgs internals shared between the 2D (libgs.cpp) and 3D (libgs3d.cpp) parts.
#pragma once
#include <cstdint>

namespace libgs {
int screen_w();
int screen_h();
uint32_t swap_count();
uint32_t *&work_base();  // packet area pointer (GsSetWorkBase)
short pos_x();
short pos_y();
}  // namespace libgs
