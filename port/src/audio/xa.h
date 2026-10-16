// CD-XA ADPCM audio sector decoder (feeds the SPU's CD audio input at 44.1 kHz).
#pragma once
#include <cstdint>

namespace xa {

// Clears the decoder history (on seek / new stream).
void reset();

// Decodes one raw 2352-byte Mode 2 Form 2 audio sector and queues its samples.
void decode_sector(const uint8_t *raw);

}  // namespace xa
