// Read-only access to the game disc image (raw MODE2/2352 BIN) and its ISO9660 file system.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace psx {

constexpr int RAW_SECTOR = 2352;
constexpr int DATA_SECTOR = 2048;

struct DiscFile {
    uint32_t lba;   // first sector (0 = MSF 00:02:00)
    uint32_t size;  // bytes
};

class Disc {
public:
    // Opens a .cue sheet (its first data track) or a raw MODE2/2352 .bin. A cooked 2048-byte image
    // still opens, but it has no XA audio, so the music is missing.
    bool open(const std::string &path);
    void close();
    uint32_t sector_count() const { return sectors_; }
    bool cooked() const { return sector_size_ == DATA_SECTOR; }
    // True if this is PaRappa the Rapper (US, SCUS-94183): SYSTEM.CNF boots SCUS_941.83.
    bool is_parappa();

    // Raw 2352-byte sector (sync, header, subheader, data, EDC/ECC). Returns false past the end.
    bool read_raw(uint32_t lba, uint8_t *out);
    // User data (2048 bytes) of a Mode 2 Form 1 sector.
    bool read_data(uint32_t lba, uint8_t *out);

    // Looks up an ISO9660 path such as "\\S0\\COMMON.INT;1" (case-insensitive, version optional).
    bool find(const std::string &path, DiscFile *out);

private:
    bool find_in_dir(uint32_t lba, uint32_t size, const std::string &name, DiscFile *out);

    FILE *file_ = nullptr;
    uint32_t sectors_ = 0;
    int sector_size_ = RAW_SECTOR;
};

extern Disc g_disc;

// LBA <-> BCD MSF (CdlLOC layout), with the 150-sector lead-in offset.
void lba_to_msf(uint32_t lba, uint8_t *minute, uint8_t *second, uint8_t *sector);
uint32_t msf_to_lba(uint8_t minute, uint8_t second, uint8_t sector);

}  // namespace psx
