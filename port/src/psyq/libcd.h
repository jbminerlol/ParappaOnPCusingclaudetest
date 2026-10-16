// Internal interfaces between the native libcd, its streaming library (St*) and the runtime.
#pragma once
#include <cstdint>

#include "psyq_types.h"

namespace cd {
void init();
void start_stream(uint8_t mode);
}  // namespace cd

namespace st {
// A data sector read while streaming (raw 2352 bytes).
void deliver_sector(const uint8_t *raw);
}  // namespace st
